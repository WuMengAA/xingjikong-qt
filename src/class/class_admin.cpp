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

// ── 发布中心（P1 第一刀：通知三件套 + 作业）──
// 与 post() 同口径但单独写：成功文案要带"送达 N / 在线 M"台数（post() 通用分支没有这些字段），
// 重认证门 / 网络失败兜底与 post() 一致。凭据现取 backend->httpBaseUrl() / viewerToken()。
void ClassAdmin::publish(const QString &classCode, const QString &type,
                        const QString &title, const QString &body)
{
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        emit opFinished(QStringLiteral("publish"), false,
                        QStringLiteral("云端地址未配置，无法发布"));
        return;
    }
    QJsonObject card;
    card.insert(QStringLiteral("type"), type);
    card.insert(QStringLiteral("title"), title);
    card.insert(QStringLiteral("body"), body);
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), classCode);
    b.insert(QStringLiteral("card"), card);

    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/publish")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
    auto *r = m_nam->post(req, QJsonDocument(b).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (status == 200 && o.value(QStringLiteral("ok")).toBool()) {
            const int delivered = o.value(QStringLiteral("delivered")).toInt();
            const int online = o.value(QStringLiteral("online")).toInt();
            emit opFinished(QStringLiteral("publish"), true,
                            QStringLiteral("已发布（送达 %1 / 在线 %2）").arg(delivered).arg(online));
            return;
        }
        // 重认证门（403 + REAUTH_REQUIRED）：会话还在，弹重登提示即可。
        const QString sem = o.value(QStringLiteral("code")).toString();
        if (status == 403 && sem == QLatin1String("REAUTH_REQUIRED")) {
            const QString msg = o.value(QStringLiteral("error")).toString();
            emit reauthRequired(msg.isEmpty() ? QStringLiteral("需要重新验证密码") : msg);
            emit opFinished(QStringLiteral("publish"), false,
                            msg.isEmpty() ? QStringLiteral("需要重新验证密码") : msg);
            return;
        }
        QString msg = o.value(QStringLiteral("error")).toString();
        if (msg.isEmpty())
            msg = r->error() == QNetworkReply::NoError
                ? QStringLiteral("HTTP %1").arg(status) : r->errorString();
        emit opFinished(QStringLiteral("publish"), false, msg);
    });
}

// ── 学生名单 + 班级课表（2026-10-25 · 云端数据库）──────────────────────
// 用户裁决「云端数据库存储，不要本机了」：名单和课表都跟班走，不再存在本机档案里。
//
// 为什么列表类另走 fetchJson 而不用 post：post 的语义是"写操作回执"（成功就弹 toast），
// 而拉名单/拉课表是**读**——失败不该打断用户，只该把表清空并记日志。

void ClassAdmin::fetchJson(const QString &action, const QJsonObject &body,
                           std::function<void(const QJsonObject &)> onOk)
{
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        logf("[viewer] FAIL 班级数据读取跳过（%s）：云端地址未配置", action.toUtf8().constData());
        if (onOk) onOk(QJsonObject());
        return;
    }
    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/") + action));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());

    auto *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, action, onOk] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (status == 200 && o.value(QStringLiteral("ok")).toBool()) {
            if (onOk) onOk(o);
            return;
        }
        const QString sem = o.value(QStringLiteral("code")).toString();
        const QString msg = o.value(QStringLiteral("error")).toString();
        if (status == 403 && sem == QLatin1String("REAUTH_REQUIRED")) {
            emit reauthRequired(msg.isEmpty() ? QStringLiteral("需要重新验证密码") : msg);
        } else if (!msg.isEmpty()) {
            logf("[viewer] FAIL 班级数据读取失败（%s / HTTP %d）：%s",
                 action.toUtf8().constData(), status, msg.toUtf8().constData());
        }
        if (onOk) onOk(QJsonObject());
    });
}

void ClassAdmin::students(const QString &code)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    fetchJson(QStringLiteral("students"), b, [this, code](const QJsonObject &o) {
        QVariantList list;
        const QJsonArray arr = o.value(QStringLiteral("students")).toArray();
        for (const QJsonValue &v : arr) {
            if (v.isObject()) list.append(v.toObject().toVariantMap());
        }
        emit studentsList(code, list);
    });
}

void ClassAdmin::addStudent(const QString &code, const QString &name,
                            const QString &studentNo, const QString &seat)
{
    QJsonObject st;
    st.insert(QStringLiteral("name"), name);
    if (!studentNo.trimmed().isEmpty()) st.insert(QStringLiteral("studentNo"), studentNo);
    if (!seat.trimmed().isEmpty()) st.insert(QStringLiteral("seat"), seat);
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("student"), st);
    const QString tip = seat.trimmed().isEmpty()
        ? QStringLiteral("已加入学生：%1").arg(name.trimmed())
        : QStringLiteral("已加入学生：%1（座位 %2）").arg(name.trimmed(), seat.trimmed());
    post(QStringLiteral("student-add"), b, tip);
}

void ClassAdmin::updateStudent(const QString &code, const QString &studentId,
                               const QString &name, const QString &studentNo,
                               const QString &seat, const QString &note)
{
    QJsonObject patch;
    patch.insert(QStringLiteral("name"), name);
    patch.insert(QStringLiteral("studentNo"), studentNo);
    patch.insert(QStringLiteral("seat"), seat);
    patch.insert(QStringLiteral("note"), note);
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("studentId"), studentId);
    b.insert(QStringLiteral("patch"), patch);
    post(QStringLiteral("student-update"), b,
         QStringLiteral("已更新学生：%1").arg(name.trimmed()));
}

