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
//
// ── 2026-10-13：captureRms 从「能力骨架（return 0）」换成真采集 ──
// 用 winmm waveIn 做**短窗口**采集：每次分析临时打开设备 → 录 ~150ms → 算 RMS → 立刻关。
// 为什么不常开：教室机麦克风可能同时被语音对讲用（mic 是独占资源），长开=抢麦；
// 而且长开等于"常驻监听"，与隐私纪律冲突。短窗口"开→录→关"两不误。
// 采集放 QThreadPool 异步跑 —— waveIn 要等录满才拿得到数据，同步做会卡主线程
// （灵动岛动画/托盘事件都会顿），所以采样在子线程、结果回主线程 emit。

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
    // 本窗口是否判定为"有明显声音"（> 默认阈值 -45dB）
    bool lastActive() const { return m_lastActive; }
    // 采集是否真可用（有麦克风且最近一次分析成功）——供上层区分"安静"与"没采到"
    bool captureAvailable() const { return m_captureOk; }

signals:
    // 每次分析完成：dB（相对响度）、有没有噪音、事件序号
    void noiseSample(double db, bool active, int seq);
    // 设备打不开 / 采集失败
    void failed(const QString& reason);

private slots:
    void sample();   // 定时触发：起一次异步采集

private:
    class Task;                                  // 异步采集任务（QRunnable，.cpp 内定义）
    void onCaptured(double rms, int seq);        // 子线程算完 → 回主线程 emit

    bool m_running = false;
    int m_sampleRate = 16000;
    QTimer m_timer;
    double m_lastDb = 0.0;
    bool m_lastActive = false;
    bool m_captureOk = false;   // 最近一次采集成功？
    bool m_busy = false;        // 上一次采集还没回来，跳过本轮（防重入）
    int m_seq = 0;
};
