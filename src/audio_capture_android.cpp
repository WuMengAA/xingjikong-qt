// 语音对讲 · 管理端音频采集 —— Android 实现（2026-10-11 从 stub 升级 AAudio）
//
// 桌面版用 winmm waveIn（audio_capture.cpp）；Android 没有 winmm，而
// Qt Multimedia 模块两端 kit 都未安装（QAudioSource 不可用），故用 NDK 原生
// AAudio（Android 8.0+/API 26+，纯 C++，无 Qt 模块依赖）。
//
// 采集 16kHz 单声道 16bit PCM，每 20ms 一帧（640 字节，与桌面帧格式一致）
// 通过 WebSocket 二进制发出（ViewerBackend 注入 onFrame）。
//
// ⚠️ 权限：AndroidManifest 已声明 RECORD_AUDIO；运行期权限在
// ViewerBackend::startSpeaking 里用 Qt QMicrophonePermission 请求，授权后才
// 调 startCapture。AAudio open 时权限未授予会返回 ERROR（报失败并给出人话）。

#include "audio_capture.h"

#include <aaudio/AAudio.h>

#include <QByteArray>
#include <QDebug>

namespace {
AudioCapture *g_androidCapture = nullptr;
AAudioStream *g_androidStream = nullptr;
QByteArray g_androidBuf;   // 攒帧：回调给的块可能不是 640B 整数倍，跨回调补齐
}

// AAudio 数据回调（跑在音频线程，与桌面 waveIn 回调线程同级）。
// audioData 是填好的一帧数据（numFrames 帧，格式见 stream）；攒满 640B
// （20ms@16k/16bit/单声道）就吐一帧给 onFrame（行为与桌面一致）。
static aaudio_data_callback_result_t aaudioDataCb(AAudioStream *stream,
                                                  void *userData,
                                                  void *audioData,
                                                  int32_t numFrames)
{
    AudioCapture *cap = reinterpret_cast<AudioCapture *>(userData);
    if (!cap || !g_androidStream) return AAUDIO_CALLBACK_RESULT_CONTINUE;

    const int32_t samplesPerFrame = AAudioStream_getSamplesPerFrame(stream);
    int bytesPerSample = 2;   // PCM_I16 是我们请求的格式
    const aaudio_format_t fmt = AAudioStream_getFormat(stream);
    if (fmt == AAUDIO_FORMAT_PCM_FLOAT) bytesPerSample = 4;
    const int32_t bytesPerFrame = samplesPerFrame * bytesPerSample;
    const int32_t totalBytes = numFrames * bytesPerFrame;
    if (totalBytes <= 0 || !audioData) return AAUDIO_CALLBACK_RESULT_CONTINUE;

    g_androidBuf.append(reinterpret_cast<const char *>(audioData), totalBytes);
    while (g_androidBuf.size() >= 640) {
        const QByteArray pcm = g_androidBuf.left(640);
        g_androidBuf.remove(0, 640);
        if (cap->onFrame) cap->onFrame(pcm);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture()
{
    stopCapture();
    if (g_androidCapture == this) g_androidCapture = nullptr;
}

bool AudioCapture::startCapture()
{
    if (m_active) return true;
    if (g_androidCapture && g_androidCapture != this) {
        g_androidCapture->stopCapture();   // 单麦语义：先停旧的
    }
    g_androidCapture = this;

    AAudioStreamBuilder *builder = nullptr;
    aaudio_result_t bb = AAudio_createStreamBuilder(&builder);
    if (bb != AAUDIO_OK || !builder) {
        m_lastError = QStringLiteral("AAudio_createStreamBuilder 失败（%1）").arg(int(bb));
        g_androidCapture = nullptr;
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, 16000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setDataCallback(builder, aaudioDataCb, this);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);

    aaudio_result_t ob = AAudioStreamBuilder_openStream(builder, &g_androidStream);
    AAudioStreamBuilder_delete(builder);
    if (ob != AAUDIO_OK || !g_androidStream) {
        m_lastError = QStringLiteral("AAudio open 失败（%1）——是否未授予录音权限（RECORD_AUDIO）？")
                          .arg(int(ob));
        g_androidStream = nullptr;
        g_androidCapture = nullptr;
        return false;
    }

    g_androidBuf.clear();
    const aaudio_result_t sb = AAudioStream_requestStart(g_androidStream);
    if (sb != AAUDIO_OK) {
        m_lastError = QStringLiteral("AAudio start 失败（%1）").arg(int(sb));
        AAudioStream_close(g_androidStream);
        g_androidStream = nullptr;
        g_androidCapture = nullptr;
        return false;
    }

    m_active = true;
    qInfo("[audio] 🎤 Android 麦克风已开（AAudio 16kHz 单声道 16bit，20ms/帧）");
    return true;
}

void AudioCapture::stopCapture()
{
    if (g_androidStream) {
        AAudioStream_requestStop(g_androidStream);
        AAudioStream_close(g_androidStream);
        g_androidStream = nullptr;
    }
    g_androidBuf.clear();
    m_active = false;
    g_androidCapture = nullptr;
}

void AudioCapture::handleBuffer()
{
    // Android 由 AAudio readyRead-like 数据回调驱动，无 waveIn 缓冲回调用不到。
}