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

    // 信令出站：ldc 的 answer/candidate → 云端（与旧路同协议 rtc-* 消息）
    QObject::connect(&r, &LdcReceiver::signalAnswer, backend,
        [backend](const QByteArray &sdp) {
            QJsonObject p;
            p.insert(QStringLiteral("payload"), QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("answer") },
                { QStringLiteral("sdp"), QString::fromUtf8(sdp) } });
            p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
            backend->sendLdcSignal(QStringLiteral("rtc-answer"), p);
        }, Qt::UniqueConnection);
    QObject::connect(&r, &LdcReceiver::signalCandidate, backend,
        [backend](const QByteArray &cand) {
            QJsonObject p;
            p.insert(QStringLiteral("payload"), QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("ice") },
                { QStringLiteral("candidate"), QString::fromUtf8(cand) } });
            p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
            backend->sendLdcSignal(QStringLiteral("rtc-ice"), p);
        }, Qt::UniqueConnection);
    QObject::connect(&r, &LdcReceiver::failed, backend,
        [backend](const QString &reason) {
            backend->setLdcRtcState(QStringLiteral("failed"));
            backend->setLdcRtcState(QStringLiteral("failed:" ) + reason);
        }, Qt::UniqueConnection);
    // 帧就绪：走既有帧显示通道（QML 画面区）
    QObject::connect(&r, &LdcReceiver::frameReady, backend,
        [backend](const QImage &img) { backend->emitLdcFrame(img); },
        Qt::UniqueConnection);

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