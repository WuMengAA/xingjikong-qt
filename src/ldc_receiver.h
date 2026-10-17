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
#include <QBuffer>
#include <QMutex>
#include <QProcess>
#include <memory>
#include <atomic>
#include "mfx_h264_decode.h"

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
    // 摄像头第二轨（2026-10-12 · 四·双画面）：mid="camera" 的轨解出的帧，
    // 与屏幕轨分开抛，接线层据此喂管理端摄像头格（screen 仍走 frameReady）。
    void cameraFrameReady(const QImage &frame);
    void stateChanged(bool connected);
    void failed(const QString &reason);
    // 信令出站（接线层转发到云端 → 被控端）：answer / candidate
    void signalAnswer(const QByteArray &sdp);
    void signalCandidate(const QByteArray &candidate);

private:
    explicit LdcReceiver(QObject *parent = nullptr);
    void createPc(const QByteArray &sdp);   // 事件循环内建 PC（避免信号栈内并发）

    // ── 屏幕轨 H264 解码（2026-10-16：ffmpeg 子进程 libavcodec —— 主方案；MF 作真机备选）──
    void startDecoder();                    // 起 ffmpeg：h264 → rawvideo bgr24 stdout
    void stopDecoder();
    void feedDecoder(const QByteArray &h264);  // onFrame 重组帧 → ffmpeg stdin
    void onDecStdout();                     // 读 bgr24 → 满一帧 → QImage → frameReady

    // libdatachannel 对象 shared_ptr 语义：裸 new 后无持有者会立即析构（0xC0000005）
    std::shared_ptr<void> m_pc;   // rtc::PeerConnection*
    QMutex m_mu;                  // 保护 m_track/m_pc（onTrack 回调线程 vs 主线程）
    void *m_track = nullptr;      // rtc::Track*（screen 轨）
    void *m_camTrack = nullptr;   // rtc::Track*（camera 轨，2026-10-12 四·双画面）
    // ⚠️ 2026-10-17 修（H264 0 帧根因·answer 侧）：libdatachannel 的 mTracks 存 weak_ptr，
    //    addTrack() 返回的 shared_ptr 是 track 的唯一持有者 —— makeAnswer 里局部变量析构
    //    → track 销毁 → 生成 answer 时该 media 被 markRemoved → SDP 里 m=video 0（port 0
    //    = 拒绝该媒体）→ agent 的 sendonly track 永远 isOpen=false → H264 0 帧。
    //    必须用成员 shared_ptr 持住 addTrack 返回值（与 agent 侧 ldc_streamer 修复同构）。
    std::shared_ptr<void> m_scTrackHold;   // rtc::Track*（screen 轨唯一持有者）
    std::shared_ptr<void> m_camTrackHold;  // rtc::Track*（camera 轨唯一持有者）
    std::atomic<bool> m_connected{false};   // 回调线程写，主线程读
    // ── H264 解码 ──
    QProcess *m_ffmpegDec = nullptr;   // 主方案：ffmpeg -f h264 → rawvideo bgr24
    QByteArray m_decBuf;               // bgr24 输出缓冲（跨 readyRead 的半帧残留）
    int m_decW = 320, m_decH = 240;    // 解码输出尺寸（与被控端编码固定 320x240 一致）
    MfxH264Decoder m_mfDec;            // 备选：MF 兜底（老师机没 ffmpeg 时）
    bool m_useMf = false;              // true = 走 MF 兜底通道
};