// WebRTC ldc 桥接实现（2026-10-09 · 独立编译单元）
#include "ldc_rtc_bridge.h"

#include "viewerbackend.h"   // 公有方法 sendLdcSignal/setLdcRtcState/emitLdcFrame
#include "ldc_receiver.h"

#include <QJsonObject>
#include <QJsonDocument>

void ldcInitRtc(ViewerBackend *backend)
{
    if (!backend) return;
    LdcReceiver &r = LdcReceiver::instance();

    // ⚠️ 2026-10-15 修（真机联调实证）：Qt 6 的 `Qt::UniqueConnection` **只支持成员函数指针**，
    //    配 lambda 会直接连接失败（日志刷 QObject::connect(LdcReceiver, ViewerBackend):
    //    unique connections require a pointer to member function of a QObject subclass）。
    //    后果链：signalAnswer 没连上 ⇒ answer 永远发不出 ⇒ 被控端等不到 answer ⇒
    //    track 不开 ⇒ 0 帧黑屏（webrtc 从未端到端建成的根因，恰好被日志实锤）。
    //    frameReady / cameraFrameReady 同样没连上 ⇒ 即使通了帧也显示不出来。
    //    ldcInitRtc 由 m_ldcInitialized 守卫（viewerbackend 只调一次），不会重复连接，
    //    去掉 UniqueConnection 安全。

    // 信令出站：ldc 的 answer/candidate → 云端（与旧路同协议 rtc-* 消息）
    QObject::connect(&r, &LdcReceiver::signalAnswer, backend,
        [backend](const QByteArray &sdp) {
            QJsonObject p;
            p.insert(QStringLiteral("payload"), QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("answer") },
                { QStringLiteral("sdp"), QString::fromUtf8(sdp) } });
            p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
            // ⚠️ 2026-10-16 修（真机联调实证）：answer 不带 uid → 云端 rtc-answer 分支
            // `const uid = p.payload.uid` 拿到空串 → rtcRelayToAgent('') →「设备不在线」
            // → answer 永远到不了被控端 → track 不开 → 0 帧。被控端 offer 是带 uid 的
            //（rtcSendSignal），viewer 回 answer 必须同样带（当前正在看的那台）。
            p.insert(QStringLiteral("uid"), backend->currentUid());
            backend->sendLdcSignal(QStringLiteral("rtc-answer"), p);
        });
    QObject::connect(&r, &LdcReceiver::signalCandidate, backend,
        [backend](const QByteArray &cand) {
            QJsonObject p;
            p.insert(QStringLiteral("payload"), QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("ice") },
                { QStringLiteral("candidate"), QString::fromUtf8(cand) } });
            p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
            p.insert(QStringLiteral("uid"), backend->currentUid());   // 同 rtc-answer：不带 uid 云端无法中继
            backend->sendLdcSignal(QStringLiteral("rtc-ice"), p);
        });
    QObject::connect(&r, &LdcReceiver::failed, backend,
        [backend](const QString &reason) {
            // ⚠️ 2026-10-13 修：原来这里是连调两次 setLdcRtcState —— 第一句是**死代码**。
            //    setRtcState 只在值变化时发信号，中间态 "failed" 立刻被 "failed: <原因>" 顶掉，
            //    于是 rtcState 这个公开读数**永远不会等于 "failed"**（任何按 `rtcState === "failed"`
            //    写的降级/提示分支恒不成立，qml/Main.qml 现在判的是 "waiting"/"track" 才没暴露），
            //    而且每次失败都会多一次 rtcStateChanged 抖动。
            //    语义态固定为 "failed"；原因只走日志（本端没有独立的 reason 通道）。
            backend->setLdcRtcState(QStringLiteral("failed"));
            logf("[viewer] RTC 失败：%s", reason.toUtf8().constData());
        });
    // 帧就绪：走既有帧显示通道（QML 画面区）
    QObject::connect(&r, &LdcReceiver::frameReady, backend,
        [backend](const QImage &img) { backend->emitLdcFrame(img); });
    // 摄像头第二轨帧（2026-10-12 · 四·双画面）：落 m_camFrame → QML 摄像头格
    QObject::connect(&r, &LdcReceiver::cameraFrameReady, backend,
        [backend](const QImage &img) { backend->emitLdcCameraFrame(img); });

    backend->setLdcRtcState(QStringLiteral("ldc"));
}

void ldcFeedSignal(ViewerBackend *backend, const QString &type, const QByteArray &payload)
{
    Q_UNUSED(backend);
    LdcReceiver &r = LdcReceiver::instance();
    if (type == QLatin1String("offer") || type == QLatin1String("candidate")) {
        r.onSignal(type, payload);
    }
}