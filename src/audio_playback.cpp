#include "audio_playback.h"

#include <cstring>

#ifndef Q_OS_ANDROID
// winmm（waveOut*）由这个 pragma 拉进来 —— 与 audio_capture.cpp 同配方，
// 不在 CMake 里显式 target_link_libraries（保持与既有采集件一致的最小改动）。
#pragma comment(lib, "winmm.lib")
#endif

// ── winmm waveOut 播放 16kHz/单声道/16bit PCM ─────────────────────────
// 与被控端 AudioPlayer 同族。m_buffer 保持 waveOut 正在播的缓冲存活
// （waveOutWrite 后不能释放，直到播放完成或 close 自动清理）。

AudioPlayback::AudioPlayback(QObject *parent) : QObject(parent) {}

AudioPlayback::~AudioPlayback()
{
    closeDevice();
}

#ifdef Q_OS_ANDROID
// Android（手机 App）播放：waveOut 是 Windows-only，改用 NDK 原生 AAudio 的 OUTPUT 流。
// 与采集端 audio_capture_android.cpp 同源范式（纯 C++、无 Qt Multimedia 依赖）。
//
// 对外契约（与桌面版逐字一致）：write() 吃进 16kHz/单声道/16bit 小端 PCM 帧（每帧 640B）。
// 内部：write() 来自主线程（onBinaryMessage 在 WS 所属线程调），把帧塞进环形缓冲；
// AAudio 数据回调在音频线程从环形缓冲吐样本到扬声器。环形缓冲满则丢最旧帧 ——
// 与桌面 8 槽轮转同语义（播放端慢于音频节奏 → 丢帧、不阻塞主线程、不砍正在播的帧）。
//
// ⚠️ AAudio 的 setSampleRate/setFormat/setChannelCount 只是"建议"，设备可回别的值（同采集端）。
// 故回调内做归一化：回读设备实际采样率，若 ≠16k 则把 16k 源线性重采样到设备率；
// 设备若给的就是 16k 走直通快路径（零损耗）。对端（学生端）零改动。
//
// ⚠️ 2026-10-21 前此处是 fail-silent 空桩（write() 直接丢弃），导致 Android 管理端听不到
//    学生回传音频。本实现补齐播放链路（与桌面 8 槽轮转同语义）。

#include <aaudio/AAudio.h>

#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

