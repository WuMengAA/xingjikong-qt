#pragma once
// 噪音事件策略（域服务）—— 2026-10-30 · P0-2 分层第 2 层
//
// ── 为什么要有这个类 ────────────────────────────────────────────────────
// 上一批把「累计 3 次 → 该上报了」的判断写进了 main.cpp 的 lambda：
//     QObject::connect(g_noise, &NoiseMonitor::thresholdReached, g_ws, [](int hits, double db){ ... })
// 后果有三个：
//   ① 策略不可单测（要跑起整个进程、连上 WS 才能看结果）；
//   ② 计数状态藏在 NoiseMonitor 的私有成员里，谁想知道"现在几次"只能再开个接口；
//   ③ main.cpp 继续膨胀（已 8700 行）。
// 所以把这段策略搬出来：**纯逻辑、零 Qt 对象、零网络**，用普通单测就能验。
//
// ── 职责边界（很重要，别再往里塞东西）──────────────────────────────────
// 它只回答一个问题：「这次样本该不该产出一个上报事件？」
// 它**不**负责：抓帧、序列化、发送、写日志、发信号。
// 那些属于应用服务（NoiseEvidenceCapture / NoiseReporter）与传输层（CloudLink）。
//
// ── 统计周期 ────────────────────────────────────────────────────────────
// 以「节次」为周期：setPeriodKey() 一换就自动清零。这样"3 次"不会被上一节课的
// 计数污染，也不需要在多个调用点手动 reset（手动 reset 总会有人漏）。

#include "noise_incident.h"

#include <optional>

class NoiseIncidentService
{
public:
    void setTriggerCount(int n) { m_triggerCount = n > 0 ? n : 1; }
    int  triggerCount() const { return m_triggerCount; }

    /** 换节次（如 "2026-10-30#3"）；key 变化即开启新统计周期并清零。 */
    void setPeriodKey(const QString& key);
    QString periodKey() const { return m_periodKey; }

    /**
     * 喂一次判定结果。
     * @return 有值 = 本次该产出上报事件（同一周期只会返回一次）；空 = 还没到，或已报过
     */
    std::optional<NoiseIncident> onSample(double db, bool active, const QDateTime& at,
                                          double baselineDb, double thresholdDb);

    /** 手动清零（一般不用：换节次会自动清）。 */
    void reset();

    int  hits() const { return m_hits; }
    bool triggered() const { return m_triggered; }

private:
    int     m_triggerCount = 3;      // 笔记：累计 3 次才上报
    int     m_hits         = 0;
    bool    m_triggered    = false;  // 已产出过事件（锁定，防同周期重复上报）
    QString m_periodKey;
};
