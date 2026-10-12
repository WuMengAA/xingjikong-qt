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
            // ── 2026-10-11 诊断：整条 RTC 链路此前**零日志**，
            //    "被控端起了 ldc 但云端从没收到过 offer" 这件事只能靠猜。
            //    这里把回调是否到达、类型是什么、SDP 多长全打出来。──
            const bool isOffer = desc.type() == rtc::Description::Type::Offer;
            qInfo("[rtc-ldc] onLocalDescription 回调到达 type=%s offerSent=%d len=%d",
                  isOffer ? "offer" : "answer", (int)m_offerSent,
                  (int)std::string(desc).size());
            if (isOffer && !m_offerSent) {
                m_offerSent = true;
                const QByteArray sdp = QByteArray::fromStdString(desc);
                qInfo("[rtc-ldc] 发射 signalOffer（%d 字节）", sdp.size());
                emit signalOffer(sdp);
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
        // 摄像头 track（"camera"）：与屏幕同一 PC，随首个 offer 一起协商（不额外 renegotiate）。
        // ⚠️ 只建轨、不推帧——摄像头帧要等管理端按需 startCamera() 才发（隐私红线：不看不采）。
        //    用不同 payload type（97）避免与屏幕轨的 96 在 SDP 里撞车。
        rtc::Description::Video camMedia("camera", rtc::Description::Direction::SendOnly);
        camMedia.addH264Codec(97);
        auto camTrk = pc->addTrack(camMedia);
        {
            QMutexLocker lock(&m_mu);
            m_track = trk.get();
            m_camTrack = camTrk.get();
            m_dc = pc->createDataChannel("rtc").get();
            m_pc = pc;
        }
        // offer 发起方：下一轮事件循环再 setLocalDescription —— 让 negotiationNeeded
        // 先跑完，否则同样报 "No ... to negotiate"（实测 start 立即发会失败）
        QTimer::singleShot(0, this, [this]() {
            auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
            // ── 2026-10-11 诊断：这一段跑没跑、跑完有没有回调，以前全靠猜 ──
            if (!pc) { qWarning("[rtc-ldc] FAIL 延迟发 offer 时 PC 已经没了"); return; }
            qInfo("[rtc-ldc] 延迟发 offer：调 setLocalDescription()");
            try {
                pc->setLocalDescription();
                qInfo("[rtc-ldc] setLocalDescription() 已返回（等 onLocalDescription 回调）");
            } catch (const std::exception &e) {
                qWarning("[rtc-ldc] FAIL setLocalDescription: %s", e.what());
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

void LdcStreamer::renegotiate()
{
    QMutexLocker lock(&m_mu);
    auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
    if (!pc) { qWarning("[rtc-ldc] renegotiate：PC 不存在（还没 start？）"); return; }
    m_offerSent = false;   // 让 onLocalDescription 的 offer 分支再发一次 signalOffer
    lock.unlock();
    QTimer::singleShot(0, this, [this]() {
        auto *p = static_cast<rtc::PeerConnection *>(m_pc.get());
        if (!p) { qWarning("[rtc-ldc] renegotiate：PC 在单次回调前已销毁"); return; }
        try {
            p->setLocalDescription();   // 同一条 SDP 媒体线重新协商 → 新 offer
            qInfo("[rtc-ldc] renegotiate：重新 setLocalDescription（新订阅者拿 offer）");
        } catch (const std::exception &e) {
            qWarning("[rtc-ldc] FAIL renegotiate: %s", e.what());
            emit failed(QStringLiteral("renegotiate: %1").arg(QString::fromLocal8Bit(e.what())));
        }
    });
}

void LdcStreamer::stop()
{
    if (!m_active && !m_pc) return;
    m_active = false;
    m_capture.stop();
    stopCamera();            // 先杀 ffmpeg 采集进程（否则推流停了它还占着摄像头）
    m_connected = false;
    if (m_pc) {
        auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
        // 只 close：libdatachannel 内部线程还持有引用，close() 后 shared_ptr
        // 引用计数自然归零才安全析构（PC 由 m_pc 持有，重置即释放所有权）。
        try { pc->close(); } catch (...) {}
        m_pc.reset();
        m_track = nullptr;
        m_camTrack = nullptr;
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

// ═══════════════════════════════════════════════════════════════════════════
// 摄像头第二轨（2026-10-12 · UI 评审四·双画面 · 方案 A：RTC 第二 track）
//
// 与屏幕轨同一 PC、同一 RTP 发法（payload=整帧 JPEG、marker=1），差别只在：
//   · mid = "camera"（管理端按 mid 分流到摄像头格）
//   · 帧来自 ffmpeg 连续 MJPEG 管道（非 QScreen::grabWindow）
//   · 按需启停（管理端打开摄像头格才 startCamera —— 隐私红线：不看不采）
// ═══════════════════════════════════════════════════════════════════════════

void LdcStreamer::startCamera(const QString &device, const QString &ffmpegBin)
{
    if (m_camActive) return;
    if (!m_active || !m_pc) {
        emit failed(QStringLiteral("startCamera: RTC 未启动（先 rtc-start）"));
        return;
    }
    if (!m_camTrack) {
        emit failed(QStringLiteral("startCamera: camera 轨未建立"));
        return;
    }
    const QString ff = ffmpegBin.isEmpty() ? QStringLiteral("ffmpeg") : ffmpegBin;
    m_camDevice = device.trimmed().isEmpty() ? QStringLiteral("video0") : device.trimmed();

    m_camBuf.clear();
    m_camSeq = 0;
    m_camProc = new QProcess(this);
    m_camProc->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(m_camProc, &QProcess::readyReadStandardOutput,
                     this, &LdcStreamer::onCamStdout);
    QObject::connect(m_camProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int, QProcess::ExitStatus) {
                         if (m_camActive)
                             qWarning("[rtc-ldc] 摄像头采集进程退出（设备 %s）", qPrintable(m_camDevice));
                     });
    // -f mjpeg → stdout 是连续 JPEG（按 FFD8…FFD9 切）；-r 10 控帧率，-q:v 7 控体积。
    // 通道分着收（SeparateChannels）：stderr 里的设备错误要单独拿得到，便于回执人话原因。
    m_camProc->start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                           QStringLiteral("error"), QStringLiteral("-f"),
                           QStringLiteral("dshow"), QStringLiteral("-i"),
                           QStringLiteral("video=%1").arg(m_camDevice),
                           QStringLiteral("-f"), QStringLiteral("mjpeg"),
                           QStringLiteral("-q:v"), QStringLiteral("7"),
                           QStringLiteral("-r"), QStringLiteral("10"),
                           QStringLiteral("pipe:1") });
    m_camActive = true;
    emit cameraStateChanged(true);
    qInfo("[rtc-ldc] 摄像头采集已开（device=%s）", qPrintable(m_camDevice));
}

void LdcStreamer::stopCamera()
{
    if (!m_camActive && !m_camProc) return;
    m_camActive = false;
    if (m_camProc) {
        QObject::disconnect(m_camProc, nullptr, this, nullptr);
        if (m_camProc->state() != QProcess::NotRunning) {
            m_camProc->terminate();
            if (!m_camProc->waitForFinished(2000)) { m_camProc->kill(); m_camProc->waitForFinished(1500); }
        }
        delete m_camProc;
        m_camProc = nullptr;
    }
    m_camBuf.clear();
    emit cameraStateChanged(false);
    qInfo("[rtc-ldc] 摄像头采集已停");
}

void LdcStreamer::onCamStdout()
{
    if (!m_camProc) return;
    m_camBuf += m_camProc->readAllStandardOutput();
    // MJPEG 切帧：找 SOI(FFD8)…EOI(FFD9)。跨 readyRead 保留半帧。
    for (;;) {
        const int soi = m_camBuf.indexOf(QByteArrayLiteral("\xFF\xD8"));
        if (soi < 0) {
            if (m_camBuf.size() > 1) m_camBuf = m_camBuf.right(1);   // 只留可能的首字节
            break;
        }
        if (soi > 0) m_camBuf.remove(0, soi);
        const int eoi = m_camBuf.indexOf(QByteArrayLiteral("\xFF\xD9"), 2);
        if (eoi < 0) break;                       // 半帧，等下次 readyRead
        const QByteArray jpeg = m_camBuf.left(eoi + 2);
        m_camBuf.remove(0, eoi + 2);
        sendCameraFrame(jpeg);
        if (m_camBuf.size() > (4 << 20)) m_camBuf.clear();   // 脏数据护栏
    }
}

void LdcStreamer::sendCameraFrame(const QByteArray &jpeg)
{
    if (!m_camActive || !m_connected.load() || jpeg.isEmpty()) return;
    rtc::Track *track = nullptr;
    {
        QMutexLocker lock(&m_mu);
        track = static_cast<rtc::Track *>(m_camTrack);
    }
    if (!track) return;
    bool open = false;
    try { open = track->isOpen(); } catch (...) { open = false; }
    if (!open) return;

    constexpr size_t kRtpHead = 12;
    QByteArray pkt;
    pkt.resize(int(kRtpHead) + jpeg.size());
    auto *rtp = reinterpret_cast<rtc::RtpHeader *>(pkt.data());
    rtp->preparePacket();
    rtp->setPayloadType(97);
    rtp->setMarker(true);
    rtp->setSsrc(43);                              // 与屏幕轨(42)区分
    rtp->setSeqNumber(uint16_t(m_camSeq));
    rtp->setTimestamp(uint32_t(m_camSeq) * 9000);  // 90kHz：10fps → 每帧 1/10s
    std::memcpy(pkt.data() + kRtpHead, jpeg.constData(), size_t(jpeg.size()));
    try {
        track->send(reinterpret_cast<const std::byte *>(pkt.constData()), size_t(pkt.size()));
        ++m_camSeq;
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("camera track send: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}