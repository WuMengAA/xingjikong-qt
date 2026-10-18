// 远控隧道 · agent 侧第二条 WebSocket（RFB over WS）—— 纯字节桥，详见 tunnel_agent.h。
#include "tunnel_agent.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDateTime>
#include <QAbstractSocket>

// 协议 v1 信封（与 cloud_proto.h 同格式，这里自包含一份，避免跨 TU 链接依赖）。
static QString envelope(const QString &type, const QJsonObject &payload)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

TunnelAgent &TunnelAgent::instance()
{
    static TunnelAgent s;
    return s;
}

TunnelAgent::TunnelAgent(QObject *parent) : QObject(parent) {}

QString TunnelAgent::wsUrlFor(const QString &baseUrl) const
{
    QString u = baseUrl.trimmed();
    // 把 /ws/agent → /ws/tunnel-agent（云端 tunnel.js 的 agent 端点，无需鉴权）。
    const int i = u.lastIndexOf(QStringLiteral("/ws/agent"));
    if (i >= 0) return u.left(i) + QStringLiteral("/ws/tunnel-agent");
    if (u.endsWith(QLatin1Char('/'))) u.chop(1);
    return u + QStringLiteral("/ws/tunnel-agent");
}

void TunnelAgent::start(const QString &baseUrl, const QString &sid, const QString &key, quint16 port)
{
    if (m_active) stop();          // 每台设备同时只允许 1 条隧道：新的先清旧的
    m_sid = sid;
    m_key = key;
    m_port = port;
    m_active = true;
    m_wsOpen = false;
    m_tcpOpen = false;
    m_tcpPaused = false;
    m_wsToTcp.clear();
    m_tcpToWs.clear();

    qInfo("[tunnel] 启动远控隧道：sid=%s port=%d", qPrintable(sid), port);

    m_tcp = new QTcpSocket(this);
    connect(m_tcp, &QTcpSocket::connected, this, &TunnelAgent::onTcpConnected);
    connect(m_tcp, &QTcpSocket::readyRead, this, &TunnelAgent::onTcpReadyRead);
    connect(m_tcp, &QTcpSocket::errorOccurred, this, &TunnelAgent::onTcpError);
    connect(m_tcp, &QTcpSocket::disconnected, this, &TunnelAgent::onTcpDisconnected);
    connect(m_tcp, &QTcpSocket::bytesWritten, this, &TunnelAgent::onTcpBytesWritten);
    // 只连本机 loopback（安全边界）：云端永远拿不到真实 host:port。
    m_tcp->connectToHost(QStringLiteral("127.0.0.1"), port);

    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::connected, this, &TunnelAgent::onWsConnected);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &TunnelAgent::onWsBinaryReceived);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &TunnelAgent::onWsTextReceived);
    connect(m_ws, &QWebSocket::errorOccurred, this, &TunnelAgent::onWsError);
    connect(m_ws, &QWebSocket::disconnected, this, &TunnelAgent::onWsDisconnected);
    connect(m_ws, &QWebSocket::bytesWritten, this, &TunnelAgent::onWsBytesWritten);
    m_ws->open(QUrl(wsUrlFor(baseUrl)));
}

void TunnelAgent::stop()
{
    if (!m_active) return;
    cleanup();
}

void TunnelAgent::cleanup()
{
    const bool wasActive = m_active;
    m_active = false;
    m_wsOpen = false;
    m_tcpOpen = false;
    m_tcpPaused = false;
    m_wsToTcp.clear();
    m_tcpToWs.clear();
    if (m_ws) { m_ws->close(); m_ws->deleteLater(); m_ws = nullptr; }
    if (m_tcp) { m_tcp->close(); m_tcp->deleteLater(); m_tcp = nullptr; }
    if (wasActive) {
        qInfo("[tunnel] 远控隧道已关闭");
        emit status(QStringLiteral("远控隧道已关闭"));
    }
}

// ── WS（云端）侧 ────────────────────────────────────────────────────────────
void TunnelAgent::onWsConnected()
{
    m_wsOpen = true;
    // attach：把主 WS 下发的 sid/key 交回云端配对。
    QJsonObject p;
    p.insert(QStringLiteral("sid"), m_sid);
    p.insert(QStringLiteral("key"), m_key);
    m_ws->sendTextMessage(envelope(QStringLiteral("tunnel.attach"), p));
    qInfo("[tunnel] 第二条 WS 已连，已发 tunnel.attach（sid=%s）", qPrintable(m_sid));
    // 关键：本地 TCP（→VNC）往往比这条 WS 先连上（loopback 比网络握手快），
    // VNC 一连接就发的首字节可能已暂存在 m_tcpToWs。WS 此刻才开，必须把滞留的
    // TCP→WS 字节排空，否则 RFB 握手首包（"RFB 003.008\n"）卡死、隧道永远不贯通。
    flushTcpToWs();
}

