// 星集控被控端 · 发布中心卡片 → 通知参数（2026-10-24 · P1 第一刀）
//
// 为什么独立成件而不写在 main.cpp：main.cpp 已超拆分台账基线（登记 7647，现 7658），
// AGENTS.md §10 明令「烂地基上不许再叠」；而这段是**纯逻辑**（无 QObject / 无全局 /
// 无网络 / 无定时），独立后可单测（与 ThumbnailReporter 的 tick 同口径）。
//
// 只做一件事：把云端 blackboard-card 载荷翻译成 notifyFromParams 认识的 params。
// **渲染一律复用 notifyFromParams**（kind=danmaku → 顶部跑马灯；kind=fullscreen →
// 全屏文字窗）—— 它已经实现了考试门控、本机通知总开关、托盘留存、TTS 朗读四件事；
// 另写一套展示必然与它漂移（两份真源铁律）。
//
// 类型 → 形态（与云端 class-admin.PUBLISH_TYPES 对齐，顺序无关）：
//   fullscreen-notice / homework → 全屏文字窗
//   student-notice / class-notice → 顶部跑马灯
//
// 载荷形状（云端 sendToAgentMsg 会把 payload 平铺进消息体，另注入 uid/from）：
//   { type, title, body, from, at, id } —— 全在顶层，不嵌 data。

#pragma once

class QJsonObject;
class QString;

namespace BlackboardCard {

/**
 * 把一张黑板卡片翻译成 notify 参数。
 * @param payload blackboard-card 的消息体（main.cpp 里的 pay）
 * @param out     成功时填入 notifyFromParams 的 params（kind / title / content）
 * @param why     可选：忽略原因（人话，供日志；成功时不写）
 * @return true = 应当展示；false = 空卡片或未知类型（调用方直接忽略，不必报 failed）
 */
bool toNotifyParams(const QJsonObject &payload, QJsonObject &out, QString *why = nullptr);

} // namespace BlackboardCard
