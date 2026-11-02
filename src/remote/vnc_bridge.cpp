// 远控 · 被控端 VNC 桥（统一方案 A · vnc-data 走主 WS）—— 纯字节桥，详见 vnc_bridge.h。
#include "vnc_bridge.h"

#include <QAbstractSocket>

VncBridge &VncBridge::instance()
{
    static VncBridge s;
    return s;
}

VncBridge::VncBridge(QObject *parent) : QObject(parent)
{
    // socket 终身单例（2026-10-18 崩溃修复）：原来每次 stop 都 close+deleteLater，
    // 实机反复崩溃在 Qt6Core（栈顶 = VncBridge::cleanup 调用的 Qt 内部函数）。
    // 改为构造时建一次、信号只连一次；cleanup 只 abort()（对象保留），零删除竞态。
    m_tcp = new QTcpSocket(this);
    connect(m_tcp, &QTcpSocket::connected, this, &VncBridge::onTcpConnected);
    connect(m_tcp, &QTcpSocket::readyRead, this, &VncBridge::onTcpReadyRead);
    connect(m_tcp, &QTcpSocket::errorOccurred, this, &VncBridge::onTcpError);
    connect(m_tcp, &QTcpSocket::disconnected, this, &VncBridge::onTcpDisconnected);
    connect(m_tcp, &QTcpSocket::bytesWritten, this, &VncBridge::onTcpBytesWritten);
    // 16ms 批量合帧（≈60fps）：把本地 VNC 产出的字节攒成一帧再发主 WS，
    // 避免"鼠标动一下、画面闪一下"就发一个微小的 WS 帧（规格：减少网络包开销）。
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(16);
    connect(m_flushTimer, &QTimer::timeout, this, &VncBridge::onFlushTick);
}

void VncBridge::start(quint16 port)
{
    if (m_active) stop();          // 同一时刻只允许 1 条桥：新的先清旧的
    m_port = port;
    m_active = true;
    m_tcpOpen = false;
    m_wsToTcp.clear();
    m_tcpToFrame.clear();

    qInfo("[vnc-bridge] 启动远控桥：连 127.0.0.1:%d", port);

    // 只连本机 loopback（安全边界）：云端永远拿不到真实 host:port。
    m_tcp->abort();   // 清掉上一次会话可能的残留连接状态
    m_tcp->connectToHost(QStringLiteral("127.0.0.1"), port);

    m_flushTimer->start();
}

void VncBridge::stop()
{
    if (!m_active) return;
    cleanup();
}

void VncBridge::cleanup()
{
    const bool wasActive = m_active;
    m_active = false;
    m_tcpOpen = false;
    m_wsToTcp.clear();
    m_tcpToFrame.clear();
    m_flushTimer->stop();
    if (m_tcp) m_tcp->abort();   // 只断连接不删对象（socket 与桥同生命周期）
    if (wasActive) {
        qInfo("[vnc-bridge] 远控桥已关闭");
        emit status(QStringLiteral("远控桥已关闭"));
    }
}

// ── 入向（主 WS vnc-data → 本地 VNC）────────────────────────────────────────
void VncBridge::feedDown(const QByteArray &data)
{
    if (data.isEmpty() || !m_active) { qInfo("[vnc-probe] feedDown 丢弃（空或未激活）"); return; }
    m_wsToTcp.append(data);
    flushWsToTcp();
}

void VncBridge::flushWsToTcp()
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
        // TCP loopback 极快，几乎不背压；这里只作安全阀：写缓冲超阈值就留在入向缓冲，
        // 等 onTcpBytesWritten 再 flush（不会无限堆积）。
        if (m_tcp->bytesToWrite() > kMaxBuffer) break;
    }
}

// ── 出向（本地 VNC → 主 WS vnc-data）────────────────────────────────────────
void VncBridge::onTcpReadyRead()
{
    if (!m_tcp) return;
    m_tcpToFrame.append(m_tcp->readAll());
    // 不立即发：等 16ms 定时器合帧（规格：定时器批量发送，减少小包）。
}

void VncBridge::onFlushTick()
{
    flushTcpToFrame();
}

void VncBridge::flushTcpToFrame()
{
    if (m_tcpToFrame.isEmpty()) return;
    // 背压保护（规格口径）：积压超上限宁可丢最旧也不占内存 —— 说明网络太慢
    // 或对端解码跟不上，重连/降帧让用户重试，绝不无限增长。
    if (m_tcpToFrame.size() > kMaxBuffer) {
        qWarning("[vnc-bridge] ⚠️ 出向积压超 %d MB，丢最旧字节防内存膨胀",
                 (int)(kMaxBuffer / 1024 / 1024));
        m_tcpToFrame.remove(0, m_tcpToFrame.size() - (int)kResumeAt);
        if (m_tcpToFrame.isEmpty()) return;
    }
    const QByteArray chunk = m_tcpToFrame;
    m_tcpToFrame.clear();
    qInfo("[vnc-probe] 桥出向 %d 字节", chunk.size());
    emit outBytes(chunk);   // → main.cpp 打包 makeVncFrameBytes 发主 WS
}

// ── TCP（本地 VNC）侧 ────────────────────────────────────────────────────────
void VncBridge::onTcpConnected()
{
    m_tcpOpen = true;
    qInfo("[vnc-bridge] 本地 VNC 已连 127.0.0.1:%d", m_port);
    flushWsToTcp();   // 把主 WS 先于 TCP 到达的字节灌进去
    emit status(QStringLiteral("远控桥已贯通（VNC 服务已接入）"));
}

void VncBridge::onTcpError(QAbstractSocket::SocketError)
{
    if (m_tcp) qWarning("[vnc-bridge] 本地 VNC TCP 出错：%s", qPrintable(m_tcp->errorString()));
    cleanup();
}

void VncBridge::onTcpDisconnected()
{
    qInfo("[vnc-bridge] 本地 VNC TCP 断开（VNC 服务可能停了）");
    cleanup();
}

void VncBridge::onTcpBytesWritten(qint64)
{
    flushWsToTcp();   // TCP 排空 → 继续搬入向缓冲
}
