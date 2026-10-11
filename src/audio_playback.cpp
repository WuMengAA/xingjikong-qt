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
    // 单缓冲：一帧 640 字节（20ms），写入后 waveOutWrite 异步播放。
    // ⚠️ Prepare 只做**一次**（与被控端 AudioPlayer 同配方）：每帧都 Prepare 而不 Unprepare
    //    会累积准备状态、重复 prepare 同一 header 返回 WAVERR_... 且缓冲被反复改指针。
    m_buffer.resize(640);
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = m_buffer.data();
    m_header.dwBufferLength = 640;
    if (waveOutPrepareHeader(m_waveOut, &m_header, sizeof(m_header)) != MMSYSERR_NOERROR) {
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
        return false;
    }
    m_open = true;
    return true;
}

void AudioPlayback::closeDevice()
{
    if (m_open && m_waveOut) {
        // 先停再清：未播完的缓冲交给 close 释放
        waveOutReset(m_waveOut);
        if (m_header.lpData) {
            waveOutUnprepareHeader(m_waveOut, &m_header, sizeof(m_header));
            m_header.lpData = nullptr;
            m_header.dwBufferLength = 0;
        }
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
    }
    m_open = false;
    m_buffer.clear();
}

void AudioPlayback::write(const QByteArray &pcm)
{
    if (pcm.isEmpty()) return;
    if (!openDevice()) return;

    // 缓存放成员：waveOutWrite 后缓冲必须存活到播放完成
    m_buffer = pcm;
    m_header.lpData = m_buffer.data();
    m_header.dwBufferLength = DWORD(m_buffer.size());
    // ⚠️ 上一帧若还在播（waveOut 慢于音频节奏），先 Reset 掉再写新的 —— 语音实时性优先，
    //    不攒缓冲（攒了就是越听越延迟）。与被控端 AudioPlayer 同一裁决。
    waveOutReset(m_waveOut);
    waveOutWrite(m_waveOut, &m_header, sizeof(m_header));
}

void AudioPlayback::stop()
{
    closeDevice();
}
#endif // !Q_OS_ANDROID