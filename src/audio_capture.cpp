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
    // 只在锁内读一下就立刻放锁：下面马上要调 g_activeCapture->stopCapture()，
    // 那是**另一个对象**的锁，但本对象的 m_active 判据必须先拿锁读完再放行。
    {
        QMutexLocker lock(&m_mu);
        if (m_active) return true;
    }
    if (g_activeCapture && g_activeCapture != this) {
        // 别的实例在采：先停它（管理端单麦语义）。锁外调 —— 抢的是那个实例的锁。
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

    // 收尾也在锁内：handleBuffer() 判的是「m_active && m_waveIn」，
    // 这两个必须成对可见，否则回调会卡在 m_active 已置位、缓冲却还没排好的窗口里。
    {
        QMutexLocker lock(&m_mu);
        m_active = true;
    }
    g_activeCapture = this;
    qInfo("[audio] 🎤 麦克风已开（16kHz 单声道 16bit，20ms/帧）");
    return true;
}

void AudioCapture::stopCapture()
{
    HWAVEIN waveIn = nullptr;
    WAVEHDR hdrForUnprepare = {};
    {
        QMutexLocker lock(&m_mu);
        if (!m_active && !m_waveIn) return;
        m_active = false;
        if (g_activeCapture == this) g_activeCapture = nullptr;
        // 锁内只做状态清理 + 把句柄/头**拷出来**，winmm 回收放锁外。
        // 2026-10-16 修（语音对讲「点击录制就卡死」）：原实现**持锁调**
        // waveInStop/Reset/Unprepare/Close —— waveInClose 需要驱动线程完成设备清理，
        // 而驱动线程的 handleBuffer() 又在等这把锁 → 经典死锁：按住讲话松开
        // （快速 start/stop 反复）时主线程卡死在 waveInClose，界面假死。
        waveIn = m_waveIn;
        m_waveIn = nullptr;
        hdrForUnprepare = m_header;   // 拷走（m_header 马上清零，unprepare 用这把拷贝）
        m_header = {};
        m_buffer.clear();
    }
    // 锁已放：驱动线程此时可自由拿锁；handleBuffer() 判 !m_waveIn 会直接 return、
    // 不再入队，不会触碰已关句柄。主线程锁外做设备回收，不再同锁互等。
    if (waveIn) {
        waveInStop(waveIn);
        waveInReset(waveIn);
        // ⚠️ 2026-10-17（真机语音断续实证）：waveInReset 是异步的，立即 unprepare 会报
        //   「Cannot perform this operation while media data is still playing」→ 头没释放
        //   → 快速开关麦（按住讲话松开再按）时资源冲突，采集断续。等缓冲真正空闲再释放。
        for (int i = 0; i < 50; ++i) {
            if (!(hdrForUnprepare.dwFlags & WHDR_DONE)) {
                // 缓冲仍在驱动线程手里（INQUEUE 或播放中），稍等
                Sleep(10);
            } else {
                break;
            }
        }
        if (hdrForUnprepare.lpData) {
            if (const MMRESULT r = waveInUnprepareHeader(waveIn, &hdrForUnprepare, sizeof(WAVEHDR));
                r != MMSYSERR_NOERROR)
                qWarning("[audio] waveInUnprepareHeader 失败：%s",
                         waveErrText(r).toUtf8().constData());
        }
        if (const MMRESULT r = waveInClose(waveIn); r != MMSYSERR_NOERROR)
            qWarning("[audio] waveInClose 失败：%s", waveErrText(r).toUtf8().constData());
    }
    qInfo("[audio] 🎤 麦克风已关（静音）");
}

void AudioCapture::handleBuffer()
{
    // 本函数跑在 winmm 音频驱动线程（见 viewerbackend_voice.cpp:136 的注释）。
    // 两件事必须先在锁内做完，否则和主线程的 stopCapture() 并发就是 UB：
    //  ① 原来 onFrame(m_buffer) 直接把 m_buffer 的引用递出去，而 stopCapture() 的
    //     m_buffer.clear() 可能同一刻在跑 —— 边读边清。先锁内拷出来，锁外再吐。
    //  ② 原来完全不查 waveInAddBuffer 的返回值：句柄已被 stop 关掉时它照样返回失败，
    //     代码却当成功 —— 这就是"麦克风拔了也不报错、界面一声不吭"的源头。
    QByteArray frame;
    QString failMsg;
    {
        QMutexLocker lock(&m_mu);
        if (!m_active || !m_waveIn) return;   // stop 已回收 → 别往已关句柄上入队
        frame = m_buffer;                     // 锁内拷贝，锁外吐帧
        const MMRESULT r = waveInAddBuffer(m_waveIn, &m_header, sizeof(m_header));
        // WAVERR_STILLPLAYING 不算错：单缓冲设计只在 WIM_DATA 后入队，不会真撞上；
        // 真返回了也只是说上一帧还在放，不该因此中断采集。
        if (r != MMSYSERR_NOERROR && r != WAVERR_STILLPLAYING)
            failMsg = QStringLiteral("采集中断：%1（可能麦克风被拔了）").arg(waveErrText(r));
    }
    // 出锁之后再调回调：onError 那条链会经 stopSpeaking 回来调 stopCapture，
    // 抢的是这把**非递归**锁 —— 在锁内调就是自死锁。
    if (!failMsg.isEmpty()) {
        if (onError) onError(failMsg);
        return;
    }
    if (onFrame) onFrame(frame);
}
