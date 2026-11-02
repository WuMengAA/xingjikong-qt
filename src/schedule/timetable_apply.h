#pragma once
#include <QString>
#include <QJsonObject>
#include "profile.h"

// 课表下发落地（2026-11-02 · 「被控端仿 ClassIsland 组件，课表相关同步」）
// ───────────────────────────────────────────────────────────────────────────
// 补一条**从来不存在**的链路：管理端把课表下发到教室机。
//
// 改造前的实况（逐条实读，不是推测）：
//   · 云端 `class-admin-students.js:adminImportTimetable` 落库后只做两件事：
//     写审计事件 + `pushClassEvent(..., 'timetable', ...)` 广播给**管理端浏览器**。
//     —— 全程没有任何到设备的通道。管理端界面显示「导入成功」，教室机一无所知。
//   · 被控端课表源固定读 `%LOCALAPPDATA%\ClassIsland\data\Profiles\Default.json`，
//     也就是**本机的 ClassIsland 档案**。管理端改云端库，与这个文件毫无关系。
//   结论：这是「两边都在、中间没接」的典型 —— 不是 bug，是从没建过。
//
// 本模块只做一件事：把云端归一化信封（TIMETABLE_SCHEMA='classisland/v1'）
// **翻译成 ClassIsland 官方 Profile 结构**再落盘，让 ScheduleClock 原样就能读。
//
// 为什么不另存一套自有格式：
//   ① 教室机上的 ClassIsland 本体还要用这份档案，换成自有格式等于把两边拆散；
//   ② `Profile::fromJson` 已经能吃 ClassIsland 真机结构（TimeRule + Classes 下标），
//      再造一套只会多一条需要维护的解析路径。
namespace TimetableApply {

/** 落地结果（回执与日志都用它，避免各处各拼一套文案）。 */
struct Result {
    bool ok = false;
    QString error;
    QString path;       // 落盘路径（诊断/回执用）
    int dayCount = 0;   // 写进去了几天
    int lessonCount = 0;// 一共几节课
    bool changed = false; // 内容与磁盘上已有档案是否不同（false ⇒ 不重载时钟，避免无谓抖动）
};

/**
 * 把云端信封应用到本机档案。
 * @param envelope 云端 `normalized` 对象（含 schema/source/group/days/layouts/subjects）
 * @param profilePath 目标档案路径（通常 = ScheduleClock 正在读的那个）
 * @param classCode 班级码，写进档案名便于现场排查（可为空）
 *
 * 行为约定（都有理由，改动前先看）：
 *   · **schema 不认就拒**：不认识的版本宁可不动磁盘，也不要把档案写坏 ——
 *     写坏了下一拍 ScheduleClock 读不出课表，教室机直接变"今天没课"。
 *   · **空 days 也拒**：一张没有课的课表落盘 = 把当天课全清空，
 *     这是"下发了一个空档案"和"今天真的放假"的混淆点，宁可报错让人再看一眼。
 *   · **先写 .tmp 再 rename**：直接 truncate 原文件时若进程被杀，档案变半截 JSON，
 *     ClassIsland 与本机同时读坏。rename 在同分区是原子的。
 *   · **原档案备份一次**（.bak-<时间戳>）：现场救命用，不改语义。
 */
Result applyEnvelope(const QJsonObject &envelope, const QString &profilePath,
                     const QString &classCode = QString());

} // namespace TimetableApply
