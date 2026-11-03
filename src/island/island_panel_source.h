#pragma once

#include <QObject>
#include <QVariantMap>

// 灵动岛「组件」数据源（2026-11-03）
//
// ── 为什么又开一个文件（而不是塞进 IslandBridge）─────────────────────
//  `island_bridge.h` 已 318 行、台账 318（白名单内**加一行就 error**），
//  而「日期」「倒计时」两个面的数据本来就是 PanelDomain 的纯计算结果，
//  和桥的状态机没有关系。塞进桥等于为了省一个文件去动一个只降不升的账。
//
// ── 为什么是 Q_INVOKABLE 函数而不是 Q_PROPERTY ────────────────────────
//  两个面都是「打开那一刻算一次」的语义：
//    · 日期面自己用 Date.now() 续走秒针（info.nowMs 只是起算点）；
//    · 倒计时面的目标时刻是「打开时按课表边界推一次」的。
//  做成属性就得配一个定时器不停 emit changed，而那会让正在看的倒计时
//  每隔几秒被重置一次目标 —— 把「距下课」越算越远。
//
// ── 失败口径（重要）─────────────────────────────────────────────────
//  `build()` 失败时返回**带 error 的空对象**，QML 侧必须能区分
//  「今天没课」与「课表没接上」：前者是事实，后者是故障。
//  因此这里原样透传 PanelDomain 的 error 字段，不吞、不编造。
class IslandPanelSource : public QObject
{
    Q_OBJECT

public:
    explicit IslandPanelSource(QObject *parent = nullptr);

    /** 日期面数据：time/date/weekday/week/weekNum/nowMs/skewMs（见 PanelDomain::buildDate）。 */
    Q_INVOKABLE QVariantMap dateInfo() const;

    /** 倒计时面数据：targetMs/nowMs/skewMs/label/note/targetText（targetMs<=0 = 无目标）。 */
    Q_INVOKABLE QVariantMap countdownInfo() const;

    /** 哪些面有可用数据（诊断/设置页用来解释"为什么这一面是空的"）。 */
    Q_INVOKABLE QStringList availableFaces() const;
};

/** 全机唯一实例；由 island.cpp 以 context property `panelSource` 注入。 */
IslandPanelSource *islandPanelSourceInstance();
