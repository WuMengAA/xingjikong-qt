// 星集控被控端 · 首次启动引导（OOBE Wizard）数据面实现（2026-10-20）
// 设计与接口契约见 oobe_wizard.h 头部。
#include "oobe_wizard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QDebug>

namespace {

// 扫码轮询：2 秒一次（用户要求）；最多 150 次（5 分钟）⇒ 当作「码过期」，给刷新按钮留活口。
constexpr int kPollIntervalMs = 2000;
constexpr int kPollLimit = 150;
// 单请求超时：8 秒。断网/后端挂掉时不至于让界面「转圈」到天荒地老。
constexpr int kTimeoutMs = 8000;

/** 绑定码格式：云端发的 6 位一次性票（2026-10-25 班级接入码收归我方后，
 *  旧站点 `XJK-XXXX-XXX` 码已废止，不再识别）。易混字符由发码方保证去除。
 */
bool codeFormatOk(const QString &code)
{
    static const QRegularExpression six(QStringLiteral("^[A-Z0-9]{6}$"));
    return six.match(code).hasMatch();
}

/**
 * 网络错误 → **能照着做点什么**的提示。
 * 纪律：不出现 wss / token / 接口名 / 状态码；只告诉用户下一步能做什么。
 */
QString friendlyNetworkError(QNetworkReply *reply)
{
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("连不上服务器，请检查这台设备的网络后重试。");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("等待服务器超时了，请稍后重试。");
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("服务器暂时没响应，请稍后重试。");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("这台设备的时间可能不准，校对时间后再试一次。");
    default:
        return QStringLiteral("网络不太稳定，请重试；也可以先用「输入绑定码」。");
    }
}

QString friendlyHttpError(int status)
{
    if (status == 401 || status == 403) return QStringLiteral("这台设备还没有登记，请联系管理员。");
    if (status == 404) return QStringLiteral("绑定服务暂不可用，请改用「输入绑定码」。");
    if (status >= 500) return QStringLiteral("服务器正忙，请稍后再试。");
    return QStringLiteral("这一步没成功，请重试；也可以改用「输入绑定码」。");
}

} // namespace

OobeWizard::OobeWizard(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_poll(new QTimer(this))
{
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &OobeWizard::pollOnce);
}

OobeWizard::~OobeWizard() = default;

void OobeWizard::setMachineCode(const QString &uid)
{
    if (m_uid == uid) return;
    m_uid = uid;
    emit infoChanged();
}

void OobeWizard::setSiteBase(const QString &siteBase)
{
    m_siteBase = siteBase;
    while (m_siteBase.endsWith(QLatin1Char('/'))) m_siteBase.chop(1);
}

void OobeWizard::setState(int s)
{
    if (m_state == s) return;
    m_state = s;
    emit bindingStateChanged();
}

void OobeWizard::fail(const QString &friendly)
{
    stopScanning();
    m_error = friendly;
    emit errorTextChanged();
    setState(Failed);
    qWarning("[oobe] ⚠ %s", qPrintable(friendly));
}

void OobeWizard::succeed(const QString &classCode, const QString &className)
{
    stopScanning();
    m_classCode = classCode;
    m_className = className;
    m_error.clear();
    m_done = true;
    emit errorTextChanged();
    emit infoChanged();
    setState(Success);
    emit boundSucceeded(classCode, className);
    qInfo("[oobe] ✅ 绑定成功：班级=%s 班级码=%s",
          qPrintable(className.isEmpty() ? QStringLiteral("（未回班级名）") : className),
          qPrintable(classCode.isEmpty() ? QStringLiteral("（未回班级码）") : classCode));
}

void OobeWizard::stopScanning()
{
    if (m_poll->isActive()) m_poll->stop();
    m_pollCount = 0;
}

void OobeWizard::startScan()
{
    if (m_done) return;
    if (m_state == Success) return;
    m_error.clear();
    emit errorTextChanged();
    setState(WaitingForScan);
    refreshQrCode();
}

