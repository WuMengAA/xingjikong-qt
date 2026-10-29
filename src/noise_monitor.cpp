#include "noise_monitor.h"

#include <QDebug>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QByteArray>
#include <QtMath>
#include <algorithm>

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
    resetTriggerCounter();   // 新周期（建议=一节次）开始，计数归零
    m_window.clear();        // 基线也重新学：换了一节课，安静底噪可能不同（开窗/空调）
    m_baselineDb = std::numeric_limits<double>::quiet_NaN();
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
    evaluate(db, seq);   // 判定/计数统一走 evaluate（与 injectSample 同口径）
}

// ── 2026-10-30（P0-1）：判定 + 计数的唯一出口 ──────────────────────────────
// 拆出来是为了让"注入测试"和"真采集"走**同一条**判定路径 —— 否则测试验的是另一套
// 逻辑，等于没验。真机不调用 injectSample，但两者共用这里，计数语义才有保障。
void NoiseMonitor::evaluate(double dbRaw, int seq)
{
    // ── 先算出本次该用的阈值（统一在 SPL 口径上比较，避免两个口径混着算）──
    //   · 固定模式：阈值就是 thresholdDb（未校准时它是 dBFS，语义由调用方负责）
    //   · 自适应模式：阈值 = 学到的安静基线 + delta（基线也换算到同一口径）
    //   · 样本不足 20 个 ⇒ 回退固定阈值，避免开机头几秒拿几个样本就乱判
    double thrSpl = m_thresholdDb;
    if (m_adaptive) {
        m_window.append(dbRaw);
        if (m_window.size() > m_windowMax) m_window.removeFirst();
        if (m_window.size() >= 20) {
            QList<double> sorted = m_window;
            std::sort(sorted.begin(), sorted.end());
            // 10% 分位 = "这间教室安静时大概多少 dB"；用它比用均值更抗偶发尖峰
            const int idx = qMin(sorted.size() - 1, qRound(sorted.size() * 0.10));
            m_baselineDb = sorted[idx];
            thrSpl = (m_baselineDb + m_splOffset) + m_adaptiveDelta;
        }
    }
    m_effectiveThreshold = thrSpl - m_splOffset;   // 回写 dBFS 口径，供诊断/日志

    const double dbJudge = dbRaw + m_splOffset;   // 未校准时 offset=0 ⇒ 等价旧行为
    const bool   active  = dbJudge > thrSpl;

    m_lastDb     = dbRaw;
    m_lastActive = active;
    emit noiseSample(dbRaw, active, seq);

    if (!active) return;

    ++m_hits;
    // 达 N 次才发一次，且发过就锁住 —— 同一个统计周期内不重复上报（笔记：3 次才上报）
    if (!m_triggered && m_hits >= m_triggerCount) {
        m_triggered = true;
        emit thresholdReached(m_hits, dbJudge);
    }
}

void NoiseMonitor::resetTriggerCounter()
{
    m_hits      = 0;
    m_triggered = false;
}

void NoiseMonitor::resetBaseline()
{
    m_window.clear();
    m_baselineDb = std::numeric_limits<double>::quiet_NaN();
    m_effectiveThreshold = m_thresholdDb - m_splOffset;
}

void NoiseMonitor::injectSample(double db)
{
    // 测试/调试用：绕过麦克风直接喂一个 dB 值，走与真采集完全相同的判定与计数。
    // 不检查 m_running —— 无麦克风环境下也要能验状态机。
    evaluate(db, ++m_seq);
}
