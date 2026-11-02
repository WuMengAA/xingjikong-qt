// 星集控被控端 · 班级身份域（2026-11-02 · 「班级绑定的整合与修复」）
// ───────────────────────────────────────────────────────────────────────────
// 为什么单开一个域文件：班级身份（码 + 名 + 落盘文件）原来散在 main.cpp 里，
// 由 4 个 static 函数 + 2 个全局变量 + 7 处调用点共同维护。2026-11-02 修
// 「被移出班级后班级名残留」时发现：**同一个身份被拆成两半各清各的**，
// 一处清了码没清名，界面就自相矛盾。这类"一组状态要嘛全动要嘛不动"的逻辑
// 必须收在一处，散着写迟早再错一次。
//
// 本域负责：
//   · 码/名在两个文件（classcode.txt / classname.txt）里的读写与清空
//   · 「被云端移出班级」这一整组清理（码、名、灵动岛菜单栏）
//   · 「云端下发新绑定」的统一入口（幂等；带名才覆盖名）
//
// ── 依赖边界 ────────────────────────────────────────────────────────────
//   只依赖 QtCore + 一个可选的回调（灵动岛菜单栏同步）。不反向依赖 main.cpp，
//   路径由 init() 注入 —— 与 settings_domain / timesync_domain 同一约定。
//   全局变量 g_classCode / g_className 仍留在 main.cpp（它们是 register 报文
//   的数据源，跨域可读），本域通过 init() 拿到**引用**，不复制一份。
#pragma once

#include <QString>

namespace ClassIdentity {

/**
 * 启动时调用一次（越早越好，在 loadPersisted 之前）。
 *
 * @param configDir  数据目录（classcode.txt / classname.txt 所在处，通常
 *                   `%LOCALAPPDATA%/xingjikong`）
 * @param classCode  全局班级码的引用（域内直接读写这份真源，不另存副本）
 * @param className  全局班级名的引用
 * @param onNameChanged  班级名变化后的同步回调（灵动岛菜单栏要跟着变）。
 *                   可为 nullptr —— 无界面场景（探针/服务态）直接跳过。
 */
void init(const QString &configDir,
          QString &classCode,
          QString &className,
          void (*onNameChanged)(const QString &name));

/** 从磁盘载入（启动时调；两个文件各自独立，缺一个不影响另一个）。 */
void loadPersisted();

/** 写班级码（空值忽略：空码不是"清空"，清空走 clearAll）。 */
void saveCode(const QString &code);

/** 写班级名（空值忽略：云端没带名时不能把已有名抹掉）。 */
void saveName(const QString &name);

/** 当前班级码文件的绝对路径（设置页/诊断展示用）。 */
QString codePath();
/** 当前班级名文件的绝对路径。 */
QString namePath();

/**
 * 被云端移出班级：**整组**清掉（码 + 名 + 两个文件 + 灵动岛菜单栏）。
 *
 * 2026-11-02 的 bug 就是只清了码那一半：菜单栏一直挂着旧班名，而缩略图已经
 * 因为码为空停报 —— 界面自相矛盾；更隐蔽的是之后绑一个"云端没带名"的新班时，
 * 旧名留在 classname.txt 里 ⇒ 界面显示的是**上一个班**的名字。
 *
 * @returns 是否真的清掉了什么（用来决定要不要打日志，避免每收到一条空码就刷一行）
 */
bool clearAll();

/**
 * 云端下发一条绑定（`class-bound` 的在线推送、或 register 回执补带的码/名）。
 *
 * 语义（两条都是刻意选的行为，不是随手写的）：
 *   · 码非空才写；码与当前相同则**不动磁盘**（幂等，重连风暴不产生写放大）
 *   · 名非空才覆盖；名为空则保留旧名 —— 云端"只改码不带名"的路径不该让界面变空
 *
 * @returns 班级码是否发生了改变（true = 需要补发 register 让云端更新归属）
 */
bool applyBind(const QString &code, const QString &name);

} // namespace ClassIdentity