void OobeWizard::refreshQrCode()
{
    if (m_done) return;
    if (m_qrBusy) return;                    // 连点刷新不叠请求
    if (m_siteBase.isEmpty()) {
        fail(QStringLiteral("这台设备的服务器地址还没配置好，请先在设置里填好再回来绑定。"));
        return;
    }

    stopScanning();                          // 换码即作废旧码的轮询计数
    m_qrBusy = true;
    m_qrMatrix.clear();
    m_qrCode.clear();
    emit qrChanged();
    setState(WaitingForScan);

    QUrl url(m_siteBase + QStringLiteral("/api/device/bind-qr"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), m_uid);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply *rep = m_nam->get(req);

    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        m_qrBusy = false;
        emit qrChanged();

        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (rep->error() != QNetworkReply::NoError) {
            // 网络层失败与「后端没这个接口」分开说：前者可以重试，后者只能换路。
            if (status == 404) fail(friendlyHttpError(404));
            else fail(friendlyNetworkError(rep));
            return;
        }
        if (status != 200) {
            fail(friendlyHttpError(status));
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        const QString scanCode = o.value(QStringLiteral("code")).toString().trimmed();
        const QJsonArray arr = o.value(QStringLiteral("matrix")).toArray();

        // 矩阵：每行一个字符串（"0101…"）。逐行校验，任何一行不像矩阵就整体拒绝 ——
        // 宁可提示「换用输入绑定码」，也不画一个扫不出来的码。
        QVariantList rows;
        int width = -1;
        bool ok = !arr.isEmpty();
        for (const QJsonValue &v : arr) {
            const QString row = v.toString();
            if (row.isEmpty() || row.size() != (width < 0 ? row.size() : width)) { ok = false; break; }
            for (QChar c : row) {
                if (c != QLatin1Char('0') && c != QLatin1Char('1')) { ok = false; break; }
            }
            if (!ok) break;
            width = row.size();
            rows.append(row);
        }
        if (!ok || rows.size() < 9 || width < 9 || rows.size() != width) {
            fail(QStringLiteral("二维码没能生成出来，请点「换一张」重试，或改用「输入绑定码」。"));
            return;
        }

        m_qrCode = scanCode;
        m_qrMatrix = rows;
        emit qrChanged();
        qInfo("[oobe] 二维码已就绪（%dx%d）", width, rows.size());
        // 扫码轮询只在这里启动：没有有效二维码就没有可轮询的东西
        m_pollCount = 0;
        m_poll->start();
    });
}

void OobeWizard::pollOnce()
{
    if (m_done || m_qrCode.isEmpty() || m_siteBase.isEmpty()) {
        stopScanning();
        return;
    }
    if (++m_pollCount > kPollLimit) {
        fail(QStringLiteral("这个二维码等太久了，已经过期。点「换一张」重新生成即可。"));
        return;
    }

    QUrl url(m_siteBase + QStringLiteral("/api/device/bind-status"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), m_uid);
    q.addQueryItem(QStringLiteral("code"), m_qrCode);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply *rep = m_nam->get(req);

    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        if (m_done || m_state != WaitingForScan) return;   // 期间用户可能已返回/已跳过
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (rep->error() != QNetworkReply::NoError || status != 200) {
            // 轮询失败**不弹错**：网络抖一下不该让正在扫码的人看到一个红框。
            // 只有连续失败到上限才由 m_pollCount 判过期（见上）。
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (!o.value(QStringLiteral("bound")).toBool()) return;   // 还没扫：继续等
        succeed(o.value(QStringLiteral("classCode")).toString().trimmed(),
                o.value(QStringLiteral("className")).toString().trimmed());
    });
}

