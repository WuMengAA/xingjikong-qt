#include "ldc_streamer.h"

#include <QScreen>
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <QDebug>
#include <cstring>

// libdatachannel（仅本 .cpp 引入，头文件保持零依赖）
#include "rtc/rtc.hpp"

// ── 抓屏 → JPEG（被控端截图同款路径，QScreen::grabWindow）──────────────
QByteArray LdcStreamer::encodeJpeg(const QPixmap &pm)
{
    if (pm.isNull()) return QByteArray();
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    // 缩放：教室大屏 1080p 全帧 JPEG 太大，压到宽 960 足够管理端看清
    const QImage img = pm.toImage().scaledToWidth(960, Qt::SmoothTransformation);
    if (!img.save(&buf, "JPEG", 80)) return QByteArray();
    return out;
}

LdcStreamer &LdcStreamer::instance()
{
    static LdcStreamer inst;
    return inst;
}

LdcStreamer::LdcStreamer(QObject *parent) : QObject(parent)
{
    m_screen = QGuiApplication::primaryScreen();
    connect(&m_capture, &QTimer::timeout, this, &LdcStreamer::captureAndSend);
}

// ── 信令入口：云端把管理端的 answer / candidate 转过来 ──
void LdcStreamer::onSignal(const QString &type, const QByteArray &payload)
{
    auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
    if (!pc) return;
    if (type == QLatin1String("answer")) {
        try {
            pc->setRemoteDescription(rtc::Description(payload.constData(), "answer"));
        } catch (const std::exception &e) {
            emit failed(QStringLiteral("setRemoteDescription(answer): %1").arg(QString::fromLocal8Bit(e.what())));
        }
    } else if (type == QLatin1String("candidate")) {
        pc->addRemoteCandidate(rtc::Candidate(payload.constData()));
    }
}

void LdcStreamer::start(int captureMs)
{
    if (m_active) return;
    m_active = true;
    m_captureMs = captureMs;
    m_seq = 0;
    m_offerSent = false;

    try {
        rtc::Configuration cfg;
        // 信令经云端中继；ICE 用 host/中继候选（与现有 rtc-* 消息协议同构）
        auto pc = std::make_shared<rtc::PeerConnection>(cfg);

        // 本地候选回传给管理端（由上层转发到云端 → 管理端）
        pc->onLocalCandidate([this](rtc::Candidate cand) {
            emit signalCandidate(QByteArray::fromStdString(cand.candidate()));
        });
        // 本地 offer 回传（被控端是 offer 发起方）。只发**首个** offer：
        // 之后的 re-offer（negotiationNeeded 再触发）让 receiver 复用 PC 处理，
        // 重复发 offer 会撞车（2026-10-08 联调实测 Invalid ICE settings）。
        pc->onLocalDescription([this](rtc::Description desc) {
            if (desc.type() == rtc::Description::Type::Offer && !m_offerSent) {
                m_offerSent = true;
                emit signalOffer(QByteArray::fromStdString(desc));
            }
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

        // 视频 track：SendOnly H264（与现有 SDP 媒体线同构）
        rtc::Description::Video media("screen", rtc::Description::Direction::SendOnly);
        media.addH264Codec(96);
        auto trk = pc->addTrack(media);
        {
            QMutexLocker lock(&m_mu);
            m_track = trk.get();
            m_dc = pc->createDataChannel("rtc").get();
            m_pc = pc;
        }
        // offer 发起方：下一轮事件循环再 setLocalDescription —— 让 negotiationNeeded
        // 先跑完，否则同样报 "No ... to negotiate"（实测 start 立即发会失败）
        QTimer::singleShot(0, this, [this]() {
            auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
            if (!pc) return;
            try {
                pc->setLocalDescription();
            } catch (const std::exception &e) {
                emit failed(QStringLiteral("setLocalDescription: %1").arg(QString::fromLocal8Bit(e.what())));
            }
        });
        emit stateChanged(true);
    } catch (const std::exception &e) {
        m_active = false;
        emit failed(QStringLiteral("start: %1").arg(QString::fromLocal8Bit(e.what())));
    }

    if (m_active) m_capture.start(qMax(50, m_captureMs));
}

void LdcStreamer::stop()
{
    if (!m_active && !m_pc) return;
    m_active = false;
    m_capture.stop();
    m_connected = false;
    if (m_pc) {
        auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
        // 只 close：libdatachannel 内部线程还持有引用，close() 后 shared_ptr
        // 引用计数自然归零才安全析构（PC 由 m_pc 持有，重置即释放所有权）。
        try { pc->close(); } catch (...) {}
        m_pc.reset();
        m_track = nullptr;
        m_dc = nullptr;
    }
    emit stateChanged(false);
}

// ── 抓屏 → JPEG → RTP 打包 → 发 track ──
// 与官方 media-sender 示例同构：track->send 收 RTP 包（RtpHeader + 载荷）。
// 约定载荷 = 完整 JPEG 帧（payload type 96，marker=1 单包成帧）；
// H264 NAL 打包留给接线层精化（本组件验证通路优先）。
void LdcStreamer::captureAndSend()
{
    if (!m_active || !m_connected.load() || !m_pc) return;
    QPixmap pm = m_screen ? m_screen->grabWindow(0) : QPixmap();
    const QByteArray jpeg = encodeJpeg(pm);
    if (jpeg.isEmpty()) return;

    rtc::Track *track = nullptr;
    {
        QMutexLocker lock(&m_mu);
        track = static_cast<rtc::Track *>(m_track);
    }
    if (!track) return;
    // isOpen 在协商中可能抛/未就绪——包 try，未开就静默跳过（等下一帧）
    bool open = false;
    try { open = track->isOpen(); } catch (...) { open = false; }
    if (!open) return;

    // 组 RTP 包：在缓冲上构造 RtpHeader（官方 media-sender 同款，不用栈对象——
    // RtpHeader 含柔性数组 _csrc[]，栈对象按 getSize() 拷贝会越界，0xC0000005）
    constexpr size_t kRtpHead = 12;   // 固定头 12B，无 csrc
    QByteArray pkt;
    pkt.resize(int(kRtpHead) + jpeg.size());
    auto *rtp = reinterpret_cast<rtc::RtpHeader *>(pkt.data());
    rtp->preparePacket();
    rtp->setPayloadType(96);
    rtp->setMarker(true);
    rtp->setSsrc(42);
    rtp->setSeqNumber(uint16_t(m_seq));
    rtp->setTimestamp(uint32_t(m_seq) * 3000);   // 90kHz：每帧 1/30s
    std::memcpy(pkt.data() + kRtpHead, jpeg.constData(), size_t(jpeg.size()));

    try {
        track->send(reinterpret_cast<const std::byte *>(pkt.constData()), size_t(pkt.size()));
        ++m_seq;
        emit frameSent(m_seq);
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("track send: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}