#include "noise_incident_service.h"

void NoiseIncidentService::setPeriodKey(const QString& key)
{
    if (key == m_periodKey) return;   // 同一节次反复设置不该把计数清掉
    m_periodKey = key;
    reset();                          // 换节次 ⇒ 新周期
}

std::optional<NoiseIncident> NoiseIncidentService::onSample(double db, bool active,
                                                           const QDateTime& at,
                                                           double baselineDb,
                                                           double thresholdDb)
{
    if (!active) return std::nullopt;          // 安静样本不计入「吵了几次」
    ++m_hits;

    // 已达阈值且本周期已报过 ⇒ 继续累计次数但不再产出事件（防刷屏上报）
    if (m_triggered || m_hits < m_triggerCount) return std::nullopt;

    m_triggered = true;
    NoiseIncident inc;
    inc.at           = at;
    inc.periodKey    = m_periodKey;
    inc.hits         = m_hits;
    inc.db           = db;
    inc.baselineDb   = baselineDb;
    inc.thresholdDb  = thresholdDb;
    return inc;
}

void NoiseIncidentService::reset()
{
    m_hits      = 0;
    m_triggered = false;
}
