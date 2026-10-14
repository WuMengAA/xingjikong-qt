#include "microphone.h"

#include <cstring>
#include <QElapsedTimer>
#include <QDebug>

// ── winmm waveIn 采集 16kHz/单声道/16bit PCM ──────────────────────────
// 定时 pump 拉取数据（waveInAddBuffer + waveInGetNumDevs 头部检查）。
// 简化实现：单缓冲 + 20ms tick 读取；通话场景 50 帧/s 足够。

Microphone::Microphone(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(20);
    connect(&m_tick, &QTimer::timeout, this, &Microphone::pump);
    m_watch.setSingleShot(true);
    connect(&m_watch, &QTimer::timeout, this, &Microphone::hardStop);
}

Microphone::~Microphone()
{
    stop();
}

bool Microphone::openDevice()
{
    if (m_waveIn) return true;
    // 无麦克风设备：waveInGetNumDevs()==0 → 不开，静默降级
    if (waveInGetNumDevs() == 0) return false;

    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 16000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = 16000 * 2;

    if (waveInOpen(&m_waveIn, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return false;

    // 单缓冲：攒 20ms（16000*2*0.02 = 640 字节）
    m_buffer.resize(640);
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = m_buffer.data();
    m_header.dwBufferLength = m_buffer.size();

    // 2026-10-14 修（审计 P1）：下面三个调用的返回值原来**全都不查**、无条件 return true。
    // 若 waveInStart 失败：m_waveIn 非空 → start() 返回 true → m_running=true，
    // 但采集根本没起来，pump() 每 20ms 空转一次却永远收不到一个字节
    // → 语音通话「静默无声」，而日志上一片正常，老师听到的死寂查不出任何线索。
    // 对比 noise_monitor.cpp：它同样漏查这两个返回值，但有 600ms 死等 +
    // 「样本数<=0 就返回 -1.0」兜底，所以不会静默失败；这里没有兜底，只能把检查补上。
    // 失败一律先回滚已开的资源再返回 false（回滚顺序与 closeDevice 相反）。
    const MMRESULT prep = waveInPrepareHeader(m_waveIn, &m_header, sizeof(m_header));
    if (prep != MMSYSERR_NOERROR) {
        qWarning("[mic] waveInPrepareHeader 失败（错误码 %u），放弃采集", unsigned(prep));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }
    const MMRESULT add = waveInAddBuffer(m_waveIn, &m_header, sizeof(m_header));
    if (add != MMSYSERR_NOERROR) {
        qWarning("[mic] waveInAddBuffer 失败（错误码 %u），放弃采集", unsigned(add));
        waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }
    const MMRESULT st = waveInStart(m_waveIn);
    if (st != MMSYSERR_NOERROR) {
        qWarning("[mic] waveInStart 失败（错误码 %u）——采集未启动，放弃", unsigned(st));
        waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }
    return true;
}

void Microphone::closeDevice()
{
    if (m_waveIn) {
        waveInStop(m_waveIn);
        waveInUnprepareHeader(m_waveIn, &m_header, sizeof(m_header));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
    }
    m_buffer.clear();
}

bool Microphone::start(int maxMs)
{
    if (m_running) return true;
    if (!openDevice()) return false;
    m_running = true;
    m_maxMs = maxMs > 0 ? maxMs : 120000;
    m_elapsedMs = 0;
    m_tick.start();
    m_watch.start(m_maxMs);
    return true;
}

void Microphone::stop()
{
    if (!m_running && !m_waveIn) return;
    m_running = false;
    m_tick.stop();
    m_watch.stop();
    closeDevice();
}

void Microphone::pump()
{
    if (!m_running || !m_waveIn) return;
    m_elapsedMs += 20;

    // 缓冲满才读（waveIn 填满 dwBufferLength 才返回）；不满说明设备没给数据
    if (m_header.dwBytesRecorded >= m_header.dwBufferLength) {
        const QByteArray frame(m_buffer.constData(), m_header.dwBytesRecorded);
        // 重新入队列继续采
        waveInAddBuffer(m_waveIn, &m_header, sizeof(m_header));
        emit pcmReady(frame);
        m_elapsedMs += 0;
    }
}