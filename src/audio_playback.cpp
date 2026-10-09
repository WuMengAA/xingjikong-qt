#include "audio_playback.h"

#include <cstring>

// ── winmm waveOut 播放 16kHz/单声道/16bit PCM ─────────────────────────
// 与被控端 AudioPlayer 同族。m_buffer 保持 waveOut 正在播的缓冲存活
// （waveOutWrite 后不能释放，直到播放完成或 close 自动清理）。

AudioPlayback::AudioPlayback(QObject *parent) : QObject(parent) {}

AudioPlayback::~AudioPlayback()
{
    closeDevice();
}

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
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = m_buffer.data();
    m_header.dwBufferLength = DWORD(m_buffer.size());

    if (waveOutPrepareHeader(m_waveOut, &m_header, sizeof(m_header)) == MMSYSERR_NOERROR) {
        // 上一段没播完会排队（waveOut 内部队列）；播完自动回调，我们不管细节
        waveOutWrite(m_waveOut, &m_header, sizeof(m_header));
    }
}

void AudioPlayback::stop()
{
    closeDevice();
}