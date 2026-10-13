// 星集控 · ViewerBackend RTC 收流方法域（2026-10-13 架构拆分，审计 2.1）
//
// 为什么拆出来：viewerbackend.cpp 曾 3573 行（C1060）。RTC 收流域（setRtcState /
// initRtcView / rtcDiag / rtcGotAnswer / rtcGotIce / addRemoteIce / deliverOffer /
// resetRtcState / cancelRtcViewReap / stopRtcTimers / scheduleRtcViewReap /
// rtcGotTrack / setRtcFrame）是弱状态、边界清楚的一块：只管收流状态机与信令
// 转发。拆到独立翻译单元，行为不变（同 header 的 ViewerBackend 成员，QObject
// 跨编译单元实现合法）。
//
// ⚠️ 现状：ldc（libdatachannel）是唯一 RTC 路（initRtcView 直接 ldcInitRtc，
// STE_USE_LDC_RTC=0 也只是报 failed 不回退 WebEngine）。这些方法中 addRemoteIce/
// deliverOffer 已是 WebEngine 时代的空壳（保留签名供 QML/旧路径引用）。
// 析构等生命周期方法仍在主文件；本域只负责"协商 + 状态 + 延迟回收"。

#include "viewerbackend.h"
#include "ldc_rtc_bridge.h"   // ldcInitRtc

#include <QDateTime>
#include <QTimer>

void ViewerBackend::setRtcState(const QString &s)
{
    if (m_rtcState == s) return;
    m_rtcState = s;
    m_rtcReady = (s == QStringLiteral("track"));   // 拿到远端视频轨才算真出画面
    emit rtcStateChanged();
}
void ViewerBackend::initRtcView()
{
    // ── 2026-10-09 换 other webrtc：优先 libdatachannel（ldc_receiver 收流），
    // 不拉起 Chromium（瘦身 137MB 常驻）。STE_USE_LDC_RTC=0 回退旧 WebEngine 路。
    // 接线在独立编译单元 ldc_rtc_bridge.cpp（大文件不再膨胀，C1060 已解决）。──
    if (qgetenv("STE_USE_LDC_RTC") != "0") {
        if (m_ldcInitialized) return;
        m_ldcInitialized = true;
        cancelRtcViewReap();
        ldcInitRtc(this);
        logf("[viewer-ldc] 收流初始化（libdatachannel，无 Chromium）");
        return;
    }
    // ── 2026-10-10 去 WebEngine 底层：WebEngine 收流分支已移除，ldc 是唯一路 ──
    // STE_USE_LDC_RTC=0 不再回退 WebEngine（Qt 6.12 不装 WebEngineWidgets，
    // 且 "去 WebEngine 底层" 本就是目标）。走到这里说明被显式关闭了 ldc。
    logf("[viewer] STE_USE_LDC_RTC=0 但 WebEngine 已移除——RTC 收流不可用，用 JPEG 兜底");
    setRtcState(QStringLiteral("failed"));
}
/** 诊断/日志（JS → C++）。 */
void ViewerBackend::rtcDiag(const QString &s)
{
    logf("[viewer] RTC %s", s.toUtf8().constData());
    if (m_rtcState == QStringLiteral("waiting") &&
        !s.contains(QStringLiteral("REJECT")) && !s.contains(QStringLiteral("FAIL"))) {
        // 进入协商期
        setRtcState(QStringLiteral("negotiating"));
    }
}
/**
 * JS 侧 createAnswer 产出的 answer → **回云端给被控端**。
 *
 * 这里原本只有一行 "收到意外的 rtc-answer（管理端不产出 answer）" —— 那是照着"管理端是答案产出方"
 * 倒推的错判：真正的分工是**被控端 offer 侧、管理端 answer 侧**，被控端在等我们这份 answer 才能
 * 走完 setRemoteDescription → 触发 ontrack。answer 不回云端 = 协商永远停在 have-local-offer，
 * 被控端那边静默重连、管理端这边一眼看不出问题（只有"一直黑屏"）。
 */
