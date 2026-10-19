#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 远控 · 被控端 VNC 桥（统一方案 A · vnc-data 走主 WS）
//
// 职责（纯字节桥，不解析 RFB）：
//   · 本地 TCP 127.0.0.1:<port>（↔ in-agent VncServer）与**主 WS 的 vnc-data 帧**
//     之间双向搬字节。
//   · 主 WS 收到 vnc-data（direction=down，管理端键鼠/更新请求）→ 写本地 TCP；
//     本地 TCP 收到（VNC 服务产出的 RFB 字节）→ 攒进缓冲，由 16ms 定时器批量
//     打成 vnc-data 帧（direction=up，由 main.cpp 的 makeVncFrameBytes 负责信封）发主 WS。
//   · 任一端断开即清理整条桥。
//   · 背压：单向缓冲超 4MB 丢最旧（宁可丢帧也不占内存，规格口径）；TCP 写缓冲
//     超阈值时保留在入向缓冲，等 onTcpBytesWritten 再 flush（不无限堆积）。
//
// 与旧 TunnelAgent 的差异（2026-10-18 统一方案 A）：
//   · 不再拨第二条 WS（/ws/tunnel-agent 已下线）—— 主 WS 天然已鉴权，sid/key 不再需要；
//   · 不再需要云端下发 port —— VncServer::start(0) 自动选 loopback 端口，这里只管连它。
//
// 安全边界（写死，不靠配置）：本地 TCP 只连 127.0.0.1（loopback）；云端永远拿不到
// 真实 host:port，它只在中继带 uid 的 vnc-data 帧。
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QTcpSocket>
#include <QString>
#include <QTimer>
#include <QByteArray>

class VncBridge : public QObject
{
    Q_OBJECT
public:
    static VncBridge &instance();

    /// 启动桥：连 127.0.0.1:port（本地 in-agent VNC Server 的监听端口）。
    void start(quint16 port = 5900);
    /// 停掉当前桥（整条清理）。
    void stop();
    bool isActive() const { return m_active; }

signals:
    void status(const QString &msg);
    /// 本地 VNC 产出的 RFB 字节（main.cpp 打包成 vnc-data 帧发主 WS）。
    void outBytes(const QByteArray &data);

public slots:
    /// 主 WS 收到的 vnc-data payload（管理端键鼠/请求）→ 写本地 VNC。
    void feedDown(const QByteArray &data);

private slots:
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpError(QAbstractSocket::SocketError);
    void onTcpDisconnected();
    void onTcpBytesWritten(qint64);
    void onFlushTick();          // 16ms 定时器：把 TCP 缓冲合帧发主 WS

private:
    explicit VncBridge(QObject *parent = nullptr);
    void cleanup();
    void flushWsToTcp();         // 入向缓冲 → 本地 TCP
    void flushTcpToFrame();      // TCP 缓冲 → 打包一帧 emit outBytes

    QTcpSocket *m_tcp = nullptr;
    QTimer *m_flushTimer = nullptr;
    quint16 m_port = 5900;
    bool m_active = false;
    bool m_tcpOpen = false;

    QByteArray m_wsToTcp;        // 主 WS 收到的字节，待写 TCP
    QByteArray m_tcpToFrame;     // TCP 收到的字节，待打包发主 WS

    static constexpr qint64 kMaxBuffer = 4 * 1024 * 1024;   // 单向积压上限
    static constexpr qint64 kResumeAt  = 1 * 1024 * 1024;   // 恢复水位（丢到这条以下）
};