void OobeWizard::submitBindingCode(const QString &code)
{
    if (m_done) return;
    const QString c = code.trimmed().toUpper();
    if (!codeFormatOk(c)) {
        fail(QStringLiteral("绑定码是 6 位字母数字，请对一下那串字符。"));
        return;
    }
    if (m_siteBase.isEmpty()) {
        fail(QStringLiteral("这台设备的服务器地址还没配置好，请先在设置里填好再回来绑定。"));
        return;
    }

    stopScanning();
    m_error.clear();
    emit errorTextChanged();

    QUrl url(m_siteBase + QStringLiteral("/api/device/activate"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), c);
    q.addQueryItem(QStringLiteral("uid"), m_uid);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(kTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // 与既有 OOBE 绑定同一条路（POST 空 body；端点 query 优先）—— 不另起新端点，避免两套口径。
    QNetworkReply *rep = m_nam->post(req, QByteArray("{}"));

    connect(rep, &QNetworkReply::finished, this, [this, rep, c] {
        rep->deleteLater();
        if (m_done) return;
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (rep->error() != QNetworkReply::NoError && status == 0) {
            fail(friendlyNetworkError(rep));
            return;
        }
        if (status != 200) {
            if (status == 400 || status == 409 || status == 410) {
                fail(QStringLiteral("这个绑定码无效或已经被用过了，请让管理员再发一个。"));
            } else {
                fail(friendlyHttpError(status));
            }
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        if (!o.value(QStringLiteral("ok")).toBool(false) && o.isEmpty()) {
            fail(QStringLiteral("绑定没成功，请再试一次；还不行就找管理员确认这个码。"));
            return;
        }
        QString authoritative = o.value(QStringLiteral("classCode")).toString().trimmed();
        if (authoritative.isEmpty()) authoritative = o.value(QStringLiteral("classId")).toString().trimmed();
        if (authoritative.isEmpty()) authoritative = c;    // 老站点不回码：先落用户输入的码
        succeed(authoritative, o.value(QStringLiteral("className")).toString().trimmed());
    });
}

/** 用途归一：白名单外一律教室端（与云端 registry.normalizeDeviceRole 同口径）。
 *  ⚠️ 两端各写一遍是有意的：端侧不能信任云端一定升级过，云端也不能信任端侧不乱报。 */
static QString normalizeRole(const QString &role)
{
    return role.trimmed().toLower() == QStringLiteral("broadcast")
        ? QStringLiteral("broadcast") : QStringLiteral("classroom");
}

void OobeWizard::setInitialRole(const QString &role)
{
    const QString n = normalizeRole(role);
    if (m_role == n) return;
    m_role = n;
    emit roleChanged();
}

void OobeWizard::chooseRole(const QString &role)
{
    const QString n = normalizeRole(role);
    if (m_role == n) return;
    m_role = n;
    emit roleChanged();
    // 立即上报：不放进 finishWizard —— 「选了广播端但点稍后配置」是真实路径，
    // 那条路上跳过绑定也不能跳过用途（否则机器身份空着，云端按教室端对待）。
    emit roleChosen(m_role);
    qInfo("[oobe] 用途已选：%s", m_role == QStringLiteral("broadcast") ? "广播端" : "教室端");
}

void OobeWizard::skipWizard()
{
    if (m_done) return;
    stopScanning();
    m_done = true;
    m_error.clear();
    emit errorTextChanged();
    qInfo("[oobe] 用户选择「稍后配置」——进入未绑定/离线主界面");
    emit wizardFinished(false);
}

void OobeWizard::finishWizard()
{
    if (m_done && m_state != Success) return;
    stopScanning();
    m_done = true;
    emit wizardFinished(true);
}

void OobeWizard::dismissError()
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorTextChanged();
    // 失败态退回「可以继续操作」的态：扫码页失败 ⇒ 回 WaitingForScan（可刷新）；
    // 输入码页失败 ⇒ 回 Idle（用户改了码再提交）。由 QML 侧自行决定留在哪一页。
    if (m_state == Failed) setState(Idle);
}

bool OobeWizard::isCodeFormatValid(const QString &code) const
{
    return codeFormatOk(code.trimmed().toUpper());
}
