#pragma once
#include <QJsonObject>

// 灵动岛「倒计时」面的固定目标（2026-11-03 · Task1 倒计时下发 + Task5 替代 ClassIsland 倒计时组件）
//
// ── 它解决什么 ────────────────────────────────────────────────────
//   此前的常驻倒计时面（face 9）走 `IslandPanelSource::countdownInfo()`，
//   永远调 `PanelDomain::build("countdown", {})` ⇒ 只能算「课表边界」
//   （正在上课→距下课，否则→距下一节上课），读不到老师指定的死时刻。
//   ClassIsland 的倒计时组件（CountDown）正是「固定到时」语义：老师设一个
//   目标时刻（如 12:30 午休），面板一直盯它，跨重启保留。
//
// ── 为什么独立成模块而不是塞进 panel_domain.cpp ─────────────────────
//   `panel_domain.cpp` 已经 #include 了 island.h / schedule_clock.h / timesync_domain.h，
//   链进探针会拖一大堆依赖。而「固定目标」只是 QSettings 里一个 JSON 字符串 +
//   三个字段的校验，**纯状态、零业务依赖**。独立出来后：
//     · 被控端常驻面（`IslandPanelSource`）与管理端临时面板（`island_panel` 指令）
//       走同一条 `buildCountdown(params)`，params 来自这里；
//     · 探针可以只链本文件，轻量、可单测、可反向验证。
//
// ── 持久化键（门禁契约）──────────────────────────────────────────
//   QSettings 键 `panel/countdownTarget`（JSON 字符串 `{to,label,title}`）。
//   改键名 = 让所有已配置教室机一夜之间丢了固定目标。读坏/空/非 JSON ⇒ 回落未配置。
//
// ── 与「临时 island_panel」的关系 ─────────────────────────────────
//   临时面板（管理端发 `island_panel{face:"countdown",to:"12:30"}`）是**当次**显示，
//   常驻面（face 9）读的是这里持久化的目标。两者语义同源、互不冲突：
//   临时面板优先级更高（它带着 durationSec 直接 showPanel），但关掉后常驻面仍显示固定目标。
namespace CountdownTarget {

// 常驻倒计时面的固定目标 params（含 to/label/title）。
// 空对象 = 未配置 ⇒ 调用方应退回「课表边界」语义（buildCountdown 已支持空 params）。
QJsonObject params();

// 配置 / 清除：
//   in.clear=true 或 in.to 空 ⇒ 清除（回落课表边界）。
//   in.to 必须是 "HH:MM" 或 ISO8601，否则 {ok:false, error}。
//   in.label / in.title 可选。
// 返回 {ok:bool, error?:string}。
QJsonObject set(const QJsonObject &in);

}   // namespace CountdownTarget
