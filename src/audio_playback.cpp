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
// Android（手机 App）：WaveOut 是 Windows-only，此处留同签名空实现。
// 手机端播放走 Android AudioTrack 的独立实现（后续接），本桩保证链接不破、行为 fail-silent。
bool AudioPlayback::openDevice() { return false; }
void AudioPlayback::closeDevice() { m_open = false; }
void AudioPlayback::write(const QByteArray &) {}
void AudioPlayback::stop() { closeDevice(); }
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