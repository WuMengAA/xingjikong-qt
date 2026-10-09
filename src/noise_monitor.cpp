#include "noise_monitor.h"

#include <QDebug>
#include <QtMath>
#include <cstring>

// Windows WASAPI 采集（MMDevice 短窗口采集）。
// 为控制依赖面，这里用「短时采集 → 立即释放」的实现：每次 sample() 打开设备、
// 录 30ms、算 RMS、关掉。开着不关会一直占麦克风（教室机可能同时被语音对讲用）。
namespace {
// WASAPI 极简捕获：真机需要 CoInitialize + MMDevice API。
// 本实现为「能力骨架」：编译通过、接口齐全；真机采集接入留待集成时
// 用 Qt Multimedia 或 WASAPI 完整实现替换 sampleImpl() 的躯壳。
double captureRms(int /*sampleRate*/)
{
    // TODO(集成)：接 WASAPI IMMDeviceEnumerator → IAudioClient 采集窗口。
    // 此处返回 0 表示"无采集能力"，上层据此降级（不误报噪音）。
    return 0.0;
}
} // namespace

NoiseMonitor::NoiseMonitor(QObject* parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &NoiseMonitor::sample);
}

NoiseMonitor::~NoiseMonitor()
{
    stop();
}

void NoiseMonitor::start(int sampleRate, int intervalMs)
{
    if (m_running) return;
    m_sampleRate = sampleRate;
    m_seq = 0;
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
}

void NoiseMonitor::sample()
{
    if (!m_running) return;
    ++m_seq;

    const double rms = captureRms(m_sampleRate);
    // RMS 0..1 → dB（相对满量程）；captureRms 返回 0 = 无采集能力
    const double db = rms > 0.0 ? 20.0 * log10(rms * 32768.0) : 0.0;
    // 阈值：-45dB 以上算"有明显声音"（语音环境经验值）
    const bool active = rms > 0.0 && db > -45.0;
    m_lastDb = db;
    m_lastActive = active;

    emit noiseSample(db, active, m_seq);
}