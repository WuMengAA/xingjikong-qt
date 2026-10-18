#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 远控隧道 · agent 侧第二条 WebSocket（RFB over WS）
//
// 为什么是「第二条连接」：被控端主 WS（/ws/agent）只走控制/指令。开远控隧道时，云端
// tunnel.js 通过主 WS 下发 tunnel.open{sid,key,port}，本类再主动拨出第二条 WS 到
// /ws/tunnel-agent（**无需鉴权**，key 由主 WS 带下来）。连上后 attach{sid,key}，云端把
// 这条 WS 与老师的 /ws/tunnel 配对，之后云端只做字节中转。
//
// 本类职责（纯字节桥，不解析 RFB）：
//   · 第二条 WS（↔ 云端）与本地 TCP 127.0.0.1:5900（↔ UltraVNC）之间双向转发字节。
//   · WS 收到二进制 → 写本地 TCP；TCP 收到 → 经 WS 二进制发回云端（→ 老师端 RFB 客户端）。
//   · 任一端断开即清理整条隧道。
//   · 背压：单向缓冲超 4MB 暂停对侧读取，降到 1MB 以下恢复（与云端 tunnel.js 同口径）。
//
// 安全边界（写死，不靠配置）：本地 TCP 只连 127.0.0.1（loopback），目标地址由本机决定，
// 云端**永远拿不到 host:port**——它只知道一个 5900 提示值与一次性 key。
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QWebSocket>
#include <QTcpSocket>
#include <QString>

class TunnelAgent : public QObject
{
    Q_OBJECT
public:
    static TunnelAgent &instance();

    /// 启动一条隧道。
    /// @param baseUrl 形如 ws://host:port/ws/agent（内部会把路径换成 /ws/tunnel-agent）。
    /// @param sid/key 云端 tunnel.open 下发的一次性会话凭据。
    /// @param port    本地 VNC 端口（默认 5900，来自云端提示，仅本机用）。
    void start(const QString &baseUrl, const QString &sid, const QString &key, quint16 port = 5900);
    /// 停掉当前隧道（整条清理）。
    void stop();
    bool isActive() const { return m_active; }

signals:
    void status(const QString &msg);

private slots:
    void onWsConnected();
    void onWsBinaryReceived(const QByteArray &data);
    void onWsTextReceived(const QString &msg);
    void onWsError();
    void onWsDisconnected();
    void onWsBytesWritten(qint64);

    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpError(QAbstractSocket::SocketError);
    void onTcpDisconnected();
    void onTcpBytesWritten(qint64);

private:
    explicit TunnelAgent(QObject *parent = nullptr);
    QString wsUrlFor(const QString &baseUrl) const;
    void cleanup();
    void flushWsToTcp();   // WS 缓冲 → 本地 TCP
    void flushTcpToWs();   // TCP 缓冲 → WS 二进制

    QWebSocket *m_ws = nullptr;
    QTcpSocket *m_tcp = nullptr;
    QString m_sid, m_key;
    quint16 m_port = 5900;
    bool m_active = false;
    bool m_wsOpen = false;
    bool m_tcpOpen = false;
    bool m_tcpPaused = false;          // TCP→WS 背压：暂停读 TCP（靠 TCP 流控反压 VNC）

    QByteArray m_wsToTcp;             // WS 收到的字节，待写 TCP
    QByteArray m_tcpToWs;             // TCP 收到的字节，待发 WS

    static constexpr qint64 kMaxBuffer = 4 * 1024 * 1024;   // 单向积压上限
    static constexpr qint64 kResumeAt  = 1 * 1024 * 1024;   // 恢复水位
};
