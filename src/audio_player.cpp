// 语音对讲 · 被控端播放实现（winmm waveOut）。
#include "audio_player.h"

#include <QDebug>
#include <mmsystem.h>
#include <cstring>

#pragma comment(lib, "winmm.lib")

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
}

AudioPlayer::~AudioPlayer()
{
    closePlayer();
}

bool AudioPlayer::open()
{
    if (m_open) return true;

    WAVEFORMATEX fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 16000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (fmt.nChannels * fmt.wBitsPerSample) / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    MMRESULT r = waveOutOpen(&m_waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveOutOpen 失败，错误码 %1").arg(r);
        m_waveOut = nullptr;
        return false;
    }

    // 多缓冲轮转：每槽独立头 + 独立缓冲；open 时清零，首次 write 才 prepare。
    for (int i = 0; i < kSlots; ++i) {
        m_bufs[i].clear();
        std::memset(&m_hdrs[i], 0, sizeof(WAVEHDR));
    }
    m_nextSlot = 0;

    m_open = true;
    qInfo("[audio] 🔊 语音播放已开（16kHz 单声道 16bit，%d 槽轮转）", kSlots);
    return true;
}

void AudioPlayer::closePlayer()
{
    if (!m_open && !m_waveOut) return;
    m_open = false;
    if (m_waveOut) {
        waveOutReset(m_waveOut);   // 停掉所有在播/排队的槽
        for (int i = 0; i < kSlots; ++i) {
            if (m_hdrs[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(m_waveOut, &m_hdrs[i], sizeof(WAVEHDR));
            std::memset(&m_hdrs[i], 0, sizeof(WAVEHDR));
            m_bufs[i].clear();
        }
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
    }
    m_nextSlot = 0;
    qInfo("[audio] 🔊 语音播放已关（静音）");
}

void AudioPlayer::write(const QByteArray &pcm)
{
    if (!m_open || !m_waveOut || pcm.isEmpty()) return;

    // 从轮转起点找**空闲槽**：未 INQUEUE 即「已播完 / 从未用过」。
    // 找不到空闲槽 = 播放端比音频节奏慢 ⇒ 丢这一帧（语音实时优先，不阻塞、不积压）。
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
