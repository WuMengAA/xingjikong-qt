// 星集控被控端 · 插件能力注入（2026-11-02 · 「插件大升级」第 4 项）
// ───────────────────────────────────────────────────────────────────────────
// 从 main.cpp 搬出来的**装配扇面**：13 个函数指针，全是在被控端能力与插件宿主
// 之间做形状转换（枚举越界回落、QJsonValue 与原生类型互转、权限词映射）。
//
// 为什么搬：这段曾在 main.cpp 里占 50 行且每次"插件能力的增删"都要改 main ——
// 而 main.cpp 有「只降不升」门禁，导致每次加一个插件能力都得先找地方还债。
// 装配逻辑放这里之后，下次扩能力只动本文件。
//
// 边界（别在这里写业务）：本文件不判断"该不该推岛"（那是 PluginHost 的
// PluginIslandGate）、不决定"哪些指令能给插件"（那是 PluginActionWhitelist）、
// 不生成课表事件（那是 ScheduleEventBus）。这里只做**形状转换**。
#pragma once

class ScheduleClock;
struct PluginAgentApi;

/**
 * 组装一份插件可用能力表。
 * @param clock 课表时钟（可为 nullptr —— 未配置课表时 schedule_get 会如实回 available=false；
 *              这一刻表指针由 main 持有，因为它在插件宿主创建之后才 new）。
 */
PluginAgentApi pluginApiBridgeBuild(ScheduleClock *clock);
