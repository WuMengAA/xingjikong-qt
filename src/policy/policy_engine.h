#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include "../action/execute_action.h"   // SchedTask / SoftPolicy（ActionDeps 共享类型）

/* 本机安全策略引擎（main 拆分 #14）
 *
 * 从 main.cpp 匿名域迁出三簇：
 *   ① 调度（定时关机/重启）：saveSchedules / loadSchedules / schedFireAllowed /
 *      fireScheduledTask / armScheduleTimer / reschedulePersisted +
 *      g_schedFile / g_schedDir / g_schedTimerById / g_schedTimers；
 *   ② 软控（黑白名单/进程冻结/去网/守护）：softNorm / softHit / saveSoftPolicy /
 *      loadSoftPolicy / setProcessFrozen / setNetBlocked / softGuardTick +
 *      g_softPolicyFile / g_soft / g_softGuard / g_frozenPids；
 *   ③ 保护名单：kProtectedSelf / kProtectedSystem / kProtectedSecurity + protectedCategory。
 *
 * 链接性约定（对齐既有 main 拆分模式）：
 *   · 导出函数与状态全局放**全局命名空间**（外部链接）—— main.cpp 的 ActionDeps 填充行
 *     一字不改（标识符经本头声明解析为外部符号，同 #13 的 proc_inspect）；
 *   · execute_action.cpp 经 g_actionDeps 函数指针/全局指针访问（**零改动**）；
 *   · enableShutdownPriv（定义仍在 main.cpp 匿名域，内部链接）经 PolicyDeps 注入；
 *   · snapshotProcessNames 已迁 src/core/proc_inspect（外部链接），本模块直接 include 调用；
 *   · 内部函数（fireScheduledTask / reschedulePersisted / softNorm / loadSoftPolicy /
 *     softGuardTick）与内部状态（g_schedDir / g_schedTimers / g_softGuard / kProtected*）
 *     留在本 .cpp 内做文件内 static（不暴露）。
 *
 * QTimer 线程亲和：armScheduleTimer 创建的定时器 setParent(qApp)、g_softGuard 的 parent 由
 * main() 传入（&app），均在主线程创建/回调 —— 与 execute_action（主线程）一致。
 */

// ── 注入上下文：本域需要但定义在别处（main.cpp 匿名域）的依赖 ──
struct PolicyDeps {
    bool (*enableShutdownPriv)();   // 提升关机权限（定义仍在 main.cpp 匿名域，内部链接）
};
extern PolicyDeps *g_policyDeps;

// ── 导出函数（外部链接，供 main.cpp ActionDeps 填充 / 接线）──
void saveSchedules(const QVector<SchedTask> &tasks);
QVector<SchedTask> loadSchedules();
bool schedFireAllowed();
void armScheduleTimer(const SchedTask &t, int delayMs);
void saveSoftPolicy();
bool softHit(const QStringList &list, const QString &exeName);
bool setProcessFrozen(quint32 pid, bool frozen);
bool setNetBlocked(const QString &exePath, bool block);
QString processPathOf(quint32 pid);
QString protectedCategory(const QString &exeName);

// ── 状态全局（外部链接；main.cpp extern 后取地址注入 ActionDeps）──
extern QHash<QString, QTimer *> g_schedTimerById;
extern QString g_schedFile;
extern SoftPolicy g_soft;
extern QString g_softPolicyFile;
extern QSet<quint32> g_frozenPids;

// ── 装配（main() 调用）──
void policyInit(const PolicyDeps &deps);                          // 存注入上下文（main() 早期）
void policySetSchedDir(const QString &dir);                       // 设 g_schedDir + g_schedFile
void policyStart(const QString &softPolicyFile, QObject *parent); // 软控路径 + loadSoftPolicy + g_softGuard 接线