namespace {

AudioPlayback *g_androidPlayback = nullptr;
AAudioStream *g_androidPlayStream = nullptr;

// 16kHz/单声道/I16 源帧的环形缓冲（write 塞、回调取）。
QMutex g_playMu;
QByteArray g_playBuf;
constexpr int kMaxPlayBuf = 8 * 640;   // 8×20ms≈160ms 抖动余量，满则丢最旧帧

// 设备实际输出采样率（open 后回读；请求 16k 但设备可不照办）。
int g_outRate = 16000;
constexpr int kSrcRate = 16000;

// 重采样状态（设备 ≠16k 时才用）：把环形缓冲里的 16k I16 喂进 g_playSrc（float），
// 回调按设备率插值吐出；跨回调保持，避免边界"咔"声。
double g_playRsPos = 0.0;
QVector<float> g_playSrc;

inline int16_t readI16(const char *p)
{
    return int16_t((uint8_t)p[0] | ((uint8_t)p[1] << 8));
}
inline int16_t floatToI16(float s)
{
    if (s > 0.999969f) s = 0.999969f;
    if (s < -1.0f) s = -1.0f;
    return int16_t(s * 32767.0f);
}

// AAudio 输出回调（跑在音频线程）。audioData 是要填的 OUTPUT 缓冲（numFrames×ch×I16）。
static aaudio_data_callback_result_t aaudioPlayCb(AAudioStream *stream,
                                                 void *userData,
                                                 void *audioData,
                                                 int32_t numFrames)
{
    Q_UNUSED(userData);
    if (!g_androidPlayStream || !audioData) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    const int32_t ch = AAudioStream_getChannelCount(stream);
    const int nout = numFrames * (ch > 0 ? ch : 1);
    int16_t *out = reinterpret_cast<int16_t *>(audioData);

    // ── 设备就是 16k：直通快路径（环形缓冲直接吐 I16，零损耗、不插值）──
    if (g_outRate == kSrcRate) {
        QMutexLocker lock(&g_playMu);
        for (int i = 0; i < nout; ++i) {
            if (g_playBuf.size() >= 2) {
                out[i] = readI16(g_playBuf.constData());
                g_playBuf.remove(0, 2);
            } else {
                out[i] = 0;   // 无数据：静音填充（不爆音、不卡流）
            }
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    // ── 设备 ≠16k：16k 源 → 设备率线性重采样 ──
    {
        QMutexLocker lock(&g_playMu);
        while (g_playBuf.size() >= 2) {
            g_playSrc.append(float(readI16(g_playBuf.constData()) / 32768.0f));
            g_playBuf.remove(0, 2);
        }
    }
    const double ratio = double(kSrcRate) / double(g_outRate);  // 源/输出
    int produced = 0;
    while (produced < nout && g_playRsPos + 1.0 < double(g_playSrc.size())) {
        const int    i0   = int(g_playRsPos);
        const double frac = g_playRsPos - double(i0);
        const float  s    = g_playSrc[i0] * (1.0f - float(frac)) + g_playSrc[i0 + 1] * float(frac);
        out[produced++] = floatToI16(s);
        g_playRsPos += ratio;
    }
    const int consumed = int(g_playRsPos);
    if (consumed > 0) {
        g_playSrc.remove(0, consumed);
        g_playRsPos -= double(consumed);
    }
    for (int i = produced; i < nout; ++i) out[i] = 0;  // 源不足：静音补齐
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace

bool AudioPlayback::openDevice()
{
    if (m_open) return true;
    AAudioStreamBuilder *builder = nullptr;
    aaudio_result_t bb = AAudio_createStreamBuilder(&builder);
    if (bb != AAUDIO_OK || !builder) {
        qWarning("[audio] ⚠️ Android AAudio_createStreamBuilder 失败（%d）", int(bb));
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, 16000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setDataCallback(builder, aaudioPlayCb, this);

    aaudio_result_t ob = AAudioStreamBuilder_openStream(builder, &g_androidPlayStream);
    AAudioStreamBuilder_delete(builder);
    if (ob != AAUDIO_OK || !g_androidPlayStream) {
        qWarning("[audio] ⚠️ Android AAudio 播放 open 失败（%d）", int(ob));
        g_androidPlayStream = nullptr;
        return false;
    }

    g_outRate = AAudioStream_getSampleRate(g_androidPlayStream);
    if (g_outRate <= 0) g_outRate = 16000;

    g_androidPlayback = this;
    g_playBuf.clear();
    g_playSrc.clear();
    g_playRsPos = 0.0;

    const aaudio_result_t sb = AAudioStream_requestStart(g_androidPlayStream);
    if (sb != AAUDIO_OK) {
        qWarning("[audio] ⚠️ Android AAudio 播放 start 失败（%d）", int(sb));
        AAudioStream_close(g_androidPlayStream);
        g_androidPlayStream = nullptr;
        g_androidPlayback = nullptr;
        return false;
    }

    m_open = true;
    qInfo("[audio] 🔊 Android 扬声器已开（设备实际 %d Hz → 16kHz 源%s，20ms/帧）",
          g_outRate, g_outRate == kSrcRate ? "直通" : "重采样");
    return true;
}

void AudioPlayback::closeDevice()
{
    if (g_androidPlayStream) {
        AAudioStream_requestStop(g_androidPlayStream);
        AAudioStream_close(g_androidPlayStream);
        g_androidPlayStream = nullptr;
    }
    g_playBuf.clear();
    g_playSrc.clear();
    g_playRsPos = 0.0;
    g_androidPlayback = nullptr;
    m_open = false;
}

void AudioPlayback::write(const QByteArray &pcm)
{
    if (pcm.isEmpty()) return;
    if (!openDevice()) return;
    QMutexLocker lock(&g_playMu);
    g_playBuf.append(pcm);
    // 满则丢最旧帧（与桌面 8 槽轮转同语义：播放慢于节奏→丢帧、不阻塞、不砍正在播的帧）
    if (g_playBuf.size() > kMaxPlayBuf)
        g_playBuf.remove(0, g_playBuf.size() - kMaxPlayBuf);
}

void AudioPlayback::stop()
{
    closeDevice();
}
#else
bool AudioPlayback::openDevice()
{
    if (m_open) return true;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 16000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = 16000 * 2;

    if (waveOutOpen(&m_waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return false;
    // 多缓冲轮转：每槽独立头 + 独立缓冲；open 时清零，首次 write 才 prepare。
    // 与被控端 AudioPlayer 同族同配方（2026-10-13 验证过：单缓冲+每帧 Reset 会周期性咔咔）。
    for (int i = 0; i < kSlots; ++i) {
        m_bufs[i].clear();
        std::memset(&m_hdrs[i], 0, sizeof(WAVEHDR));
    }
    m_nextSlot = 0;
    m_open = true;
    return true;
}

void AudioPlayback::closeDevice()
{
    if (m_open && m_waveOut) {
        // 先停再清：未播完的缓冲交给 close 释放
        waveOutReset(m_waveOut);
        for (int i = 0; i < kSlots; ++i) {
            if (m_hdrs[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(m_waveOut, &m_hdrs[i], sizeof(WAVEHDR));
            std::memset(&m_hdrs[i], 0, sizeof(WAVEHDR));
            m_bufs[i].clear();
        }
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
    }
    m_open = false;
}

void AudioPlayback::write(const QByteArray &pcm)
{
    if (pcm.isEmpty()) return;
    if (!openDevice()) return;

    // 从轮转起点找**空闲槽**：未 INQUEUE 即「已播完 / 从未用过」。
    // 找不到空闲槽 = 播放端比音频节奏慢 ⇒ 丢这一帧（语音实时优先，不阻塞、不积压、不砍前一帧）。
    for (int k = 0; k < kSlots; ++k) {
        const int idx = (m_nextSlot + k) % kSlots;
        WAVEHDR &h = m_hdrs[idx];
        if (h.dwFlags & WHDR_INQUEUE) continue;   // 还在播/排队，跳过

        // 复用已播完的旧头：先解除锁定，再重设并重新 prepare。
        if (h.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(m_waveOut, &h, sizeof(WAVEHDR));

        m_bufs[idx] = pcm;                        // 深拷贝（waveOut 异步读这块内存）
        std::memset(&h, 0, sizeof(WAVEHDR));
        h.lpData = reinterpret_cast<LPSTR>(m_bufs[idx].data());
        h.dwBufferLength = static_cast<DWORD>(m_bufs[idx].size());

        if (waveOutPrepareHeader(m_waveOut, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return;
        if (waveOutWrite(m_waveOut, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(m_waveOut, &h, sizeof(WAVEHDR));
            return;
        }
        m_nextSlot = (idx + 1) % kSlots;
        return;
    }
    // 全槽在播：丢帧（不砍前一帧、不阻塞主线程）。
}

void AudioPlayback::stop()
{
    closeDevice();
}
#endif // !Q_OS_ANDROID