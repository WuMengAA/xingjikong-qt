#include "class_admin.h"
#include "../viewerbackend.h"   // logf / ViewerBackend（与其他 src/class 文件同口径）

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
