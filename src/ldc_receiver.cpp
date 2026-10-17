#include "ldc_receiver.h"

#include <QImage>
#include <QByteArray>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QFileInfo>

// libdatachannel（仅本 .cpp 引入，头文件保持零依赖）
#include "rtc/rtc.hpp"

LdcReceiver &LdcReceiver::instance()
{
    static LdcReceiver inst;
    return inst;
}

LdcReceiver::LdcReceiver(QObject *parent) : QObject(parent)
{
    // 2026-10-17：ICE/DTLS 排障开关（STE_LDC_LOG=1 开 libdatachannel Debug 日志，
    // 真机看候选类型/打洞状态；平时不开免得刷爆日志）
    if (qgetenv("STE_LDC_LOG") == "1")
        rtc::InitLogger(rtc::LogLevel::Debug);
}

// ── 信令入口：被控端 offer / candidate 经云端转来 ──
void LdcReceiver::onSignal(const QString &type, const QByteArray &payload)
{
    if (type == QLatin1String("offer")) {
        // 已有 PC：判断它是否还活着。⚠️ 2026-10-16 修（真机实证）：老代码此时直接
        // setRemoteDescription(新 offer) —— 被控端因**另一条 viewer 订阅**renegotiate
        // 出的 offer 带新 ICE 参数（ice restart），叠到正在用的 PC 上抛
        // 「renegotiate offer: Invalid ICE settings from remote SDP」→ PeerConnection Failed
        // → 整条连接被搞挂。日志实锤：0.6.35 下 rtc-answer/ice ok=true 后仍 Failed。
        if (m_pc) {
            auto *pc = static_cast<rtc::PeerConnection *>(m_pc.get());
            try {
                const auto cs = pc->state();
                if (cs == rtc::PeerConnection::State::Failed || cs == rtc::PeerConnection::State::Closed) {
                    // PC 已判死：新 offer 值得用来重建（别复用半死 PC）。
                    const QByteArray sdp = payload;
                    QTimer::singleShot(0, this, [this, sdp]() { createPc(sdp); });
                    return;
                }
                // PC 还活着（连接中或已连）：这是重复 offer，我们这条已建立/建立中，
                // 忽略——不动它，让现有 ICE 继续。
                qInfo() << "[viewer-ldc] 已有活动 PC，忽略重复 offer（连接已建立）";
                return;
            } catch (const std::exception &e) {
                // 状态查询极端情况抛了 → 兜底重建
                qWarning() << "[viewer-ldc] 查询 PC 状态异常，重建:" << e.what();
                const QByteArray sdp = payload;
                QTimer::singleShot(0, this, [this, sdp]() { createPc(sdp); });
                return;
            }
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
        // ⚠️ ICE 打洞必须配 STUN：教室机（被控端）与老师机（viewer）跨网，只有 host
        // candidate 打不通 → PeerConnection Failed（2026-10-17 真机日志实证）。
        // 与 agent 侧同一 STUN（腾讯，国内可达），可 STE_LDC_STUN 覆盖。
        rtc::Configuration cfg;
        {
            const QByteArray envStun = qgetenv("STE_LDC_STUN");
            const QByteArray stun = envStun.isEmpty()
                ? QByteArray("stun:stun.miwifi.com:3478")   // 本机实测可达（腾讯/Google STUN UDP 被墙）
                : envStun;
            cfg.iceServers.push_back(rtc::IceServer(stun.constData()));
        }
        // ⚠️ 2026-10-17 真机 Failed 修复（与沙箱 PASS 路径对齐）：必须关闭自动协商。
        //    否则 setRemoteDescription 在 HaveRemoteOffer 后**内部自动**
        //    setLocalDescription(Answer)（公共层 :280-283），此时本端轨还没 addTrack，
        //    populate 生成的是「无本地 track」的 answer → 协议状态与沙箱 PASS 不一致。
        //    工具（ws_answer_h264.cpp）正是 disableAutoNegotiation=true + addTrack 提前
        //    之后才 PASS 的；viewer 此前缺这一对 → 真机 PeerConnection Failed。
        cfg.disableAutoNegotiation = true;
        auto pc = std::make_shared<rtc::PeerConnection>(cfg);
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

        // 收流：按 track mid 分流。
        //   · "screen"（2026-10-17）：数据接收在 **addTrack(recvonly) 返回的本端轨**上
        //     （makeAnswer 里挂 H264RtpDepacketizer + onFrame，沙箱实证）。
        //     onTrack 回调收到的是 reciprocate 的 remote track —— 只记录指针，不挂 handler
        //     （挂在它上面收不到重组帧，0 帧；2026-10-17 ws_answer_h264.cpp 实证）。
        //   · "camera"（2026-10-17 摄像头 H264 化）：agent 现在发 H264，接收同 screen ——
        //     数据走 makeAnswer 里 addTrack 的本端轨（camTrk 挂 H264RtpDepacketizer）。
        //     onTrack 只记录指针（用于能力判断），不再剥 12B RTP 头解 JPEG（旧路径删除）。
        pc->onTrack([this](std::shared_ptr<rtc::Track> track) {
            const bool isCam = (track->mid() == "camera");
            {
                QMutexLocker lock(&m_mu);
                if (isCam) m_camTrack = track.get();
                else       m_track = track.get();
            }
            Q_UNUSED(isCam);   // 旧 JPEG 接收路径已删（2026-10-17），接收在本端轨
            Q_UNUSED(this);
        });

        // ── ⚠️ 顺序关键（2026-10-17 沙箱实证复现）：注册顺序必须「先注册回调，再 setRemoteDescription」。──
        // setRemoteDescription 会**同步**驱动 signalingState 变成 HaveRemoteOffer 并触发回调；
        // 若把 onSignalingStateChange 写在 setRemoteDescription **之后**（老代码就是），
        // 状态转换早已发生 → 回调永不触发 → **answer 永不生成** → 被控端永远等不到 answer
        // → RTC 连不上 → 真机现象就是「H264 没画面、一直吃 WS 轮询兜底」。
        // 沙箱复现：_probe/webrtc-cycle/ws_answer_h264.cpp。同一坑导致的日志特征：
        // viewer 侧只有 "offer set, 等 HaveRemoteOffer..." 之后再无 answer 行，而被控端
        // 日志只有 "rtc-offer/rtc-ice 已投给 1 个对端"、始终**收不到 rtc-answer**。
        // 接收轨改为本端 recvonly（不再 addTrack 对端 sendonly media，实证方向不匹配）：
        //   · screen = H264（96）；camera 至今发手动 JPEG-over-RTP（97），viewer 不挂
        //     depacketizer、建轨只为协商不 reject（保持老 onMessage 剥 JPEG 路径）。
        // ⚠️ 2026-10-17 真机 Failed 修复：addTrack **必须提前到 setRemoteDescription 之前**，
        //    与沙箱 PASS 的工具（ws_answer_h264.cpp）逐字节同构。
        //    旧代码把 addTrack 放在 makeAnswer 回调里（HaveRemoteOffer 触发后才 addTrack），
        //    setRemoteDescription 内部 processRemoteDescription 已先为 mid 建了 remote incoming
        //    track（weak_ptr、无人持住）→ populate 时本端轨未就位 → 协商出的 answer 与 PASS
        //    形态不一致 → 真机 PeerConnection Failed。setRemoteDescription 本身会同步推进
        //    signaling state 触发回调，但 addTrack 必须是**它之前**的已定事实，
        //    使 processRemoteDescription 复用本端轨而非另建（peerconnection.cpp:1137 continue）。
        // ── 本端两条 recvonly 轨（sprite + camera），addTrack 返回值成员持住 ──
        {
            rtc::Description::Video scMedia("screen", rtc::Description::Direction::RecvOnly);
            scMedia.addH264Codec(96);
            auto scTrk = pc->addTrack(scMedia);
            // 持住：mTracks 存 weak_ptr，局部 shared_ptr 析构 → track 销毁 → answer m=video 0
            m_scTrackHold = scTrk;
            // 接收轨必须在 addTrack 本端轨上挂 depacketizer（onTrack 的 remote track 收不到帧）
            scTrk->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence));
            startDecoder();
            scTrk->onFrame([this](rtc::binary frame, rtc::FrameInfo) {
                const QByteArray h(reinterpret_cast<const char *>(frame.data()), int(frame.size()));
                feedDecoder(h);
            });
            rtc::Description::Video camMedia("camera", rtc::Description::Direction::RecvOnly);
            camMedia.addH264Codec(97);
            auto camTrk = pc->addTrack(camMedia);
            m_camTrackHold = camTrk;
            camTrk->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence));
            startCamDecoder();
            camTrk->onFrame([this](rtc::binary frame, rtc::FrameInfo) {
                const QByteArray h(reinterpret_cast<const char *>(frame.data()), int(frame.size()));
                feedCamDecoder(h);
            });
        }
        // makeAnswer 现在只负责 setLocalDescription（answer 本体）——与 PASS 工具同构
        auto answered = std::make_shared<bool>(false);
        auto makeAnswer = [this, pc, answered]() {
            if (*answered) return;   // 回调与下面的兜底检查可能都命中，只生成一次 answer
            *answered = true;
            try {
                pc->setLocalDescription();   // 本端轨已提前 addTrack，这里只产出 answer
                qDebug() << "[receiver] HaveRemoteOffer → setLocalDescription（answer）";
            } catch (const std::exception &e) {
                emit failed(QStringLiteral("answer 生成: %1").arg(QString::fromLocal8Bit(e.what())));
            }
        };
        pc->onSignalingStateChange([makeAnswer](rtc::PeerConnection::SignalingState st) {
            if (st == rtc::PeerConnection::SignalingState::HaveRemoteOffer) makeAnswer();
        });
        pc->setRemoteDescription(rtc::Description(payload.constData(), "offer"));
        // 兜底：某些路径下状态在 setRemoteDescription 内就已落地，上面的回调可能来不及；
        // 这里补一次同步检查，保证 answer 一定会生成。
        if (pc->signalingState() == rtc::PeerConnection::SignalingState::HaveRemoteOffer) makeAnswer();
        qDebug() << "[receiver] offer set，answer 生成路径已就绪（回调已先注册）";
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("offer: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}

void LdcReceiver::stop()
{
    stopDecoder();
    stopCamDecoder();   // 2026-10-17：摄像头独立解码子进程一并清理
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
        m_scTrackHold.reset();    // 2026-10-17：释放 track 唯一持有者（PC 已 close）
        m_camTrackHold.reset();
        m_track = nullptr;
        m_camTrack = nullptr;
    }
    emit stateChanged(false);
}

