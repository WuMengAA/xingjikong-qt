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

    // 单缓冲：一帧 640 字节，写入后 waveOutWrite 异步播放
    m_buffer.resize(640);
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = reinterpret_cast<LPSTR>(m_buffer.data());
    m_header.dwBufferLength = 640;

    r = waveOutPrepareHeader(m_waveOut, &m_header, sizeof(m_header));
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveOutPrepareHeader 失败，错误码 %1").arg(r);
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
        return false;
    }

    m_open = true;
    qInfo("[audio] 🔊 语音播放已开（16kHz 单声道 16bit）");
    return true;
}

void AudioPlayer::closePlayer()
{
    if (!m_open && !m_waveOut) return;
    m_open = false;
    if (m_waveOut) {
        waveOutReset(m_waveOut);
        if (m_header.lpData) waveOutUnprepareHeader(m_waveOut, &m_header, sizeof(m_header));
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
    }
    m_header = {};
    m_buffer.clear();
    qInfo("[audio] 🔊 语音播放已关（静音）");
}

void AudioPlayer::write(const QByteArray &pcm)
{
    if (!m_open || !m_waveOut || pcm.isEmpty()) return;
    // 拷贝到内部缓冲（waveOut 异步播放，不能直接指向外部可能失效的内存）
    m_buffer = pcm;
    m_header.lpData = reinterpret_cast<LPSTR>(m_buffer.data());
    m_header.dwBufferLength = static_cast<DWORD>(m_buffer.size());
    // 上一帧若还在播（waveOut 慢于 20ms），先停掉再播新的 —— 语音实时性优先，不攒缓冲
    waveOutReset(m_waveOut);
    waveOutWrite(m_waveOut, &m_header, sizeof(m_header));
}
