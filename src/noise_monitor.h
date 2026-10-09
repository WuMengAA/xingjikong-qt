#pragma once
// 噪音检测（2026-10-08 · 通知体系升级 §6.5 独立模块）
//
// 独立 QObject：周期性从麦克风拾音并分析，输出指标由上层决定用途
// （管理端展示 / 告警触发都行，本模块不碰 UI 与网络）。
//   · 周期采集（默认 2s 一次），分析分贝 + 简单频带能量
//   · start()/stop() 幂等；停止后释放设备
//   · 指标通过 signal 抛出：{dB, bandEnergy[], active}
//   · 不驻留录音文件（只管当前窗口的分析结果）
//
// 接入（main.cpp 或专用线程）：start → 收 noiseSample 信号 → 按需上传管理端。

#include <QObject>
#include <QTimer>
#include <QString>

class NoiseMonitor : public QObject {
    Q_OBJECT
public:
    explicit NoiseMonitor(QObject* parent = nullptr);
    ~NoiseMonitor() override;

    // 开始周期采集（幂等；设备打不开 → emit failed）
    void start(int sampleRate = 16000, int intervalMs = 2000);
    // 停止并释放设备（幂等）
    void stop();

    bool running() const { return m_running; }
    // 最近一次分析的 dB（方便定时读，不依赖信号时序）
    double lastDb() const { return m_lastDb; }
    // 本窗口是否判定为"有明显声音"（> 阈值）
    bool lastActive() const { return m_lastActive; }

signals:
    // 每次分析完成：dB（相对响度）、有没有噪音、事件序号
    void noiseSample(double db, bool active, int seq);
    // 设备打不开 / 采集失败
    void failed(const QString& reason);

private:
    void sample();   // 采集一小段并分析

    bool m_running = false;
    int m_sampleRate = 16000;
    QTimer m_timer;
    double m_lastDb = 0.0;
    bool m_lastActive = false;
    int m_seq = 0;
    // 平台采集句柄（Windows WASAPI 等，实现层持有）
    void* m_capture = nullptr;
};