void TunnelAgent::onWsBinaryReceived(const QByteArray &data)
{
    // 云端 → 本地 VNC（WS → TCP）。
    m_wsToTcp.append(data);
    flushWsToTcp();
}

void TunnelAgent::onWsTextReceived(const QString &msg)
{
    const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("tunnel.attached")) {
        qInfo("[tunnel] 云端确认 tunnel.attached —— 隧道贯通，开始搬字节");
        emit status(QStringLiteral("远控隧道已贯通"));
    } else if (type == QStringLiteral("tunnel.close") || type == QStringLiteral("error")) {
        qWarning("[tunnel] 云端要求关闭隧道（type=%s）", qPrintable(type));
        cleanup();
    }
}

void TunnelAgent::onWsError()
{
    if (m_ws) qWarning("[tunnel] 第二条 WS 出错：%s", qPrintable(m_ws->errorString()));
    cleanup();
}

void TunnelAgent::onWsDisconnected()
{
    qInfo("[tunnel] 第二条 WS 断开");
    cleanup();
}

void TunnelAgent::onWsBytesWritten(qint64)
{
    // WS 缓冲下降 → 恢复 TCP 读取（TCP→WS 背压释放）。
    if (m_tcpPaused && m_ws && m_ws->bytesToWrite() < kResumeAt) {
        m_tcpPaused = false;
        onTcpReadyRead();   // 把滞留数据读出来继续搬
    }
}

// ── TCP（本地 VNC）侧 ────────────────────────────────────────────────────────
void TunnelAgent::onTcpConnected()
{
    m_tcpOpen = true;
    qInfo("[tunnel] 本地 VNC 已连 127.0.0.1:%d", m_port);
    flushWsToTcp();   // 把 WS 先于 TCP 到达的字节灌进去
    flushTcpToWs();   // WS 已开则把 VNC 首字节立刻发往云端（→老师端 RFB 客户端）
}

void TunnelAgent::onTcpReadyRead()
{
    if (m_tcpPaused) return;           // 已背压：等 WS 排空再读（靠 TCP 流控反压 VNC）
    m_tcpToWs.append(m_tcp->readAll());
    flushTcpToWs();
}

void TunnelAgent::onTcpError(QAbstractSocket::SocketError)
{
    if (m_tcp) qWarning("[tunnel] 本地 VNC TCP 出错：%s", qPrintable(m_tcp->errorString()));
    cleanup();
}

void TunnelAgent::onTcpDisconnected()
{
    qInfo("[tunnel] 本地 VNC TCP 断开（VNC 服务可能停了）");
    cleanup();
}

void TunnelAgent::onTcpBytesWritten(qint64)
{
    flushWsToTcp();   // TCP 排空 → 继续搬 WS 缓冲
}

// ── 转发 + 背压 ────────────────────────────────────────────────────────────────
void TunnelAgent::flushWsToTcp()
{
    while (!m_wsToTcp.isEmpty()) {
        if (!m_tcp || !m_tcpOpen) {
            // TCP 还没连上：保留缓冲，超界丢最旧（循环缓冲区语义）。
            if (m_wsToTcp.size() > kMaxBuffer)
                m_wsToTcp.remove(0, m_wsToTcp.size() - (int)kResumeAt);
            break;
        }
        const qint64 n = m_tcp->write(m_wsToTcp);
        if (n < 0) { cleanup(); return; }
        m_wsToTcp.remove(0, (int)n);
        // TCP loopback 极快，几乎不背压；这里只作安全阀：写缓冲超阈值就留在 WS 缓冲，
        // 等 onTcpBytesWritten 再 flush（不会无限堆积）。
        if (m_tcp->bytesToWrite() > kMaxBuffer) break;
    }
}

void TunnelAgent::flushTcpToWs()
{
    if (!m_ws || !m_wsOpen) {
        if (m_tcpToWs.size() > kMaxBuffer)
            m_tcpToWs.remove(0, m_tcpToWs.size() - (int)kResumeAt);
        return;
    }
    // WS 写缓冲超阈值 → 暂停读 TCP，TCP 接收缓冲填满后由协议栈流控反压 VNC（真背压）。
    if (m_ws->bytesToWrite() >= kMaxBuffer) {
        m_tcpPaused = true;
        return;
    }
    if (!m_tcpToWs.isEmpty()) {
        m_ws->sendBinaryMessage(m_tcpToWs);
        m_tcpToWs.clear();
    }
}
