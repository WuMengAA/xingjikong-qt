#include "class_admin.h"
#include "../viewerbackend.h"   // logf / ViewerBackend（与其他 src/class 文件同口径）

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ClassAdmin::ClassAdmin(ViewerBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    // 自带 QNAM（与 InteractBridge 同款：不共用 backend 的那个，
    // 管理写操作与轮询类请求的取消/生命周期互不干扰）
    m_nam = new QNetworkAccessManager(this);
}

void ClassAdmin::createClass(const QString &className)
{
    QJsonObject b;
    b.insert(QStringLiteral("className"), className);
    post(QStringLiteral("create"), b);
}

void ClassAdmin::renameClass(const QString &code, const QString &name)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("className"), name);
    post(QStringLiteral("rename"), b);
}

void ClassAdmin::deleteClass(const QString &code, const QString &strategy)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("strategy"), strategy);
    post(QStringLiteral("delete"), b);
}

void ClassAdmin::unbindDevice(const QString &code, const QString &uid)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("uid"), uid);
    post(QStringLiteral("unbind"), b);
}

void ClassAdmin::bindDevice(const QString &code, const QString &uid)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("uid"), uid);
    post(QStringLiteral("bind"), b);
}

// ── 班级成员（人可以被添加，2026-10-24）──────────────────────────────
// 与设备成员（unbind/bind）分开：设备去留走「成员管理」的设备段，
// 人的账号走这几条 —— result 是 role(owner/teacher/viewer)。

void ClassAdmin::members(const QString &code)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        emit membersList(code, QVariantList());
        return;
    }
    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/members")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());

    auto *r = m_nam->post(req, QJsonDocument(b).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, code] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (status == 200 && o.value(QStringLiteral("ok")).toBool()) {
            // members 数组 → QVariantList（每项 {id, role, addedAt…}），QML 直接得。
            // 失败/异常时发空数组 —— 面板展示"拉不到成员"而非读半个表。
            QVariantList list;
            const QJsonArray arr = o.value(QStringLiteral("members")).toArray();
            for (const QJsonValue &v : arr) {
                if (v.isObject()) list.append(v.toObject().toVariantMap());
            }
            emit membersList(code, list);
            return;
        }
        // 403 REAUTH_REQUIRED（重认证门）→ 提示重登，但成员表保持上一次内容。
        // 其余失败 → 发空表 + 记日志（列表查询不该弹 toast 打断，写操作才会）。
        const QString msg = o.value(QStringLiteral("error")).toString();
        const QString sem = o.value(QStringLiteral("code")).toString();
        if (status == 403 && sem == QLatin1String("REAUTH_REQUIRED"))
            emit reauthRequired(msg.isEmpty() ? QStringLiteral("需要重新验证密码") : msg);
        else if (!msg.isEmpty())
            logf("[viewer] FAIL 成员清单拉取失败（%d）：%s", status, msg.toUtf8().constData());
        emit membersList(code, QVariantList());
    });
}

void ClassAdmin::addMember(const QString &code, const QString &uid, const QString &role)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("uid"), uid);
    b.insert(QStringLiteral("role"), role);
    post(QStringLiteral("member-add"), b);
}

void ClassAdmin::removeMember(const QString &code, const QString &uid)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("uid"), uid);
    post(QStringLiteral("member-remove"), b);
}

void ClassAdmin::post(const QString &action, const QJsonObject &body)
{
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        emit opFinished(action, false, QStringLiteral("云端地址未配置，无法执行班级操作"));
        return;
    }

    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/") + action));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());

    auto *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, action] {
        r->deleteLater();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = (code == 200 && o.value(QStringLiteral("ok")).toBool());
        if (ok) {
            // 成功文案带上班级名/码，QML toast 直接显示（不用再猜是哪次操作）
            const QString name = o.value(QStringLiteral("name")).toString();
            const QString cc = o.value(QStringLiteral("classCode")).toString();
            const QString shown = !name.isEmpty() ? name : cc;
            emit opFinished(action, true,
                            shown.isEmpty() ? QStringLiteral("操作完成") : shown);
            return;
        }
        // 2026-10-24 重认证门：403 + code=REAUTH_REQUIRED 不是普通失败 ——
        // 会话还在，只是该再验一次密码。走专用信号让界面弹重登提示
        // （而不是把"需要重新验证密码"当 toast 一闪而过）。
        const QString sem = o.value(QStringLiteral("code")).toString();
        if (code == 403 && sem == QLatin1String("REAUTH_REQUIRED")) {
            QString msg = o.value(QStringLiteral("error")).toString();
            emit reauthRequired(msg.isEmpty()
                ? QStringLiteral("长时间未验证，操作被拦截，请重新登录一次")
                : msg);
            emit opFinished(action, false, msg.isEmpty()
                ? QStringLiteral("需要重新验证密码") : msg);
            return;
        }
        // 云端 error 是人话（班级不存在/显示名被占用/班内还有设备…）；
        // 网络层失败（TLS 吊销检查等）时 json 为空，用 errorString 兜底 ——
        // 不许出现"点了没反应"的静默失败。
        QString msg = o.value(QStringLiteral("error")).toString();
        if (msg.isEmpty()) {
            msg = r->error() == QNetworkReply::NoError
                ? QStringLiteral("HTTP %1").arg(code)
                : r->errorString();
        }
        emit opFinished(action, false, msg);
    });
}
