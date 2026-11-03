// 启动耗时埋点（2026-11-03 · 用户工作笔记序号 3「启动慢」）。
//
// ── 为什么单独成域而不是写在 main.cpp ─────────────────────────────────
// main.cpp 有「只降不升」台账（4399 行，门禁 check-main-size.mjs 硬拦）。
// 埋点本身是横切观察能力，不属于装配，也不属于任何业务域 —— 按 §10.2
// 「新代码必须进职能目录」放进 src/system/，main 只留一行标记调用。
//
// ── 判读方法（真机排查时看 %LOCALAPPDATA%\xingjikong\agent-qt.log）──────
//   `[boot]  <阶段名>  累计 NNNN ms（本段 MM ms）`
//   「本段」= 距上一次标记的耗时，用来定位是哪一段拖慢的；
//   「累计」= 距 QApplication 构造完成的总耗时。
// ⚠️ t0 只能取在 QApplication 构造之后：之前那段（Qt 平台插件/风格插件加载）
//    由 Qt 自己决定，本程序无从插手，量了也改不了 —— 量不到的不算进指标。
#ifndef STELARITH_SYSTEM_BOOT_TRACE_H
#define STELARITH_SYSTEM_BOOT_TRACE_H

#include <QElapsedTimer>
#include <QString>

/** 全局唯一的启动计时器。第一次 mark() 时自动 start()。 */
class BootTrace
{
public:
    /** 打一条 `[boot] <阶段名> 累计 … ms（本段 … ms）` 到日志。
     *  @param stage 阶段名（ASCII，便于日志里对齐列宽） */
    static void mark(const char *stage);

    /** 距启动（首次 mark）的毫秒数；未启动时为 0。 */
    static qint64 elapsedMs();
};

#endif   // STELARITH_SYSTEM_BOOT_TRACE_H
