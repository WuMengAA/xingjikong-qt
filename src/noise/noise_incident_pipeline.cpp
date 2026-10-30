#include "noise_incident_pipeline.h"

#include "noise_evidence_capture.h"
#include "noise_reporter.h"

#include <QDateTime>
#include <QMetaObject>
#include <QtConcurrent>

NoiseIncidentPipeline::NoiseIncidentPipeline(NoiseEvidenceCapture *capture,
                                             NoiseReporter *reporter,
                                             QObject *parent)
    : QObject(parent), m_capture(capture), m_reporter(reporter)
{
}

void NoiseIncidentPipeline::onSample(double db, bool active, double baselineDb, double thresholdDb)
{
    const QDateTime at = QDateTime::currentDateTimeUtc();
    auto inc = m_service.onSample(db, active, at, baselineDb, thresholdDb);
    if (!inc.has_value()) return;   // 还没到 / 已报过
    scheduleCapture(*inc);
}

void NoiseIncidentPipeline::scheduleCapture(const NoiseIncident &inc)
{
    // 抓帧会阻塞（桌面 ~200ms / 摄像头最多 8s），必须离开主线程；
    // 抓完用 Queued 把结果送回本对象所在线程（主线程）再上报 —— WS 只能由主线程用。
    QtConcurrent::run([this, inc]() {
        const NoiseEvidence ev = m_capture ? m_capture->capture() : NoiseEvidence{};
        QMetaObject::invokeMethod(this, [this, inc, ev]() { onEvidenceReady(inc, ev); },
                                 Qt::QueuedConnection);
    });
}

void NoiseIncidentPipeline::onEvidenceReady(const NoiseIncident &inc, const NoiseEvidence &ev)
{
    ++m_incidents;
    if (m_reporter) m_reporter->report(inc, ev);
    qInfo().noquote() << QStringLiteral("[agent-qt] 噪音存证已上报：节次=%1 hits=%2 桌面=%3 摄像头=%4")
        .arg(inc.periodKey, QString::number(inc.hits),
             ev.desktopPath.isEmpty() ? QStringLiteral("无") : QStringLiteral("有"),
             ev.cameraPath.isEmpty() ? QStringLiteral("无") : QStringLiteral("有"));
}
