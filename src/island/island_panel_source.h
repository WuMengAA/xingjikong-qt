#pragma once

#include <QObject>
#include <QVariantMap>

#include <functional>   // openEditor 的注入钩子（见类尾注释）

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

    /** ★收起条组件槽（2026-11-04 · m16706/m16757 用户裁决）────────────────
     *  「时钟什么的组件应该显示在**收起态**上，而不是放在页面里面」。
     *
     *  实测本机 ClassIsland 主界面是一条常驻横条：
     *    `倒计时 | 天气 | 周三 11/04 第10周 | 00:58 | 早化数物英数语地地自化地 | 下午第一·07:25`
     *  —— 每个组件就是一个并排单元。本方法就是那群单元的**数据侧**：
     *  按 islandLayout 的启用态与顺序，把各面压成
     *  `[{id,icon,text,sub,accent,wide}]`，QML 只负责画。
     *
     *  ⚠️ 为什么不在 QML 里拼：科目名、周次、倒计时剩余分钟全是业务计算
     *  （§10.9 封杀令）。QML 拿到的必须是**已经能直接画**的字符串。
     *
     *  ⚠️ 为什么 Q_INVOKABLE 而不是 Q_PROPERTY：与本文件其它方法同源 ——
     *  每次调用重算一遍，由 QML 的 1s 定时器驱动重取（时钟/课表/倒计时都要走字）。 */
    Q_INVOKABLE QVariantList barSlots() const;

    /** 收起条整条的一句话摘要（`bridge.barLesson` 同源，但**不依赖**桥注入）；
     *  用于 QML 在 slots 为空时至少有东西显示。空串 = 今天没课（不是故障）。 */
    Q_INVOKABLE QString barLessonText() const;

    /** ★点收起条上的组件槽 → 展开并打开那一面（2026-11-04 · m16757）。
     *
     *  用户原话：「灵动岛触发展开除了通知，其他的改为按收起态上面的各种按钮组件进行展开」。
     *  ⇒ 点「今」就摊开今日课表面、点「期」摊开日期面、点「倒」摊开倒计时面；
     *  不再是一律展开到概览再让人自己翻。
     *
     *  ⚠️ 为什么不走 `bridge.requestExpand`：那个只是「展开/收起」开关，
     *  展开后画什么由 panelFace 决定。要「展开到指定面」，必须同时设 panelFace ——
     *  那是 IslandOverlay 的成员，桥没有这个入口（也不该有：桥管状态机，
     *  面板内容归 PanelDomain）。所以这条路径从数据源直接调 PanelDomain::show。
     *
     *  返回 false = 这一面当前没有可用数据（如倒计时未配置目标）——
     *  QML 侧据此什么都不做，而不是摊开一个空面板。
     *  @param id 与 islandLayout 的行 id 一致（10 钟 / 8 期 / 9 倒 / 7 今） */
    Q_INVOKABLE bool openFace(int id);

    /** ★长按组件槽 → 进编辑模式（2026-11-04 · m16757「长按进入编辑模式」）。
     *
     *  用户要的是「能编辑组件」，不必然是「原地浮层编辑」。星集控的编辑面已经存在
     *  且比浮层强：设置页「主界面」分类里那张灵动岛组件卡
     *  （`IslandLayoutCard.qml`）本来就有拖动把手 / 启停开关 / 缩放 −/+。
     *  ⇒ 长按 = 打开设置页并切到「主界面」分类，把 `id` 那一行带到眼前。
     *
     *  ⚠️ 为什么不走 `bridge.settingsAction("openeditor:...")`：
     *  `island_bridge.h` 是 318 行 / 台账 318 的「加一行就 error」文件；而打开面板
     *  需要 AgentShell（QQuickWidget），把那个依赖塞进岛桥会让**所有链岛桥的探针**
     *  都被迫拖上整套面板依赖（2026-11-02 探针 LNK2019 血案的同一形状）。
     *  所以这里放一个 std::function 钩子：岛桥只管算，真正开面板由 main.cpp 注入。
     *  探针不注入 ⇒ 钩子为空 ⇒ 直接返回 false，什么都不做（探针不需要真开窗）。 */
    Q_INVOKABLE bool openEditor(int id);

    /** 注入「打开设置页某个分类」的实现（main.cpp 在装配段调用一次）。
     *  不注入时 openEditor 返回 false —— 探针/管理端共用本文件时即走这条路。 */
    static void setEditorOpener(std::function<void(int)> opener);
};

/** 全机唯一实例；由 island.cpp 以 context property `panelSource` 注入。 */
IslandPanelSource *islandPanelSourceInstance();
