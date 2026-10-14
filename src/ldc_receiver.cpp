#include "ldc_receiver.h"

#include <QImage>
#include <QByteArray>
#include <QTimer>
#include <QDebug>

// libdatachannel（仅本 .cpp 引入，头文件保持零依赖）
#include "rtc/rtc.hpp"

LdcReceiver &LdcReceiver::instance()
{
    static LdcReceiver inst;
    return inst;
}

LdcReceiver::LdcReceiver(QObject *parent) : QObject(parent) {}

// ── 信令入口：被控端 offer / candidate 经云端转来 ──
void LdcReceiver::onSignal(const QString &type, const QByteArray &payload)
{
    if (type == QLatin1String("offer")) {
        // 已有 PC：二次 offer = 重协商，setRemoteDescription 到同一 PC 即可
        if (m_pc) {
            auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
            try {
                pc->setRemoteDescription(rtc::Description(payload.constData(), "offer"));
                pc->setLocalDescription();
            } catch (const std::exception &e) {
                emit failed(QStringLiteral("renegotiate offer: %1").arg(QString::fromLocal8Bit(e.what())));
            }
            return;
        }
        // ⚠️ 不能在发送方的信号发射栈内**同步**建 PC：发送方 PC 正在协商，
        // 此时新建第二个 PC 会并发触碰 libdatachannel 全局 Init（PollService 线程），
        // 实测 0xC0000005。延迟到事件循环再建。
        const QByteArray sdp = payload;
        QTimer::singleShot(0, this, [this, sdp]() { createPc(sdp); });
        return;
    } else if (type == QLatin1String("candidate")) {
        auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
        if (pc) pc->addRemoteCandidate(rtc::Candidate(payload.constData()));
    }
}

void LdcReceiver::createPc(const QByteArray &payload)
{
    try {
        auto pc = std::make_shared<rtc::PeerConnection>(rtc::Configuration{});
        m_pc = pc;
        qInfo() << "[viewer-rtc] 收流 PeerConnection 已建，等待协商";

        // 本地 answer 与 candidate 由接线层转回云端 → 被控端
        pc->onLocalDescription([this](rtc::Description desc) {
            if (desc.type() == rtc::Description::Type::Answer)
                emit signalAnswer(QByteArray::fromStdString(desc));
        });
        pc->onLocalCandidate([this](rtc::Candidate cand) {
            emit signalCandidate(QByteArray::fromStdString(cand.candidate()));
        });
        pc->onStateChange([this](rtc::PeerConnection::State st) {
            const bool conn = st == rtc::PeerConnection::State::Connected;
            if (conn != m_connected.load()) {
                m_connected.store(conn);
                emit stateChanged(conn);
            }
            if (st == rtc::PeerConnection::State::Failed) {
                emit failed(QStringLiteral("PeerConnection Failed"));
                // 2026-10-14 修（审计 P1）：Failed 之后不拆掉这台 PC，RTC 就再也没救。
                // 原来 stop() 全库零调用点 → 这台 PC 一辈子不复用。于是后续每台设备的
                // offer 都落进 onSignal 的「已有 PC → 重协商」分支（onSignal:23），
                // 往一台**已死**的 PC 上 setRemoteDescription：answer 永远不生成，
                // RTC 整个进程死在这儿，只能一直吃 JPEG 兜底，而且日志上看不出来为什么。
                // 延时到事件循环再拆 —— 不能在这台 PC 自己的回调栈里 close 它自己
                //（和上面 offer 建 PC 用 singleShot(0) 是同一个约束，见本文件 :38 的注释）。
                QTimer::singleShot(0, this, [this]() { stop(); });
            }
        });

        // 收流：track 的每个 message 是 RTP 包（RtpHeader + JPEG 载荷），剥头解 JPEG。
        // 2026-10-12 四·双画面：按 track mid 分流 —— "camera" 轨走 cameraFrameReady，
        // 其余（"screen"）走 frameReady，屏幕路径行为不变。
        pc->onTrack([this](std::shared_ptr<rtc::Track> track) {
            const bool isCam = (track->mid() == "camera");
            {
                QMutexLocker lock(&m_mu);
                if (isCam) m_camTrack = track.get();
                else       m_track = track.get();
            }
            track->onMessage([this, isCam](rtc::binary data) {
                const auto *raw = reinterpret_cast<const unsigned char *>(data.data());
                const size_t n = data.size();
                if (n < 12) return;   // RTP 固定头至少 12B
                const QByteArray bytes(reinterpret_cast<const char *>(raw + 12), int(n - 12));
                QImage img;
                if (img.loadFromData(bytes, "JPEG")) {
                    if (isCam) emit cameraFrameReady(img);
                    else       emit frameReady(img);
                }
            }, nullptr);
        });

        pc->setRemoteDescription(rtc::Description(payload.constData(), "offer"));
        qDebug() << "[receiver] offer set, generating answer...";
        // ⚠️ 2026-10-10 修：接收方必须响应 offer 的 media（addTrack），
        // answer 才会带媒体生成（onLocalDescription 才回调）。缺这一步 =
        // setLocalDescription 返回但永远等不到 answer（联调实测卡死）。
        const rtc::Description offer(payload.constData(), "offer");
        for (int i = 0; i < offer.mediaCount(); ++i) {
            // media(i) 是 variant<Media*, Application*>：视频/音频 media 才响应，
            // application（DataChannel 的 SDP 描述）没有 addTrack 概念，跳过。
            const auto media = offer.media(i);
            // offer 是 const → media(i) 返回 variant<const Media*, const Application*>
            // ⚠️ 不用 get_if（libdatachannel 版本里 const Media 模板推导歧义触发
            //    static assert），用 hold_alternative + get 显式取。
            if (std::holds_alternative<const rtc::Description::Media *>(media)) {
                pc->addTrack(*std::get<const rtc::Description::Media *>(media));
            }
        }
        pc->setLocalDescription();   // 生成 answer
        qDebug() << "[receiver] setLocalDescription returned";
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("offer: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}

void LdcReceiver::stop()
{
    if (!m_pc) return;
    m_connected.store(false);
    rtc::PeerConnection *pc = nullptr;
    {
        QMutexLocker lock(&m_mu);
        pc = static_cast<rtc::PeerConnection *>(m_pc.get());
        // close 是异步的；PC 由 m_pc(shared_ptr) 持有，reset 后引用计数自然归零
    }
    if (pc) { try { pc->close(); } catch (...) {} }
    {
        QMutexLocker lock(&m_mu);
        m_pc.reset();
        m_track = nullptr;
        m_camTrack = nullptr;
    }
    emit stateChanged(false);
}