void ViewerBackend::rtcGotAnswer(const QString &sdp)
{
    if (sdp.isEmpty()) {
        logf("[viewer] FAIL RTC answer 是空的（createAnswer 没产出 sdp），不回云端");
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("kind"), QStringLiteral("answer"));
    p.insert(QStringLiteral("sdp"), sdp);
    p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
    sendEnvelope(QStringLiteral("rtc-answer"), p);
    logf("[viewer] RTC answer 已回云端（sdp %d 字符 → %s）", sdp.size(),
         m_currentUid.toUtf8().constData());
}
/**
 * JS 侧**本端** ICE candidate 过来：回云端给被控端。
 *
 * 少发这一半是致命的：被控端只收到自己的候选，两端候选对不上，ICE 永远完不成，
 * ontrack 不来 → 画面全黑，而两端日志看起来都挺正常（offer 收了、answer 回了）。
 *
 * ⚠️ 这里**只发云端，不回灌自己的 pc**。以前这两件事挤在同一个函数里，于是：
 *   - 本端候选被 addIceCandidate 加给自己 → Chromium 静默 ice-FAIL；
 *   - 云端来的远端候选也被同一个函数处理 → 被原样 sendEnvelope 回发给被控端，
 *     被控端收到自己刚发出去的候选，形成回环放大。
 * 两个方向现在彻底分开：本端走这里，远端走 addRemoteIce()。
 */
void ViewerBackend::rtcGotIce(const QString &candJson)
{
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("kind"), QStringLiteral("ice"));
    p.insert(QStringLiteral("candidate"), candJson);
    p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
    sendEnvelope(QStringLiteral("rtc-ice"), p);
}
void ViewerBackend::addRemoteIce(const QString &)
{
    // ── 2026-10-10 去 WebEngine：远端 ICE 由 ldc 路接管（ldcFeedSignal "candidate"），
    // 这个 QWebChannel 回调用不到了。保留签名（QML 可能还引用）但内部为空。──
}
void ViewerBackend::deliverOffer(const QString &)
{
    // ── 2026-10-10 去 WebEngine：远端 offer 由 ldc 路接管（ldcFeedSignal "offer"）──
}
/**
 * 2026-10-13 审计 2.10 改名：WebEngine 已移除，此函数不再回收任何 Chromium 页，
 * 只把 RTC 状态清回 idle（保留调用路径安全收尾）。原名 releaseRtcView() 是
 * WebEngine 时代残留，空壳名字会误导后来人以为还在做资源清理。
 */
void ViewerBackend::resetRtcState()
{
    setRtcState(QStringLiteral("idle"));
}
void ViewerBackend::cancelRtcViewReap()
{
    if (m_rtcReap && m_rtcReap->isActive()) m_rtcReap->stop();
}
void ViewerBackend::stopRtcTimers()
{
    if (m_grabTimer) m_grabTimer->stop();
    if (m_rtcDiagTimer) m_rtcDiagTimer->stop();
}
// 延迟回收：给"刚断又马上重连"留窗口，避免断线抖动时反复重建 Chromium（重建一次几百毫秒）。
// 真正等多久由 STE_RTC_IDLE_MS（默认 20 秒）决定，从"跟云端断开"那一刻开始算。
void ViewerBackend::scheduleRtcViewReap()
{
    if (!m_rtcReap) {
        m_rtcReap = new QTimer(this);
        m_rtcReap->setSingleShot(true);
        m_rtcReap->setInterval(5000);
        QObject::connect(m_rtcReap, &QTimer::timeout, this, [this]() { resetRtcState(); });
    }
    m_rtcReap->stop();
    if (!m_offlineSince) return;          // 还在线上，没什么可收的
    // 空 = 默认 20 秒；显式写 0 = 关掉（别让默认值把开关吃掉）
    const QByteArray envIdle = qgetenv("STE_RTC_IDLE_MS");
    const qint64 limit = envIdle.isEmpty() ? 20000 : QString::fromLocal8Bit(envIdle).toLongLong();
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_offlineSince;
    if (limit <= 0) return;
    if (elapsed >= limit) { resetRtcState(); return; }   // 早就算超了，立刻收
    m_rtcReap->setInterval((int)(limit - elapsed));
    m_rtcReap->start();
}
/** JS 侧 ontrack 触发：真正拿到远端视频轨。 */
void ViewerBackend::rtcGotTrack()
{
    logf("[viewer] ✅ RTC ontrack：拿到远端视频轨");
    setRtcState(QStringLiteral("track"));
}
/**
 * JS → C++：抽到一帧 JPEG（base64，可能带 data: 前缀）。
 * 复用现有 frame 通道（走 applyFrameBytes → frameChanged），所以 QML 那边一行都不用改。
 */
void ViewerBackend::setRtcFrame(const QString &b64)
{
    if (b64 == QStringLiteral("EMPTY") || b64.isEmpty()) return;
    const QByteArray raw = QByteArray::fromBase64(
        b64.startsWith(QStringLiteral("data:")) ? b64.mid(b64.indexOf(QLatin1Char(',') ) + 1).toUtf8()
                                                 : b64.toUtf8());
    if (raw.isEmpty()) return;
    applyFrameBytes(raw, QJsonObject(), RtcGrab);
}