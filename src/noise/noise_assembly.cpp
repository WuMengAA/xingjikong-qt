#include "noise_assembly.h"

#include "noise_camera_snapshot.h"
#include "noise_cloud_transport.h"
#include "noise_desktop_snapshot.h"    // DesktopSnapshot 完整类型：new 它需要（漏了会 C2061 语法错误）
#include "noise_evidence_capture.h"
#include "noise_incident_pipeline.h"
#include "noise_reporter.h"

NoiseIncidentPipeline *createNoisePipeline(QObject *parent)
{
    // 桌面 + 摄像头两个可注入抓帧源；本机无摄像头时 CameraSnapshot 会如实报"无设备"。
    auto *capture = new NoiseEvidenceCapture(new DesktopSnapshot(), new CameraSnapshot());
    auto *transport = new CloudNoiseTransport();    // 唯一知道 WS 的实现
    auto *reporter = new NoiseReporter(transport);
    return new NoiseIncidentPipeline(capture, reporter, parent);
}
