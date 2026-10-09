// 语音对讲 · 管理端音频采集实现（winmm waveIn）。
#include "audio_capture.h"

#include <QDebug>
#include <mmsystem.h>
#include <cstring>

#pragma comment(lib, "winmm.lib")

// waveIn 回调是全局 C 回调：用一个"当前实例"指针转发到 Qt 对象。
// 单例语义：同一时间只该有一个 AudioCapture 在采集（管理端一个麦）。
namespace {
AudioCapture *g_activeCapture = nullptr;

/** 把 waveIn 的错误码翻译成人话。
 *  起因（2026-10-09）：老师机弹「开麦失败：waveInOpen 失败，错误码 32」—— 光看数字
 *  没人分得清是"格式不支持"还是"麦克风被别的程序占用"。错误信息必须给到能干活的程度。 */
QString waveErrText(MMRESULT r)
{
    wchar_t buf[MAXERRORLENGTH] = { 0 };
    if (waveInGetErrorTextW(r, buf, MAXERRORLENGTH) == MMSYSERR_NOERROR && buf[0])
        return QString::fromWCharArray(buf).trimmed();
    return QStringLiteral("未知错误（码 %1）").arg(int(r));
}

void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR inst, DWORD_PTR, DWORD_PTR)
{
    Q_UNUSED(hwi);
    if (uMsg != WIM_DATA) return;
    AudioCapture *cap = reinterpret_cast<AudioCapture *>(inst);
    if (!cap) return;
    // WIM_DATA = 一个缓冲区采满了：交给实例处理（它会重新入队继续采）。
    cap->handleBuffer();
}
}

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
}

AudioCapture::~AudioCapture()
{
    stopCapture();
}

bool AudioCapture::startCapture()
{
    if (m_active) return true;
    if (g_activeCapture && g_activeCapture != this) {
        // 别的实例在采：先停它（管理端单麦语义）
        g_activeCapture->stopCapture();
    }

    // 没有录音设备就直接说清楚 —— 别一路走到 waveInOpen 再吐一个数字
    if (waveInGetNumDevs() == 0) {
        m_lastError = QStringLiteral("未检测到麦克风设备（系统录音设备列表为空）");
        return false;
    }

    WAVEFORMATEX fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 16000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (fmt.nChannels * fmt.wBitsPerSample) / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    MMRESULT r = waveInOpen(&m_waveIn, WAVE_MAPPER, &fmt,
                            reinterpret_cast<DWORD_PTR>(waveInProc),
                            reinterpret_cast<DWORD_PTR>(this),
                            CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveInOpen 失败：%1（16kHz/单声道/16bit）").arg(waveErrText(r));
        m_waveIn = nullptr;
        return false;
    }

    // 20ms 一帧：16000 × 2B × 0.02 = 640 字节
    m_buffer.resize(640);
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = reinterpret_cast<LPSTR>(m_buffer.data());
    m_header.dwBufferLength = 640;
    m_header.dwUser = reinterpret_cast<DWORD_PTR>(this);

    r = waveInPrepareHeader(m_waveIn, &m_header, sizeof(m_header));
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveInPrepareHeader 失败：%1").arg(waveErrText(r));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }
    r = waveInAddBuffer(m_waveIn, &m_header, sizeof(m_header));
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveInAddBuffer 失败：%1").arg(waveErrText(r));
        waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }
    r = waveInStart(m_waveIn);
    if (r != MMSYSERR_NOERROR) {
        m_lastError = QStringLiteral("waveInStart 失败：%1").arg(waveErrText(r));
        waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }

    m_active = true;
    g_activeCapture = this;
    qInfo("[audio] 🎤 麦克风已开（16kHz 单声道 16bit，20ms/帧）");
    return true;
}

void AudioCapture::stopCapture()
{
    if (!m_active && !m_waveIn) return;
    m_active = false;
    if (g_activeCapture == this) g_activeCapture = nullptr;
    if (m_waveIn) {
        waveInStop(m_waveIn);
        if (m_header.lpData) waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
    }
    m_header = {};
    m_buffer.clear();
    qInfo("[audio] 🎤 麦克风已关（静音）");
}

void AudioCapture::handleBuffer()
{
    if (!m_active || !m_waveIn) return;
    // 这一帧采满了：先吐出去，再重新入队继续采（避免停止采集时丢数据）。
    if (onFrame) onFrame(m_buffer);
    // 重新入队（单缓冲 20ms 足够，延迟小）
    waveInAddBuffer(m_waveIn, &m_header, sizeof(m_header));
}
