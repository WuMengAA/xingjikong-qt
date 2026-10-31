#pragma once
#include <QVariantMap>

// P3 记录页的**结构化视图**（2026-10-31 被控端「组件显示雏形」批次）。
//
// ── 为什么另起一个文件，而不塞进 p3_reader.cpp ──────────────────────────
// p3_reader.cpp 已经 214 行（红线 300），再塞汇总+行构造会顶到 290 上下，
// 下次谁再加一行就判红。职责本身也不同：
//   p3_reader     = 「怎么读文件」（readJsonlDir 一族，纯 IO + 历史文本口径）
//   p3_record_view = 「读出来给界面看什么形状」（按天/按次汇总成行）
//
// ── 返回形状 ────────────────────────────────────────────────────────────
//   { "summary": QString,                       // 顶部一句话（"共 3 天 · 128 条采样"）
//     "rows":    QVariantList<QVariantMap>      // 每行 { label, value }，已按时间倒序
//   }
//
// ⚠️ QML 侧**不做**分组/排序/统计（被控端 QML 零业务逻辑铁律）——
//    这里给的就是最终要上屏的两列文本，QML 只负责画。
QVariantMap buildNoiseRecordView();
QVariantMap buildStudyRecordView();

/**
 * 「view 字典 → AgentRecordUi」的拆解入口（2026-10-31 二改）。
 *
 * 为什么放在本文件而不是 agent_panel.cpp 里就地拆：
 *   agent_panel.cpp 已 645 行 / 红线 600 且不在白名单（新增超限），**一行都不能加**。
 *   而拆解要把 {summary, rows, failReason, failImpact} 四个键分别喂给 apply* 的四个形参，
 *   就地写至少 4 行。下沉到这里后，调用点仍是两行。
 *
 * 本头**不 include** agent_ui.h（避免把 776 行的 UI 声明拖进每个用它的 TU）；
 * 故用前向声明，.cpp 里再 include。
 */
class AgentUi;
void applyNoiseRecordView(AgentUi *ui);
void applyStudyRecordView(AgentUi *ui);
