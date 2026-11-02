// 星集控被控端 · 课表事件总线（2026-11-02 · 「插件大升级」第 1 项）
// ───────────────────────────────────────────────────────────────────────────
// 修的是一个**从未实现**的承诺：`pluginhost.cpp:246` 早就把 `schedule_change`
// 列为需 `events:schedule` 权限才能订阅的事件，`kKnownPermissions` 里也一直有
// `events:schedule`，但全库**没有任何地方 emit 过它** —— 插件订阅了永远收不到。
// 症状是最难查的那一类：插件不报错、日志不打、只是"什么都没发生"。
//
// 为什么单开一个文件而不是在 main.cpp 里 connect 一下就完事：
//   ① 课表事件有**四个**来源（上课 / 下课 / 换档案 / 面板查询），逐个 connect
//      到不同 lambda 里各拼一份 payload，四份必然漂移（ClassIsland 那边就是因为
//      同一条信息有好几种形状，插件作者才要写一堆兼容分支）；
//   ② main.cpp 有"只降不升"门禁（台账 4399 行），往上叠 30 行会当场变红；
//   ③ 本域只依赖 QtCore + 三个函数指针，能被离屏探针单独拉起（探针不进 main）。
//
// ── 事件契约（对外稳定，插件 SDK 与内置模块共用，改这里等于改协议）───────────
//   name = "schedule_change"
//   payload = {
//     reason:        "period-started" | "period-ended" | "timetable-changed" | "tick"
//     lessonContent: "第一节 · 语文"      （ClassIsland 同名字段，插件可直读）
//     subjectName:   "语文"               （可能为空 —— 只配了时间点、没配科目时）
//     periodName:    "第一节"
//     slotIndex:     0-based；-1 = 不在任何一节内
//     startTime:     "HH:mm:ss"           （当天本地时刻；跨天课不适用，故不带日期后缀）
//     endTime:       "HH:mm:ss"
//     date:          "yyyy-MM-dd"
//     week:          本学期教学周（1-based，与岛上显示同口径）
//   }
//   ⚠️ 时间刻意用「当天 HH:mm:ss」而不是 ISO 绝对时刻：**课表是周期性的**，
//      把 2026-11-02T08:00:00 发给插件，插件没法判断"这是每周一还是只有这周一"。
//      ClassIsland 的 TimeLayout 也是"每天重复的时间点"语义，对齐它比自创一套更省事。
#pragma once

#include <QString>
#include <QJsonObject>
#include <QTime>

class ScheduleClock;

namespace ScheduleEventBus {

// 三个投递口由 main.cpp 注入。任意一个可以为 nullptr（探针/无插件场景）。
// 用函数指针而不是 Qt 信号：本域不是 QObject，也刻意不成为 QObject ——
// 事件总线一旦有身份，就有人想给它加状态，加了状态之后"同一条事件发两次/漏发"
// 就再也说不清了。无状态 + 纯函数投递，是这个文件唯一想保住的性质。
struct Sinks {
    void (*toPlugins)(const QString &name, const QJsonObject &payload) = nullptr;
    void (*toModules)(const QString &name, const QJsonObject &payload) = nullptr;
    // 上报云端（可选）。云端目前没有 schedule_change 的消费方，留口是给
    // 「管理端实时看到每台设备当前上什么课」这条后续链路用，不接也不影响任何现有功能。
    void (*toCloud)(const QString &name, const QJsonObject &payload) = nullptr;
};

void init(const Sinks &sinks);

/**
 * 把 ScheduleClock 的三条边界信号接到总线上（一条语句，供 main.cpp 调用）。
 * 幂等：重复 attach 不会重复投递（第二次直接返回）。
 */
void attachClock(ScheduleClock *clock);

/** 手工投递（课表档案被下发/替换后调用，reason 固定 "timetable-changed"）。 */
void emitTimetableChanged(const QString &source);

/** 当前是否已 init（探针可据此判断该不该期望收到事件）。 */
bool ready();

/**
 * 给插件 `schedule_get` 用的快照（2026-11-02 插件大升级 #2）。
 *
 * 形状：
 *   { available: bool,          // false = 没有课表源（未配置/档案缺失），与「今天没课」不同
 *     current: {...} | null,    // 正在上的那一节（字段同事件 payload）
 *     lessons: [ {...}, ... ],  // 今天全部课，按开始时间排好
 *     date, week }
 *
 * ⚠️ `available=false` 与 `lessons=[]` 必须能区分：前者是配置问题（该报错/提示绑定），
 *    后者是正常状态（周末、假期）。混在一起会让插件在假期里疯狂提示"课表加载失败"。
 * @param clock 课表时钟；nullptr 时返回 available=false。
 */
QJsonObject buildSnapshot(ScheduleClock *clock);

} // namespace ScheduleEventBus