void ClassAdmin::removeStudent(const QString &code, const QString &studentId)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("studentId"), studentId);
    post(QStringLiteral("student-remove"), b, QStringLiteral("已移出该学生"));
}

void ClassAdmin::timetable(const QString &code)
{
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    fetchJson(QStringLiteral("timetable"), b, [this, code](const QJsonObject &o) {
        // 没排过课时 profile 是 null —— 这里显式转成空串，QML 只判 isEmpty，
        // 不用去区分 undefined/null/缺失三种"空"（少一个坑）。
        QString json;
        if (o.value(QStringLiteral("profile")).isObject()
            || o.value(QStringLiteral("profile")).isArray()) {
            json = QString::fromUtf8(QJsonDocument(
                o.value(QStringLiteral("profile")).isObject()
                    ? QJsonDocument(o.value(QStringLiteral("profile")).toObject())
                    : QJsonDocument(o.value(QStringLiteral("profile")).toArray()))
                .toJson(QJsonDocument::Compact));
        }
        emit timetableLoaded(code,
                             json,
                             static_cast<qint64>(o.value(QStringLiteral("updatedAt")).toDouble()));
    });
}

void ClassAdmin::saveTimetable(const QString &code, const QString &profileJson)
{
    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
    if (doc.isNull() || (!doc.isObject() && !doc.isArray())) {
        emit opFinished(QStringLiteral("timetable-save"), false,
                        QStringLiteral("课表内容为空或格式不对，没有保存"));
        return;
    }
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("profile"), doc.isObject() ? QJsonValue(doc.object())
                                                       : QJsonValue(doc.array()));
    post(QStringLiteral("timetable-save"), b, QStringLiteral("课表已保存到云端"));
}

void ClassAdmin::post(const QString &action, const QJsonObject &body, const QString &okText)
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
    connect(r, &QNetworkReply::finished, this, [this, r, action, okText] {
        r->deleteLater();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = (code == 200 && o.value(QStringLiteral("ok")).toBool());
        if (ok) {
            // 2026-10-25：学生操作要能说清"加了谁" —— 传了 okText 就用它，
            // 否则退回"班级名/码"（建班改名删班这类操作，班名就是最好的回执）。
            if (!okText.isEmpty()) {
                emit opFinished(action, true, okText);
                return;
            }
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

/* ── 班级接入码（2026-10-25 · 收归我方管理）─────────────────────────────
 * 为什么不复用 post()：那个只发 opFinished（成功时给一句文案），而接入码要的是
 * **结构化结果**（码 / 班级 / 剩余秒）——QML 得把这串码放大显示，让老师照着敲到
 * 设备上，所以必须单独一个信号把码带出去。
 * 失败路径与 post() 保持同一套口径（403 REAUTH_REQUIRED 走重认证门、其余吐云端
 * 人话、json 空时用 errorString 兜底），不另造第二套错误语义。
 */
void ClassAdmin::issueBindCode(const QString &classCode)
{
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        emit opFinished(QStringLiteral("bindcode"), false,
                        QStringLiteral("云端地址未配置，无法生成接入码"));
        return;
    }
    const QString cc = classCode.trimmed();
    if (cc.isEmpty()) {
        emit opFinished(QStringLiteral("bindcode"), false, QStringLiteral("先选一个班级"));
        return;
    }

    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/bindcode")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
    QJsonObject body;
    body.insert(QStringLiteral("classCode"), cc);

    auto *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (http == 200 && o.value(QStringLiteral("ok")).toBool()) {
            const QString newCode = o.value(QStringLiteral("code")).toString();
            const QString ccode = o.value(QStringLiteral("classCode")).toString();
            const QString cname = o.value(QStringLiteral("className")).toString();
            const qint64 exp = static_cast<qint64>(o.value(QStringLiteral("expiresAt")).toDouble());
            const int ttl = o.value(QStringLiteral("ttlSec")).toInt();
            emit bindCodeIssued(ccode, cname, newCode, exp, ttl);
            emit opFinished(QStringLiteral("bindcode"), true, newCode);
            return;
        }
        const QString sem = o.value(QStringLiteral("code")).toString();
        if (http == 403 && sem == QLatin1String("REAUTH_REQUIRED")) {
            QString msg = o.value(QStringLiteral("error")).toString();
            emit reauthRequired(msg.isEmpty()
                ? QStringLiteral("长时间未验证，操作被拦截，请重新登录一次")
                : msg);
            emit opFinished(QStringLiteral("bindcode"), false, msg.isEmpty()
                ? QStringLiteral("需要重新验证密码") : msg);
            return;
        }
        QString msg = o.value(QStringLiteral("error")).toString();
        if (msg.isEmpty()) {
            msg = r->error() == QNetworkReply::NoError
                ? QStringLiteral("HTTP %1").arg(http)
                : r->errorString();
        }
        emit opFinished(QStringLiteral("bindcode"), false, msg);
    });
}

void ClassAdmin::revokeBindCode(const QString &code)
{
    // 作废不需要结构化结果：走通用 post（成功文案直接用码本身，人能对上号）。
    const QString c = code.trimmed();
    if (c.isEmpty()) {
        emit opFinished(QStringLiteral("bindcode-revoke"), false,
                        QStringLiteral("没有可作废的接入码"));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("code"), c);
    post(QStringLiteral("bindcode-revoke"), body, c);
}
