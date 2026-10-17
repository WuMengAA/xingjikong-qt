#include "ldc_streamer.h"

#include <QScreen>
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <cstring>
#include <vector>

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
    // 2026-10-17：ICE/DTLS 排障开关（STE_LDC_LOG=1 开 libdatachannel Debug 日志，
    // 真机看候选类型/打洞状态；平时不开免得刷爆日志）
    if (qgetenv("STE_LDC_LOG") == "1")
        rtc::InitLogger(rtc::LogLevel::Debug);
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
        // 2026-10-14 修（审计 P0）：这里原来是**整个文件唯一没有 try-catch 的** libdatachannel
        // 调用点。`rtc::Candidate` 构造要解析 SDP candidate 串、addRemoteCandidate 也会校验，
        // 格式不对就抛 std::runtime_error —— 而这条调用链是
        //   textMessageReceived → handleControlText → onSignal → 这里
        // 一路裸抛到 Qt 事件循环 = **未捕获异常 → 进程直接终止**。
        // 也就是说：云端只要转发一条畸形 rtc-ice，就能把这台教室机崩掉（远程可触发的 DoS）。
        // 同文件另外 8 处 libdatachannel 调用（setRemoteDescription / setLocalDescription×2 /
        // close / isOpen×2 / send×2）全都包着 try-catch，唯独这处漏了。
        try {
            pc->addRemoteCandidate(rtc::Candidate(payload.constData()));
        } catch (const std::exception &e) {
            emit failed(QStringLiteral("addRemoteCandidate: %1").arg(QString::fromLocal8Bit(e.what())));
        }
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
        // 信令经云端中继；ICE 打洞必须配 STUN —— 真机教室网络 NAT 后，只有 host
        // candidate（局域网 192.168.x）跨网打不通 → PeerConnection Failed → H264 一直
        // 走 WS 轮询兜底（2026-10-17 真机日志「RTC 失败：PeerConnection Failed」实证）。
        // 选腾讯 STUN（国内网络可达；Google STUN 可能被墙）。可 STE_LDC_STUN 覆盖。
        {
            const QByteArray envStun = qgetenv("STE_LDC_STUN");
            const QByteArray stun = envStun.isEmpty()
                ? QByteArray("stun:stun.miwifi.com:3478")   // 本机实测可达（腾讯/Google STUN UDP 被墙）
                : envStun;
            rtc::IceServer srv(stun.constData());
            cfg.iceServers.push_back(srv);
        }
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
        // H264RtpPacketizer（2026-10-16）：track->send(H264 帧) 自动打成 RTP
        // （分片/seq/marker 全自动）；timestamp 在 sendH264Frame 每帧递增。
        m_rtpCfg = std::make_shared<rtc::RtpPacketizationConfig>(
            42, "stelarith", 96, rtc::H264RtpPacketizer::ClockRate);
        trk->setMediaHandler(std::make_shared<rtc::H264RtpPacketizer>(
            rtc::NalUnit::Separator::StartSequence,
            std::static_pointer_cast<rtc::RtpPacketizationConfig>(m_rtpCfg)));
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

    if (m_active) {
        m_capture.start(qMax(50, m_captureMs));
        startFfmpeg();        // 2026-10-16：起 ffmpeg libx264 编码器（屏幕 → H264）
    }
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
    stopFfmpeg();            // 2026-10-16：停 ffmpeg H264 编码器（先于 PC close）
    stopCamera();            // 先杀 ffmpeg 采集进程（否则推流停了它还占着摄像头）
    m_connected = false;
    if (m_pc) {
        // 2026-10-14 修（审计 P2）：m_pc 的访问原来不统一 —— start() / renegotiate()
        // 都在 m_mu 里读，唯独 stop() 裸着。今天全在主线程所以没有真实竞争，
        // 但「同一成员有的访问点持锁、有的不持」正是本次审计里 addRemoteCandidate
        // 裸抛那类问题的同款土壤：哪天有人在子线程里碰 m_pc，这一处就是漏点。
        // 一致性优先，补齐。
        QMutexLocker lock(&m_mu);
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

// ── 抓屏 → ffmpeg 子进程编码（H264）→ H264RtpPacketizer→ track ──
// 2026-10-16：屏幕轨从「手动 JPEG-over-RTP」改为「ffmpeg libx264 编码 + H264RtpPacketizer」。
// 原因（实证）：track 的 media 声明了 addH264Codec(96)，若 track 实际发 JPEG 数据，
// 接收端（H264RtpDepacketizer）解析必失败；且手动裸 RTP 不走 libdatachannel 分片。
// 改用 libdatachannel 官方同构：track->setMediaHandler(H264RtpPacketizer)，
// 抓屏帧经 ffmpeg 编码成 H264 NAL → track->send(帧) 自动 RTP 打包。
// ffmpeg 复用被控端已有依赖（camera 也在用），libx264 软件编码跨机最稳（不挑 GPU）。
void LdcStreamer::captureAndSend()
{
    if (!m_active || !m_connected.load() || !m_pc) return;
    if (!m_ffmpeg || m_ffmpeg->state() != QProcess::Running) return;   // 编码器没起来就等
    // ⚠️ 2026-10-17 修（真机「图片/语音/文件传输异常缓慢 + 语音断续」根因）：
    //    H264 未连通（track 未 open）时**绝不能**持续抓屏写 ffmpeg —— 那会让 libx264
    //    每 200ms 空转编码一帧，白白占满 agent CPU（实测 46s+ 且不停涨），把同一台
    //    机器上的 WS 帧推、语音播放、文件写入全部拖慢。track 没开就整帧跳过，
    //    H264 通路一建立（track open）自然开始编码推流。
    rtc::Track *track = nullptr;
    {
        QMutexLocker lock(&m_mu);
        track = static_cast<rtc::Track *>(m_track);
    }
    if (!track) return;
    bool open = false;
    try { open = track->isOpen(); } catch (...) { open = false; }
    if (!open) return;

    QPixmap pm = m_screen ? m_screen->grabWindow(0) : QPixmap();
    if (pm.isNull()) return;
    // 缩放到编码尺寸 → BGRA（QImage Format_RGB32 在 x86 小端内存 = B,G,R,A，与 ffmpeg -pix_fmt bgra 匹配）
    const QImage img = pm.toImage().scaled(m_encW, m_encH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_RGB32);
    if (img.isNull() || img.sizeInBytes() <= 0) return;
    m_ffmpeg->write(reinterpret_cast<const char *>(img.bits()), img.sizeInBytes());
}

// ── ffmpeg 子进程 H264 编码器（屏幕轨）─────────────────────────────
// ffmpeg 命令与 _probe/webrtc-cycle 验证一致：rawvideo bgra → libx264 → h264(Annex B) stdout。
// zerolatency + ultrafast：低延迟（编码延迟 <100ms）；keyint 控制 IDR 间隔（新订阅者快速出画）。
void LdcStreamer::startFfmpeg()
{
    if (m_ffmpeg) { delete m_ffmpeg; m_ffmpeg = nullptr; }
    m_ffmpegBuf.clear();
    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(m_encW).arg(m_encH),
        QStringLiteral("-r"), QString::number(1000.0 / qMax(40, m_captureMs), 'f', 0),
        QStringLiteral("-i"), QStringLiteral("pipe:0"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("ultrafast"),
        QStringLiteral("-tune"), QStringLiteral("zerolatency"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-x264-params"), QStringLiteral("keyint=60:min-keyint=60:scenecut=0"),
        QStringLiteral("-f"), QStringLiteral("h264"),
        QStringLiteral("pipe:1"),
    };
    // ffmpeg 定位：环境变量 > PATH（camera 用 ffmpegBin()，这里兜一层）
    QString bin = qEnvironmentVariable("STELARITH_AGENT_FFMPEG");
    if (bin.isEmpty()) bin = qEnvironmentVariable("STE_QT_FFMPEG");
    if (bin.isEmpty()) bin = QStringLiteral("ffmpeg");
    connect(m_ffmpeg, &QProcess::readyReadStandardOutput, this, &LdcStreamer::onFfmpegStdout);
    m_ffmpeg->start(bin, args);
    if (!m_ffmpeg->waitForStarted(5000)) {
        qWarning("[rtc-ldc] FAIL ffmpeg 编码器启动失败（%s 不在/不可用），屏幕轨将无 H264",
                 qPrintable(bin));
        m_ffmpeg->deleteLater();
        m_ffmpeg = nullptr;
        return;
    }
    qInfo("[rtc-ldc] ffmpeg H264 编码器已启动（%dx%d，libx264）", m_encW, m_encH);
}

void LdcStreamer::stopFfmpeg()
{
    if (!m_ffmpeg) return;
    QProcess *p = m_ffmpeg;
    m_ffmpeg = nullptr;
    m_ffmpegBuf.clear();
    try {
        if (p->state() == QProcess::Running) { p->closeWriteChannel(); p->terminate(); }
        if (!p->waitForFinished(1500)) p->kill();
    } catch (...) {}
    p->deleteLater();
}

// 从 H264 流缓冲提取一个完整视频帧（SPS/PPS/SEI 前缀 + 一个 VCL NAL）；无完整帧返回空。
// 只消费「有下一个 start code 保证完整」的 NAL；尾段不完整的留缓冲等 ffmpeg 补齐。
namespace { // anonymous：本 TU 的帧切分工具
inline int h264NalType(const QByteArray &acc, int start, int end)
{
    int k = start;
    while (k + 3 < end && (uchar)acc[k] == 0 && (uchar)acc[k+1] == 0 && (uchar)acc[k+2] == 0 && (uchar)acc[k+3] == 1) k += 4;
    while (k + 2 < end && (uchar)acc[k] == 0 && (uchar)acc[k+1] == 0 && (uchar)acc[k+2] == 1) k += 3;
    while (k < end && (uchar)acc[k] == 0) ++k;
    return (k < end) ? (((uchar)acc[k]) & 0x1F) : -1;
}
QByteArray takeNextH264Frame(QByteArray &acc)
{
    const int n = acc.size();
    if (n < 8) return {};
    // 收集 start codes（Annex B：00 00 01 / 00 00 00 01），末尾加哨兵 n
    std::vector<int> sc;
    int i = 0;
    while (i + 2 < n) {
        if (i + 3 < n && (uchar)acc[i] == 0 && (uchar)acc[i+1] == 0 && (uchar)acc[i+2] == 0 && (uchar)acc[i+3] == 1) { sc.push_back(i); i += 4; }
        else if ((uchar)acc[i] == 0 && (uchar)acc[i+1] == 0 && (uchar)acc[i+2] == 1) { sc.push_back(i); i += 3; }
        else ++i;
    }
    if (sc.empty()) return {};
    sc.push_back(n);
    for (size_t k = 0; k + 1 < sc.size(); ++k) {
        const int end = sc[k + 1];
        const bool nalComplete = (k + 2 < sc.size());   // 有下一个 start code = 本 NAL 数据完整
        if (!nalComplete) break;                        // 尾段不完整，留缓冲
        const int t = h264NalType(acc, sc[k], end);
        if (t >= 1 && t <= 5) {                          // VCL → 一帧（含此前 SPS/PPS/SEI 前缀，前缀已在 acc 前部）
            QByteArray frame = acc.left(end);
            acc.remove(0, end);
            return frame;
        }
        // 非 VCL（SPS/PPS/SEI/filler）→ 属于当前帧前缀，继续扫下一个 NAL
    }
    return {};
}
} // namespace

void LdcStreamer::onFfmpegStdout()
{
    if (!m_ffmpeg) { m_ffmpegBuf.clear(); return; }
    m_ffmpegBuf += m_ffmpeg->readAllStandardOutput();
    while (!m_ffmpegBuf.isEmpty()) {
        QByteArray frame = takeNextH264Frame(m_ffmpegBuf);
        if (frame.isEmpty()) break;
        sendH264Frame(frame);
    }
}

void LdcStreamer::sendH264Frame(const QByteArray &frame)
{
    rtc::Track *track = nullptr;
    {
        QMutexLocker lock(&m_mu);
        track = static_cast<rtc::Track *>(m_track);
    }
    if (!track) return;
    bool open = false;
    try { open = track->isOpen(); } catch (...) { open = false; }
    if (!open) return;
    // 更新 RTP timestamp（90kHz）：按实际帧间隔递增，接收端才能按帧重组
    if (m_rtpCfg) {
        auto *cfg = static_cast<rtc::RtpPacketizationConfig *>(m_rtpCfg.get());
        cfg->timestamp += uint32_t(90000LL * qMax(40, m_captureMs) / 1000);
    }
    try {
        track->send(reinterpret_cast<const std::byte *>(frame.constData()), size_t(frame.size()));
        ++m_seq;
        emit frameSent(m_seq);
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("track send(H264): %1").arg(QString::fromLocal8Bit(e.what())));
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
            // 2026-10-13 登记项：waitForFinished 停事件循环（推帧线程/心跳全停 3.5s）。
            // 改 QEventLoop 内等：函数语义不变（同步等到退出/超时），事件循环照转。
            QEventLoop loop;
            QTimer t;
            t.setSingleShot(true);
            QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
            QObject::connect(m_camProc, &QProcess::finished, &loop, &QEventLoop::quit);
            QObject::connect(m_camProc, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
            t.start(2000);
            loop.exec();
            if (m_camProc->state() != QProcess::NotRunning) {
                m_camProc->kill();
                QEventLoop loop2;
                QTimer t2;
                t2.setSingleShot(true);
                QObject::connect(&t2, &QTimer::timeout, &loop2, &QEventLoop::quit);
                QObject::connect(m_camProc, &QProcess::finished, &loop2, &QEventLoop::quit);
                t2.start(1500);
                loop2.exec();
            }
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