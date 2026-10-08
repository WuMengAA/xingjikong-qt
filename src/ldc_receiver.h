#pragma once
// 管理端 WebRTC 收流组件（2026-10-08 · 换其他 webrtc：libdatachannel 替换 QWebEngine 离屏收流）
//
// 定位：viewerbackend 里 QWebEngineView 离屏收 RTCPeerConnection 流的 libdatachannel 版。
// 独立 QObject，不依赖 viewerbackend / Main.qml：
//   · 建 PeerConnection（**answer 侧**：收被控端 offer → answer 回云端）
//   · onTrack 收视频帧 → JPEG 解成 QImage → 信号抛给 QML 显示
//
// 用法（接入 viewerbackend，等 WorkBuddy 提交后接线）：
//   LdcReceiver::instance().onSignal("offer", sdp)    // 被控端 offer 经云端转来
//   LdcReceiver::instance().onSignal("candidate", c)
//   LdcReceiver::instance().stop()
// 信号：frameReady(QImage) / stateChanged(connected) / failed(reason)
//
// ⚠️ 编译需 _probe 的 libdatachannel（datachannel-static.lib 等，见 _probe/ldc-* 构建配方）。

#include <QObject>
#include <QImage>
#include <QMutex>
#include <memory>
#include <atomic>

class LdcReceiver : public QObject {
    Q_OBJECT
public:
    static LdcReceiver &instance();

    // 喂云端转来的信令：type ∈ {offer, candidate}
    void onSignal(const QString &type, const QByteArray &payload);

    // 断开并清理
    void stop();

    bool connected() const { return m_connected.load(); }

signals:
    void frameReady(const QImage &frame);      // 每帧解码完成（QML Image source 用）
    void stateChanged(bool connected);
    void failed(const QString &reason);
    // 信令出站（接线层转发到云端 → 被控端）：answer / candidate
    void signalAnswer(const QByteArray &sdp);
    void signalCandidate(const QByteArray &candidate);

private:
    explicit LdcReceiver(QObject *parent = nullptr);
    void createPc(const QByteArray &sdp);   // 事件循环内建 PC（避免信号栈内并发）

    // libdatachannel 对象 shared_ptr 语义：裸 new 后无持有者会立即析构（0xC0000005）
    std::shared_ptr<void> m_pc;   // rtc::PeerConnection*
    QMutex m_mu;                  // 保护 m_track/m_pc（onTrack 回调线程 vs 主线程）
    void *m_track = nullptr;      // rtc::Track*
    std::atomic<bool> m_connected{false};   // 回调线程写，主线程读
};