#include "island_panel_source.h"

#include "../domain/panel_domain.h"
#include "island_countdown_target.h"

namespace {
// 常驻倒计时面：先取管理端下发的固定目标（持久化在 QSettings），
// 没有 → 回落「课表边界」语义（buildCountdown 已支持空 params）。
// 固定目标优先级高于课表边界，符合 ClassIsland 倒计时「盯死一个时刻」的产品语义。
QJsonObject countdownParams()
{
    const QJsonObject fixed = CountdownTarget::params();
    if (!fixed.isEmpty()) return fixed;
    return QJsonObject();
}
}   // namespace

IslandPanelSource::IslandPanelSource(QObject *parent)
    : QObject(parent)
{
}

QVariantMap IslandPanelSource::dateInfo() const
{
    // 日期面恒可用（不依赖课表）：只有「第几周」需要课表，那是 PanelDomain 内部的事。
    return PanelDomain::build(QStringLiteral("date"), QJsonObject()).data.toVariantMap();
}

QVariantMap IslandPanelSource::countdownInfo() const
{
    const PanelDomain::Result r = PanelDomain::build(QStringLiteral("countdown"), countdownParams());
    QVariantMap m = r.data.toVariantMap();
    // 失败时把原因带过去，QML 侧据此写「课表未接上」而不是画一个 00:00
    // （00:00 会被当成"正好到点"，是最坏的一种错）。
    if (!r.ok) m.insert(QStringLiteral("error"), r.error);
    return m;
}

QStringList IslandPanelSource::availableFaces() const
{
    return PanelDomain::faces();
}

IslandPanelSource *islandPanelSourceInstance()
{
    static IslandPanelSource *s = new IslandPanelSource();
    return s;
}