// ── 屏幕轨 H264 解码：ffmpeg 子进程（h264 → rawvideo bgr24）──────────
// 2026-10-16 定案：主方案 ffmpeg 子进程（libavcodec，沙箱已验证管道出帧）；
// MF 解码作真机备选（本机沙箱 MF 编解码均不可靠，见会话记录）。
// ffmpeg 定位：viewer 安装目录 ffmpeg.exe（出包附带）> STE_VIEWER_FFMPEG > PATH。
void LdcReceiver::startDecoder()
{
    if (m_ffmpegDec || m_useMf) return;
    m_decBuf.clear();
    // ① 主方案：ffmpeg 子进程（沙箱已验证）。ffmpeg 212MB 全量版不打进安装包；
    //    安装目录 / STE_VIEWER_FFMPEG / PATH 有就用（老师机可自行装）。
    QString bin = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
    if (!QFileInfo::exists(bin)) {
        bin = qEnvironmentVariable("STE_VIEWER_FFMPEG");
        if (bin.isEmpty()) bin = QStringLiteral("ffmpeg");   // PATH
    }
    m_ffmpegDec = new QProcess(this);
    m_ffmpegDec->setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("h264"), QStringLiteral("-i"), QStringLiteral("pipe:0"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("bgr24"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(m_decW).arg(m_decH),
        QStringLiteral("-r"), QStringLiteral("10"),
        QStringLiteral("pipe:1"),
    };
    connect(m_ffmpegDec, &QProcess::readyReadStandardOutput, this, &LdcReceiver::onDecStdout);
    m_ffmpegDec->start(bin, args);
    if (!m_ffmpegDec->waitForStarted(5000)) {
        // ② 备选：MF 兜底（零依赖）。真机验证（沙箱 MF 不可靠，见 mfx_h264_decode.h）
        qWarning("[receiver] ffmpeg 解码器不可用（%s）→ 走 MF 兜底", qPrintable(bin));
        m_ffmpegDec->deleteLater();
        m_ffmpegDec = nullptr;
        m_useMf = m_mfDec.init(m_decW, m_decH);
        if (!m_useMf) { qWarning("[receiver] MF 兜底也失败，屏幕轨将无 H264 画面"); }
        return;
    }
    qDebug() << "[receiver] ffmpeg H264 解码器已启动:" << bin;
}

void LdcReceiver::stopDecoder()
{
    if (m_ffmpegDec) {
        QProcess *p = m_ffmpegDec;
        m_ffmpegDec = nullptr;
        m_decBuf.clear();
        try {
            if (p->state() == QProcess::Running) { p->closeWriteChannel(); p->terminate(); }
            if (!p->waitForFinished(1500)) p->kill();
        } catch (...) {}
        p->deleteLater();
    }
    m_mfDec.shutdown();
    m_useMf = false;
}

void LdcReceiver::feedDecoder(const QByteArray &h264)
{
    if (m_ffmpegDec && m_ffmpegDec->state() == QProcess::Running) {
        m_ffmpegDec->write(h264);
        return;
    }
    if (m_useMf) {
        QImage img;
        if (m_mfDec.decodeFrame(h264, img)) emit frameReady(img);
    }
}

void LdcReceiver::onDecStdout()
{
    if (!m_ffmpegDec) { m_decBuf.clear(); return; }
    m_decBuf += m_ffmpegDec->readAllStandardOutput();
    const int frameBytes = m_decW * m_decH * 3;   // bgr24
    while (m_decBuf.size() >= frameBytes) {
        const QByteArray frame = m_decBuf.left(frameBytes);
        m_decBuf.remove(0, frameBytes);
        // ffmpeg bgr24 内存序 = B,G,R → Qt Format_BGR888 直接吃；copy 保底层数据生命周期
        const QImage img(reinterpret_cast<const uchar *>(frame.constData()), m_decW, m_decH,
                         m_decW * 3, QImage::Format_BGR888);
        emit frameReady(img.copy());
    }
}

// ── 摄像头轨 H264 解码（2026-10-17 · 摄像头 H264 化）─────────────────
// 独立 ffmpeg 子进程：与屏幕轨分路（两路 H264 交错写同一 stdin 会花屏）。
// 输出统一缩放到 320x240（scale 滤镜）：真机 dshow 源尺寸不定，scale 保证与
// 屏幕轨同管线尺寸（m_decW/decH）。ffmpeg 定位同屏幕轨（安装目录 > STE_VIEWER_FFMPEG > PATH）。
void LdcReceiver::startCamDecoder()
{
    if (m_camFfmpegDec) return;
    m_camDecBuf.clear();
    QString bin = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
    if (!QFileInfo::exists(bin)) {
        bin = qEnvironmentVariable("STE_VIEWER_FFMPEG");
        if (bin.isEmpty()) bin = QStringLiteral("ffmpeg");
    }
    m_camFfmpegDec = new QProcess(this);
    m_camFfmpegDec->setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("h264"), QStringLiteral("-i"), QStringLiteral("pipe:0"),
        QStringLiteral("-vf"), QStringLiteral("scale=%1:%2").arg(m_decW).arg(m_decH),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("bgr24"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(m_decW).arg(m_decH),
        QStringLiteral("-r"), QStringLiteral("10"),
        QStringLiteral("pipe:1"),
    };
    connect(m_camFfmpegDec, &QProcess::readyReadStandardOutput, this, &LdcReceiver::onCamDecStdout);
    m_camFfmpegDec->start(bin, args);
    if (!m_camFfmpegDec->waitForStarted(5000)) {
        qWarning("[receiver] camera ffmpeg 解码器不可用（%s）→ 摄像头轨无 H264 画面", qPrintable(bin));
        m_camFfmpegDec->deleteLater();
        m_camFfmpegDec = nullptr;
        return;
    }
    qDebug() << "[receiver] camera ffmpeg H264 解码器已启动:" << bin;
}

void LdcReceiver::stopCamDecoder()
{
    if (m_camFfmpegDec) {
        QProcess *p = m_camFfmpegDec;
        m_camFfmpegDec = nullptr;
        m_camDecBuf.clear();
        try {
            if (p->state() == QProcess::Running) { p->closeWriteChannel(); p->terminate(); }
            if (!p->waitForFinished(1500)) p->kill();
        } catch (...) {}
        p->deleteLater();
    }
}

void LdcReceiver::feedCamDecoder(const QByteArray &h264)
{
    if (m_camFfmpegDec && m_camFfmpegDec->state() == QProcess::Running)
        m_camFfmpegDec->write(h264);
}

void LdcReceiver::onCamDecStdout()
{
    if (!m_camFfmpegDec) { m_camDecBuf.clear(); return; }
    m_camDecBuf += m_camFfmpegDec->readAllStandardOutput();
    const int frameBytes = m_decW * m_decH * 3;   // bgr24
    while (m_camDecBuf.size() >= frameBytes) {
        const QByteArray frame = m_camDecBuf.left(frameBytes);
        m_camDecBuf.remove(0, frameBytes);
        const QImage img(reinterpret_cast<const uchar *>(frame.constData()), m_decW, m_decH,
                         m_decW * 3, QImage::Format_BGR888);
        emit cameraFrameReady(img.copy());
    }
}