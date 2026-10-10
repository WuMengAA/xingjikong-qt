#include "mobilebackend.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonValue>

// v1 信封（与 viewerbackend makeEnvelope 同结构，探针已验证）
static QString makeEnvelope(const QString &type, const QJsonObject &payload)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), QString());
    o.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

MobileBackend::MobileBackend(QObject *parent) : QObject(parent)
{
    m_url = qEnvironmentVariable("STE_MOBILE_URL", QStringLiteral("wss://control.245959623.xyz/ws/viewer")).trimmed();
    m_token = qEnvironmentVariable("STE_MOBILE_TOKEN", QString()).trimmed();
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &MobileBackend::connectCloud);
}

void MobileBackend::start()
{
    connectCloud();
}

void MobileBackend::stop()
{
    m_reconnect.stop();
    if (m_ws) m_ws->close();
}

void MobileBackend::connectCloud()
{
    if (!m_ws) {
        m_ws = new QWebSocket();
        m_ws->setParent(this);
        connect(m_ws, &QWebSocket::connected, this, &MobileBackend::onConnected);
        connect(m_ws, &QWebSocket::disconnected, this, &MobileBackend::onDisconnected);
        connect(m_ws, &QWebSocket::textMessageReceived, this, &MobileBackend::onTextMessage);
        connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
            onError((int)e, m_ws->errorString());
        });
    }
    m_ws->open(QUrl(m_url));
    setStatus(QStringLiteral("正在连接云端…"));
}

void MobileBackend::onConnected()
{
    m_connected = true;
    emit connectedChanged();
    // auth 首消息（静态令牌；后续接 OAuth 会话换接入票）
    QJsonObject p;
    p.insert(QStringLiteral("token"), m_token);
    p.insert(QStringLiteral("role"), QStringLiteral("viewer"));
    if (m_ws) m_ws->sendTextMessage(makeEnvelope(QStringLiteral("auth"), p));
    setStatus(QStringLiteral("已连上云端，正在鉴权…"));
}

void MobileBackend::onDisconnected()
{
    m_connected = false;
    m_authed = false;
    emit connectedChanged();
    emit authedChanged();
    setStatus(QStringLiteral("连接断开，正在重连…"));
    m_reconnect.start(m_reconnectMs);
}

void MobileBackend::onTextMessage(const QString &msg)
{
    const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
    const QString type = o.value(QStringLiteral("type")).toString();
    const QJsonObject payload = o.value(QStringLiteral("payload")).toObject();

    if (type == QLatin1String("authed")) {
        m_authed = true;
        emit authedChanged();
        setStatus(QStringLiteral("已鉴权"));
    } else if (type == QLatin1String("devices")) {
        m_devices = payload.value(QStringLiteral("devices")).toArray();
        emit devicesChanged();
        setStatus(QStringLiteral("已连接 · %1 台在线").arg(onlineCount()));
    } else if (type == QLatin1String("instruction-result")) {
        emit instructionResult(
            payload.value(QStringLiteral("uid")).toString(),
            payload.value(QStringLiteral("action")).toString(),
            payload.value(QStringLiteral("result")).toString(),
            payload.value(QStringLiteral("data")).toObject());
    }
}

void MobileBackend::onError(int, const QString &msg)
{
    setStatus(QStringLiteral("云端连接错误：%1").arg(msg));
}

void MobileBackend::sendLightInstruction(const QString &uid, const QString &action,
                                         const QJsonObject &params)
{
    if (!m_ws || !m_authed || uid.isEmpty()) return;
    QJsonObject p;
    p.insert(QStringLiteral("uid"), uid);
    p.insert(QStringLiteral("action"), action);
    p.insert(QStringLiteral("params"), params);
    m_ws->sendTextMessage(makeEnvelope(QStringLiteral("instruction"), p));
}

// ── P1 便捷指令（2026-10-10 · 手机端设计 §5 P1）──
void MobileBackend::pingDevice(const QString &uid)
{
    // ping = 动作探测：被控端回 action=pong（在线探测用）
    sendLightInstruction(uid, QStringLiteral("ping"));
}

void MobileBackend::shutdownDevice(const QString &uid)
{
    // 关机：云端 store.js 对 shutdown 标 no_resend（进程死前不回执，防重连补发循环重启）
    sendLightInstruction(uid, QStringLiteral("shutdown"));
}

void MobileBackend::setStatus(const QString &s)
{
    if (m_status == s) return;
    m_status = s;
    emit statusChanged();
}

int MobileBackend::onlineCount() const
{
    int n = 0;
    for (const QJsonValue &v : m_devices) {
        if (v.toObject().value(QStringLiteral("online")).toBool()) ++n;
    }
    return n;
}