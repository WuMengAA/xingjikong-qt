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

LdcReceiver::LdcReceiver(QObject *parent) : QObject(parent) {}

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

        // 收流：按 track mid 分流。
        //   · "screen"（2026-10-16）：被控端已改 H264（ffmpeg libx264 + H264RtpPacketizer）——
        //     本端挂 H264RtpDepacketizer 重组，帧走 **onFrame**（实证：depacketizer 的帧
        //     输出是 onFrame 不是 onMessage），喂 ffmpeg 子进程解码 → frameReady。
        //   · "camera"（老路径）：被控端至今发手动 JPEG-over-RTP —— viewer 剥 12B RTP 头解 JPEG。
        pc->onTrack([this](std::shared_ptr<rtc::Track> track) {
            const bool isCam = (track->mid() == "camera");
            {
                QMutexLocker lock(&m_mu);
                if (isCam) m_camTrack = track.get();
                else       m_track = track.get();
            }
            if (isCam) {
                track->onMessage([this](rtc::binary data) {
                    const auto *raw = reinterpret_cast<const unsigned char *>(data.data());
                    const size_t n = data.size();
                    if (n < 12) return;   // RTP 固定头至少 12B
                    QImage img;
                    if (img.loadFromData(reinterpret_cast<const uchar *>(raw + 12), int(n - 12), "JPEG"))
                        emit cameraFrameReady(img);
                }, nullptr);
            } else {
                track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence));
                startDecoder();
                track->onFrame([this](rtc::binary frame, rtc::FrameInfo) {
                    // 帧回调在 libdatachannel 线程；QProcess::write 内部定序，跨线程安全
                    const QByteArray h(reinterpret_cast<const char *>(frame.data()), int(frame.size()));
                    feedDecoder(h);
                });
            }
        });

        pc->setRemoteDescription(rtc::Description(payload.constData(), "offer"));
        qDebug() << "[receiver] offer set, generating answer...";
        // 2026-10-16 改：**不再** addTrack(对端 offer 的 sendonly media)。
        // 实证（_probe/webrtc-cycle）：把对端 sendonly media 当本端轨 add 进去是错的
        // （方向不匹配），且接收方必须显式 addTrack **本端 recvonly 轨**——reciprocate
        // 自动建的 remote 轨不算本端轨，否则 answer 的 media 被 removed、track 永不 open。
        // 屏幕轨 H264 / 摄像头轨（camera 至今发手动 JPEG-over-RTP，viewer 侧不挂
        // depacketizer，建轨只为协商不 reject —— 保持老 onMessage 剥 JPEG 路径）。
        rtc::Description::Video scMedia("screen", rtc::Description::Direction::RecvOnly);
        scMedia.addH264Codec(96);
        pc->addTrack(scMedia);
        rtc::Description::Video camMedia("camera", rtc::Description::Direction::RecvOnly);
        camMedia.addH264Codec(97);
        pc->addTrack(camMedia);
        pc->setLocalDescription();   // 生成 answer
        qDebug() << "[receiver] setLocalDescription returned";
    } catch (const std::exception &e) {
        emit failed(QStringLiteral("offer: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}

void LdcReceiver::stop()
{
    stopDecoder();
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

// ── 屏幕轨 H264 解码：ffmpeg 子进程（h264 → rawvideo bgr24）──────────
// 2026-10-16 定案：主方案 ffmpeg 子进程（libavcodec，沙箱已验证管道出帧）；
// MF 解码作真机备选（本机沙箱 MF 编解码均不可靠，见会话记录）。
// ffmpeg 定位：viewer 安装目录 ffmpeg.exe（出包附带）> STE_VIEWER_FFMPEG > PATH。
void LdcReceiver::startDecoder()
{
    if (m_ffmpegDec) return;
    m_decBuf.clear();
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
    QString bin = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
    if (!QFileInfo::exists(bin)) {
        bin = qEnvironmentVariable("STE_VIEWER_FFMPEG");
        if (bin.isEmpty()) bin = QStringLiteral("ffmpeg");
    }
    connect(m_ffmpegDec, &QProcess::readyReadStandardOutput, this, &LdcReceiver::onDecStdout);
    m_ffmpegDec->start(bin, args);
    if (!m_ffmpegDec->waitForStarted(5000)) {
        qWarning("[receiver] FAIL ffmpeg 解码器启动失败（%s），屏幕轨将无画面", qPrintable(bin));
        m_ffmpegDec->deleteLater();
        m_ffmpegDec = nullptr;
        return;
    }
    qDebug() << "[receiver] ffmpeg H264 解码器已启动:" << bin;
}

void LdcReceiver::stopDecoder()
{
    if (!m_ffmpegDec) return;
    QProcess *p = m_ffmpegDec;
    m_ffmpegDec = nullptr;
    m_decBuf.clear();
    try {
        if (p->state() == QProcess::Running) { p->closeWriteChannel(); p->terminate(); }
        if (!p->waitForFinished(1500)) p->kill();
    } catch (...) {}
    p->deleteLater();
}

void LdcReceiver::feedDecoder(const QByteArray &h264)
{
    if (!m_ffmpegDec || m_ffmpegDec->state() != QProcess::Running) return;
    m_ffmpegDec->write(h264);
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