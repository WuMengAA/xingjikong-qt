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
#include <QThreadPool>
#include <QString>
#include <QList>
#include <limits>

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

    // ══ 2026-10-30（P0-1 补齐 · 笔记 P0 噪音检测）══════════════════════════
    // 三项能力补齐，但**默认行为与以前逐字一致**（不传就是老行为），避免改动面扩大。

    // ① 阈值可配 + SPL 校准偏置
    //    判定口径：`(本次 dB + splOffset) > thresholdDb`
    //      · 未校准（默认）：offset=0、threshold=-45 ⇒ 相对满量程 dBFS（= 既有行为）
    //      · 校准后：offset=校准偏置、threshold=80 ⇒ 按 dB SPL 判定（笔记要求）
    //    ⚠️ **未校准时把 threshold 设成 80 没有意义**：dBFS 与 SPL 差一个未知偏置
    //       （麦克风增益、驱动 AGC 差异可达几十 dB），必须先校准或改用自适应基线，
    //       否则 80 这个数字纯属自欺 —— 这是本模块最容易被误用的一处。
    void setThresholdDb(double v) { m_thresholdDb = v; }
    double thresholdDb() const { return m_thresholdDb; }
    void setSplOffset(double v) { m_splOffset = v; }
    double splOffset() const { return m_splOffset; }
    /** 换算成 SPL 口径的最近一次读数（未校准时 = lastDb()）。 */
    double lastDbSpl() const { return m_lastDb + m_splOffset; }

    // ② 触发计数：一个统计周期（建议=一节次）内累计超阈达 N 次 → 发一次 thresholdReached，
    //    发过就锁定，直到 resetTriggerCounter()（防同周期重复上报刷屏）。
    void setTriggerCount(int n) { m_triggerCount = n > 0 ? n : 1; }
    int triggerCount() const { return m_triggerCount; }
    void resetTriggerCounter();
    int triggerHits() const { return m_hits; }
    bool triggered() const { return m_triggered; }

    // ③ 自适应基线（2026-10-30 · 笔记 P0 的 80dB 如何落地）
    //    问题：dBFS→dB SPL 的偏置随麦克风增益/驱动 AGC 变化，未校准时 80dB 是个
    //    **永远够不到的数**（探针已实锤：未校准设 80 ⇒ 触发 0 次）。
    //    解法：不去猜绝对 SPL，而是学**这间教室自己的安静底噪**，阈值 = 基线 + delta。
    //      · 基线 = 最近窗口样本的低分位（10% 分位，代表"没人吵时"的水平）
    //      · delta = 基线之上多少 dB 算吵（默认 35dB，需按真实课堂实测调）
    //    好处：换麦克风、换驱动、开不开 AGC 都不影响判定，因为它比的是**自己跟自己**。
    //    ⚠️ 窗口样本不足（<20）时回退到 thresholdDb，避免在开机头几秒乱判。
    void setAdaptive(bool on) { m_adaptive = on; }
    bool adaptive() const { return m_adaptive; }
    void setAdaptiveDelta(double db) { m_adaptiveDelta = db; }
    double adaptiveDelta() const { return m_adaptiveDelta; }
    /** 当前学到的安静基线（未启用自适应或样本不足时返回 quiet_NaN）。 */
    double baselineDb() const { return m_baselineDb; }
    /**
     * 丢弃已学的基线重新学。什么时候需要：换教室 / 换节次 / 空调或开窗导致底噪突变 /
     * 麦克风被换过。不及时重置的话，旧基线与新环境混在滑动窗口里，阈值会被"稀释"
     * 成一个两边都不准的数（探针里就是这么翻车的）。
     */
    void resetBaseline();
    /** 本次判定实际使用的阈值（dBFS 口径）—— 自适应开启时随基线浮动。 */
    double effectiveThresholdDb() const { return m_effectiveThreshold; }

    // ④ 测试入口：绕过采集直接注入一个 dB 样本，走**同一套**判定与计数逻辑。
    //    用途：容器/无麦克风环境下验证状态机（真机不调用）。
    //    ⚠️ 不做生产用途：它只是把 evaluate() 暴露出来，不改变任何采集行为。
    void injectSample(double db);

signals:
    // 每次分析完成：dB（相对响度）、有没有噪音、事件序号
    void noiseSample(double db, bool active, int seq);
    // 设备打不开 / 采集失败
    void failed(const QString& reason);
    // 本周期累计超阈达 N 次（默认 3）：hits=累计次数、db=触发那次的 SPL 口径读数
    void thresholdReached(int hits, double db);

private slots:
    void sample();   // 定时触发：起一次异步采集

private:
    class Task;                                  // 异步采集任务（QRunnable，.cpp 内定义）
    void onCaptured(double rms, int seq);        // 子线程算完 → 回主线程 emit
    /** 判定 + 计数 + 发信号的唯一出口（onCaptured 与 injectSample 共用，保证同口径）。 */
    void evaluate(double dbRaw, int seq);

    bool m_running = false;
    int m_sampleRate = 16000;
    QTimer m_timer;
    // ⚠️ 2026-10-13：专用单线程池 —— 原来用 QThreadPool::globalInstance()，会和摄像头抓拍/
    //    录制等抢全局池（池满时噪音采样被静默挤掉）。噪音采集是"短窗口阻塞等录满"，
    //    独占一个后台线程最省事，也不连累别的任务。
    QThreadPool m_pool;
    double m_lastDb = 0.0;
    bool m_lastActive = false;
    bool m_captureOk = false;   // 最近一次采集成功？
    bool m_busy = false;        // 上一次采集还没回来，跳过本轮（防重入）
    int m_seq = 0;

    // ── 2026-10-30 P0-1 补齐（默认值 = 既有行为，零行为变更）──
    double m_thresholdDb = -45.0;   // 默认按 dBFS 判（与旧代码 `db > -45.0` 一致）
    double m_splOffset   = 0.0;     // 0 = 未校准；校准后填 dBFS→dB SPL 的偏置
    int    m_triggerCount = 3;      // 笔记：3 次触发才上报
    int    m_hits        = 0;       // 本周期累计超阈次数
    bool   m_triggered   = false;   // 已发过 thresholdReached（锁定，防重复）

    // 自适应基线（默认关闭 ⇒ 行为与既有完全一致）
    bool   m_adaptive       = false;
    double m_adaptiveDelta  = 35.0;                     // 基线之上多少 dB 算吵
    QList<double> m_window;                             // 最近样本（滑动窗口）
    int    m_windowMax      = 200;                      // 约 200×2s ≈ 7 分钟的观察窗
    double m_baselineDb     = std::numeric_limits<double>::quiet_NaN();
    double m_effectiveThreshold = -45.0;                // 本次实际判定阈值（dBFS 口径）
};
