#pragma once
// 噪音存证链路协调器（2026-10-30 · P0-2 分层总装点）
//
// 把四层粘起来：域服务（计数 / 周期） + 抓帧编排 + 上报 + 传输。
// 它本身**不写业务判断**——判定在 NoiseIncidentService，抓帧在 NoiseEvidenceCapture，
// 序列化在 NoiseReporter。它只负责「收到样本 → 问服务要不要上报 → 要就抓帧 → 抓完上报」的时序。
//
// 为什么是 QObject + 信号槽：它要接 NoiseMonitor::noiseSample（每 2s 一次），并在工作线程
// 抓帧后把结果**回主线程**再上报（QWebSocket 只能在拥有它的线程用）。
//
// ⚠️ scheduleCapture 是可覆写的虚函数：单测子类把它改成同步执行，避免依赖线程调度，
//    从而确定性地验证「3 次 → 1 次上报」这类时序逻辑。
#include <QObject>
#include "noise_incident_service.h"
#include "noise_sample.h"

class NoiseEvidenceCapture;
class NoiseReporter;

class NoiseIncidentPipeline : public QObject
{
    Q_OBJECT
public:
    NoiseIncidentPipeline(NoiseEvidenceCapture *capture, NoiseReporter *reporter,
                          QObject *parent = nullptr);
    ~NoiseIncidentPipeline() override = default;

    /// 每收到一次噪音判定结果（来自 NoiseMonitor::noiseSample 接线）。
    /// db/active 是噪音样本；baselineDb/thresholdDb 由接线处从 NoiseMonitor 现读
    /// （纯数据搬运，不算业务判断）。
    void onSample(double db, bool active, double baselineDb, double thresholdDb);

    /// 换节次重置统计周期（main 接 ScheduleClock 信号）。空 key = 清掉当前周期。
    void setPeriodKey(const QString &key) { m_service.setPeriodKey(key); }

    int incidentCount() const { return m_incidents; }   // 已产出的事件数（诊断 / 测试）

public slots:
    /// 抓帧完成（默认由 scheduleCapture 跨线程后回主线程触发；测试可直连）。
    void onEvidenceReady(const NoiseIncident &inc, const NoiseEvidence &ev);

protected:
    /// 默认实现：丢到工作线程抓帧，抓完回主线程调 onEvidenceReady。
    /// 测试子类可覆写成本地同步，避免依赖线程调度。
    virtual void scheduleCapture(const NoiseIncident &inc);

    NoiseEvidenceCapture *captureSource() const { return m_capture; }   // 供测试子类同步抓帧

private:
    NoiseIncidentService  m_service;          // 值语义持有（无父子）
    NoiseEvidenceCapture *m_capture = nullptr; // 不拥有
    NoiseReporter        *m_reporter = nullptr; // 不拥有
    int m_incidents = 0;
};
