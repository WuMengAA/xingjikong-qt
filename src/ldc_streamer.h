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
    void stop();

    bool active() const { return m_active; }
    bool connected() const { return m_connected.load(); }

signals:
    void stateChanged(bool active);
    void frameSent(int seq);
    void failed(const QString &reason);
    // 信令出站（接线层转发到云端 → 管理端）：offer / candidate
    void signalOffer(const QByteArray &sdp);
    void signalCandidate(const QByteArray &candidate);

private:
    explicit LdcStreamer(QObject *parent = nullptr);

    void captureAndSend();   // 抓一帧 → JPEG → 发 track
    QByteArray encodeJpeg(const QPixmap &pm);

    // libdatachannel 对象是 shared_ptr 语义（enable_shared_from_this），
    // 必须用 shared_ptr 持有，裸 new 后无持有者会立即析构（0xC0000005）
    std::shared_ptr<void> m_pc;   // rtc::PeerConnection*
    QMutex m_mu;                  // 保护 m_track/m_dc/m_pc（回调线程 vs 主线程）
    void *m_track = nullptr;      // rtc::Track*（由 PC 持有生命周期）
    void *m_dc = nullptr;         // rtc::DataChannel*（同上）
    bool m_offerSent = false;     // 首个 offer 已发出（只发一次）
    QTimer m_capture;
    QScreen *m_screen = nullptr;
    bool m_active = false;
    std::atomic<bool> m_connected{false};   // libdatachannel 回调线程写，主线程读
    int m_seq = 0;
    int m_captureMs = 200;
};