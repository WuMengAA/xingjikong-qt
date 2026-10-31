#include "noise_assembly.h"

#include "noise_announcer.h"
#include "noise_camera_snapshot.h"
#include "noise_cloud_transport.h"
#include "noise_desktop_snapshot.h"    // DesktopSnapshot 完整类型：new 它需要（漏了会 C2061 语法错误）
#include "noise_evidence_capture.h"
#include "noise_incident_pipeline.h"
#include "noise_reporter.h"

#include "../tts/tts_speaker.h"        // speakText（提醒环：教室本地语音）

// 云端事件（main.cpp 文件作用域外链接；提醒环只发"噪音超阈"一件事）
void sendEvent(const QString &level, const QString &msg, const QJsonObject &extra);

// 全局提醒器（thresholdReached → 语音 + 事件）；由 createNoisePipeline 顺带创建。
static NoiseAnnouncer *g_noiseAnnouncer = nullptr;

NoiseAnnouncer *getNoiseAnnouncer()
{
    return g_noiseAnnouncer;
}

NoiseAnnouncer *createNoiseAnnouncer(QObject *parent)
{
    auto *a = new NoiseAnnouncer(parent);
    g_noiseAnnouncer = a;
    // 装配：提醒动作绑定到具体实现（TTS 播报 + 云端事件）。判定的接线在 main 的
    // ensureNoise()（g_noise 是上课懒建的，只有那时才有 monitor 可连）。
    QObject::connect(a, &NoiseAnnouncer::voiceRequested, a, [](const QString &t) { speakText(t); });
    QObject::connect(a, &NoiseAnnouncer::eventRequested, a,
        [](const QString &lvl, const QString &msg, const QJsonObject &extra) { sendEvent(lvl, msg, extra); });
    return a;
}

NoiseIncidentPipeline *createNoisePipeline(QObject *parent)
{
    // 桌面 + 摄像头两个可注入抓帧源；本机无摄像头时 CameraSnapshot 会如实报"无设备"。
    auto *capture = new NoiseEvidenceCapture(new DesktopSnapshot(), new CameraSnapshot());
    auto *transport = new CloudNoiseTransport();    // 唯一知道 WS 的实现
    auto *reporter = new NoiseReporter(transport);
    auto *pipeline = new NoiseIncidentPipeline(capture, reporter, parent);

    // 提醒环：补上 P0-1 缺的一环（thresholdReached 无人接）。信号源在 main 的
    // ensureNoise() 里上课时才创建，接线放在那里；这里只保证 announcer 全局可达。
    createNoiseAnnouncer(parent);

    return pipeline;
}