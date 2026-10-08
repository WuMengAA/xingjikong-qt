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
            if (st == rtc::PeerConnection::State::Failed)
                emit failed(QStringLiteral("PeerConnection Failed"));
        });

        // 收流：track 的每个 message 是 RTP 包（RtpHeader + JPEG 载荷），剥头解 JPEG
        pc->onTrack([this](std::shared_ptr<rtc::Track> track) {
            {
                QMutexLocker lock(&m_mu);
                m_track = track.get();
            }
            track->onMessage([this](rtc::binary data) {
                const auto *raw = reinterpret_cast<const unsigned char *>(data.data());
                const size_t n = data.size();
                if (n < 12) return;   // RTP 固定头至少 12B
                const QByteArray bytes(reinterpret_cast<const char *>(raw + 12), int(n - 12));
                QImage img;
                if (img.loadFromData(bytes, "JPEG"))
                    emit frameReady(img);
            }, nullptr);
        });

        pc->setRemoteDescription(rtc::Description(payload.constData(), "offer"));
        qDebug() << "[receiver] offer set, generating answer...";
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
    }
    emit stateChanged(false);
}