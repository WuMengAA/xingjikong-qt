#pragma once
// 被控端 WebRTC 推流组件（2026-10-08 · 换其他 webrtc：libdatachannel 替换 QWebEngine）
//
// 定位：被控端 captureStream 的 libdatachannel 版。独立 QObject，不依赖 main.cpp：
//   · 建 PeerConnection（**offer 发起方**，与现有协议一致：被控端先 offer）
//   · 抓屏（QScreen::grabWindow）→ JPEG 编码 → 作为 H264/JPEG 帧经 track 发出
//   · 由云端中继信令（offer→answer/candidate）——与现有 rtc-* 消息协议同构
//
// 用法（接入 main.cpp，等 WorkBuddy 提交后接线）：
//   LdcStreamer::instance().start(captureMs)      // 发起 offer + 开始抓屏推流
//   LdcStreamer::instance().onSignal("answer", ...)  // 喂云端转来的 answer
//   LdcStreamer::instance().onSignal("candidate", ...)
//   LdcStreamer::instance().stop()
// 信号：stateChanged / frameSent(seq) / failed(reason)
//
// ⚠️ 本组件编译需要 _probe 里的 libdatachannel（先静态链 datachannel-static.lib）。
// 若仓库还没引入该依赖，本文件处于"可编译验证、未接线"状态。

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QPixmap>
#include <QScreen>
#include <QMutex>
#include <QProcess>
#include <QString>
#include <memory>
#include <atomic>

namespace rtc { class PeerConnection; class DataChannel; class Track; }

class LdcStreamer : public QObject {
    Q_OBJECT
public:
    static LdcStreamer &instance();

    // 喂云端转来的信令：type ∈ {offer, candidate}（本组件为 answer 侧）
    void onSignal(const QString &type, const QByteArray &payload);

    // 开始抓屏推流；captureMs = 抓帧间隔（默认 200ms = 5fps）
    void start(int captureMs = 200);
    // 2026-10-11：已在推流时重新协商（新订阅的管理端也要拿到 offer）。
    // 重置 m_offerSent 并重新 setLocalDescription → onLocalDescription 再发 signalOffer。
    void renegotiate();
    void stop();

    bool active() const { return m_active; }
    bool connected() const { return m_connected.load(); }

    // ── 摄像头第二轨（2026-10-12 · UI 评审四·双画面 · 方案 A：RTC 第二 track）──
    //   「摄像头实时画面」：屏幕走 "screen" 轨（既有），摄像头走 "camera" 轨（本条）。
    //   摄像头**按需**：只有管理端打开摄像头格时才 startCamera()（隐私红线：不看不采）。
    //   ffmpeg 连续输出 MJPEG → 按 SOI/EOI 切帧 → 经 camera track 发 RTP。
    //   ⚠️ 需 RTC 已 start()（PC/轨道已建）。device 空则回落 video0；ffmpegBin 由接线层传入。
    void startCamera(const QString &device, const QString &ffmpegBin);
    void stopCamera();
    bool cameraActive() const { return m_camActive; }

signals:
    void stateChanged(bool active);
    void frameSent(int seq);
    void failed(const QString &reason);
    // 摄像头轨就绪/停止（接线层可据此回执管理端「摄像头已开/已关」）
    void cameraStateChanged(bool active);
    // 信令出站（接线层转发到云端 → 管理端）：offer / candidate
    void signalOffer(const QByteArray &sdp);
    void signalCandidate(const QByteArray &candidate);

private:
    explicit LdcStreamer(QObject *parent = nullptr);

    void captureAndSend();   // 抓一帧 → 写 ffmpeg 编码器 → 读 H264 → 发 track

    // ── 屏幕轨 H264（2026-10-16：ffmpeg 子进程 libx264 + libdatachannel H264RtpPacketizer）──
    void startFfmpeg();               // 启动 ffmpeg 编码器子进程（rawvideo→libx264→h264 stdout）
    void stopFfmpeg();
    void onFfmpegStdout();            // ffmpeg stdout → 切 H264 帧 → track->send
    void sendH264Frame(const QByteArray &frame);

    QByteArray encodeJpeg(const QPixmap &pm);   // 保留（camera/兼容路径备用）

    void onCamStdout();                       // ffmpeg stdout → 按 SOI/EOI 切帧 → 发 camera track
    void sendCameraFrame(const QByteArray &jpeg);

    // libdatachannel 对象是 shared_ptr 语义（enable_shared_from_this），
    // 必须用 shared_ptr 持有，裸 new 后无持有者会立即析构（0xC0000005）
    std::shared_ptr<void> m_pc;   // rtc::PeerConnection*
    // ⚠️ 2026-10-17（H264 0 帧根因）：libdatachannel 的 mTracks 存 **weak_ptr**，
    //    addTrack() 返回的 shared_ptr 是 track 的唯一持有者 —— start() 里局部变量
    //    析构 → track 销毁 → 触发 "negotiation needed to remove track" renegotiation
    //    → 协商破坏 → 0 帧。必须用成员 shared_ptr 持住 track 生命周期。
    std::shared_ptr<void> m_trackHold;    // rtc::Track*（screen 轨唯一持有者）
    std::shared_ptr<void> m_camTrackHold; // rtc::Track*（camera 轨唯一持有者）
    std::shared_ptr<void> m_dcHold;       // rtc::DataChannel*（唯一持有者）
    QMutex m_mu;                  // 保护 m_track/m_dc/m_pc（回调线程 vs 主线程）
    void *m_track = nullptr;      // rtc::Track*（screen 轨，指向 m_trackHold 同对象）
    void *m_dc = nullptr;         // rtc::DataChannel*（同上）
    bool m_offerSent = false;     // 首个 offer 已发出（只发一次）
    QTimer m_capture;
    QScreen *m_screen = nullptr;
    bool m_active = false;
    std::atomic<bool> m_connected{false};   // libdatachannel 回调线程写，主线程读
    int m_seq = 0;
    int m_captureMs = 200;
    // ── H264 编码（2026-10-16）──
    QProcess *m_ffmpeg = nullptr;       // ffmpeg 编码器子进程（屏幕 → H264 NAL）
    QByteArray m_ffmpegBuf;             // H264 stdout 跨 readyRead 缓冲（半帧残留）
    std::shared_ptr<void> m_rtpCfg;     // rtc::RtpPacketizationConfig*（screen 轨；timestamp 递增靠它）
    int m_encW = 320, m_encH = 240;     // 编码尺寸（ffmpeg -s 固定；抓屏先 scale）
    // ── 摄像头第二轨（2026-10-12）──
    void *m_camTrack = nullptr;   // rtc::Track*（camera 轨；由 PC 持有）
    std::shared_ptr<void> m_camRtpCfg;  // rtc::RtpPacketizationConfig*（camera 轨；2026-10-17 H264 化）
    QProcess *m_camProc = nullptr;  // ffmpeg -f mjpeg pipe（连续采集）
    bool m_camActive = false;
    int m_camSeq = 0;
    QByteArray m_camBuf;          // MJPEG 切帧缓冲（跨 readyRead 保留半帧）
    QString m_camDevice;
};