#include "noise_monitor.h"

#include <QDebug>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QByteArray>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

// ── 采集实现 ────────────────────────────────────────────────────────────────
// 短窗口："打开 → 录 ~窗口时长 → 算 RMS → 关"。每次 sample() 重来一遍，
// 所以从不长占麦克风（与语音对讲互不抢麦：mic 被占时 waveInOpen 会失败 →
// 返回负值，上层据此显示"采集不可用"而不是"安静"）。
namespace {

// 返回 RMS(0..1)；<0 表示"采不到"（无设备 / 设备被占用 / 打开失败 / 没有数据）。
// 0.0 是合法值（真采样到静音），与"采不到"必须区分开 —— 否则安静教室会被
// 误报成"检测器坏了"，检测器坏了又会被误报成"很安静"。
double captureRms(int sampleRate)
{
#if defined(Q_OS_WIN)
    if (waveInGetNumDevs() == 0) return -1.0;    // 没有录音设备

    HWAVEIN h = nullptr;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = static_cast<DWORD>(sampleRate) * 2;

    // CALLBACK_NULL + 轮询：设备被语音对讲独占时这里会失败，直接返回"采不到"
    if (waveInOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return -1.0;

    const int bytes = sampleRate * 2 * 150 / 1000;   // 150ms 窗口 → 16k 时 4800B
    QByteArray buf(bytes, 0);
    WAVEHDR hdr = {};
    hdr.lpData = buf.data();
    hdr.dwBufferLength = static_cast<DWORD>(bytes);

    if (waveInPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        waveInClose(h);
        return -1.0;
    }
    // 2026-10-14 修（审计）：原来这两个返回值不查，失败会一路滑到下面的 600ms
    // 死等 —— 最终同样 return -1.0（结果正确），但白等最多 600ms，期间线程池
    // 线程被占着。这里快速失败，回滚顺序与末尾的释放顺序相反。
    // 注意**故意不打日志**：本函数每 2s 被 Task::run 周期性调用，设备被语音对讲
    // 独占时失败是常态；onCaptured() 已有「从能采到变成采不到才报一次」的去抖，
    // 在这里打 qWarning 会绕过那层去抖、每 2s 刷一条。
    if (waveInAddBuffer(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        waveInUnprepareHeader(h, &hdr, sizeof(hdr));
        waveInClose(h);
        return -1.0;
    }
    if (waveInStart(h) != MMSYSERR_NOERROR) {
        waveInUnprepareHeader(h, &hdr, sizeof(hdr));
        waveInClose(h);
        return -1.0;
    }

    // 等录满（最多 ~600ms 兜底，防设备不吐数据时死等）
    QElapsedTimer t;
    t.start();
    while (hdr.dwBytesRecorded < hdr.dwBufferLength && t.elapsed() < 600)
        Sleep(5);

    waveInStop(h);
    waveInUnprepareHeader(h, &hdr, sizeof(hdr));
    waveInClose(h);

    const int n = static_cast<int>(hdr.dwBytesRecorded) / 2;   // int16 样本数
    if (n <= 0) return -1.0;                                    // 一个样本都没录到

    const short *s = reinterpret_cast<const short *>(buf.constData());
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = s[i] / 32768.0;
        sum += v * v;
    }
    return qSqrt(sum / n);
#else
    Q_UNUSED(sampleRate);
    return -1.0;   // 非 Windows：暂不提供采集
#endif
}

} // namespace

// 异步采集任务：在 QThreadPool 线程里跑 captureRms（会阻塞等录满），
// 算完通过 queued invoke 回到 NoiseMonitor 所属线程 emit，不阻塞主线程。
class NoiseMonitor::Task : public QRunnable
{
public:
    Task(NoiseMonitor *owner, int rate, int seq) : m_owner(owner), m_rate(rate), m_seq(seq) {}
    void run() override
    {
        const double rms = captureRms(m_rate);
        NoiseMonitor *o = m_owner;
        QMetaObject::invokeMethod(o, [o, rms, seq = m_seq] { o->onCaptured(rms, seq); },
                                  Qt::QueuedConnection);
    }

private:
    NoiseMonitor *m_owner;
    int m_rate;
    int m_seq;
};

NoiseMonitor::NoiseMonitor(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);   // 采集串行，独占一个后台线程（不抢全局池）
    connect(&m_timer, &QTimer::timeout, this, &NoiseMonitor::sample);
}

NoiseMonitor::~NoiseMonitor()
{
    stop();
}

void NoiseMonitor::start(int sampleRate, int intervalMs)
{
    if (m_running) return;
    m_sampleRate = sampleRate > 0 ? sampleRate : 16000;
    m_seq = 0;
    m_busy = false;
    m_captureOk = false;
    m_timer.start(qMax(200, intervalMs));
    m_running = true;
}

void NoiseMonitor::stop()
{
    if (!m_running) return;
    m_timer.stop();
    m_running = false;
    m_lastDb = 0.0;
    m_lastActive = false;
    m_captureOk = false;
    m_busy = false;
}

void NoiseMonitor::sample()
{
    if (!m_running || m_busy) return;   // 上一轮还没回来就跳过（防设备慢时堆任务）
    m_busy = true;
    ++m_seq;
    m_pool.start(new Task(this, m_sampleRate, m_seq));
}

void NoiseMonitor::onCaptured(double rms, int seq)
{
    m_busy = false;
    if (!m_running) return;

    if (rms < 0.0) {
        // 采不到：只在"从能采到变成采不到"时报一次 failed，避免每 2s 刷屏
        if (m_captureOk) {
            m_captureOk = false;
            emit failed(QStringLiteral("麦克风采集不可用（无设备或已被语音对讲占用）"));
        }
        m_lastActive = false;
        emit noiseSample(0.0, false, seq);
        return;
    }

    m_captureOk = true;
    // RMS 0..1 → dB（相对满量程）；rms=0（真静音）给一个下限，不是 -inf
    const double db = rms > 0.0 ? 20.0 * log10(rms * 32768.0) : -96.0;
    const bool active = db > -45.0;   // 默认阈值（上层自习模式可用自己的 thresholdDb 重判）
    m_lastDb = db;
    m_lastActive = active;

    emit noiseSample(db, active, seq);
}
