// 星集控被控端 · 设置域（2026-10-28 模块三 · 设置页按原文补全）
// ───────────────────────────────────────────────────────────────────────────
// 为什么单开一个域文件：main.cpp 已是 216 个顶层定义（file-size 台账 P1 判红），
// 往上叠"安全模式 / 存储统计 / 备份 / 重置"会让下次拆刀更难。项目既定做法是
// 把可独立的功能整块下沉（alarm_domain / notify_domain / timesync_domain /
// ppt_domain / annotate_domain 全是这么拆的），这里沿用。
//
// 本域只管**通用设置**里那四件跟进程生命周期/磁盘有关的事：
//   ① 安全模式（意外崩溃防护）—— 运行标记 + 上次异常退出判定
//   ② 存储用量统计（配置/数据/日志三个目录）
//   ③ 配置备份（把关键文件复制到一个带时间戳的目录）
//   ④ 重置设置（删配置，不动数据/日志）
// 时间配置不在这里：那是 TimesyncDomain 的既有能力，设置页直接调它。
//
// ── 依赖边界 ────────────────────────────────────────────────────────────
//   只依赖 QtCore；不反向依赖 main.cpp（路径由 init() 注入）。
//   不写 QSettings：开关的持久化归 AgentSettings（QSettings 口径统一在那边）。
#pragma once

#include <QString>
#include <QtGlobal>

namespace SettingsDomain {

/**
 * 启动时调用（越早越好）：
 *   · 若上次的运行标记还在 ⇒ 上次**没有走有序退出**（崩溃/断电/被杀）⇒ 记下异常
 *   · 写新的运行标记（含 pid/启动时刻，便于人工排查）
 *   · 挂 aboutToQuit ⇒ 有序退出时删标记
 * @param dataDir 数据目录（日志/配置都在这棵树下）
 */
void init(const QString &dataDir);

/** 上一次进程是否异常退出（首次安装/上次正常 ⇒ false）。 */
bool lastExitAbnormal();

/** 本次是否运行在安全模式：上次异常退出 且 用户开了「异常后自动进入」，
 *  或本次开机后由人手动进入。安全模式下不加载第三方插件。 */
bool safeModeActive();
void setSafeModeActive(bool on);

/** 上次退出的一句话描述（直接上屏）。 */
QString lastExitText();

/** 递归统计目录占用字节数。目录不存在返回 0。大目录会有几十~几百 ms，别在滚动时调。 */
qint64 dirSizeBytes(const QString &path);

/** 人类可读体积："1.2 MB" / "345 KB" / "0 B"。 */
QString humanSize(qint64 bytes);

/**
 * 把配置备份到 `dir`（调用方给一个新建好的空目录）。
 * 备份内容：agent.env、devrole.txt、classcode.txt、channel.txt、timesync.json、
 *          以及插件目录清单（不复制插件本体，只记清单，便于恢复时对账）。
 * @returns 成功的文件数；0 表示没备份到任何东西（见 *err）。
 */
int backupConfig(const QString &dataDir, const QString &dir, QString *err);

/** 清理旧日志（log.1 / log.2 … 与 30 天前的 .log）。返回释放字节数。 */
qint64 clearOldLogs(const QString &dataDir);

/**
 * 重置设置：删除 agent.env 与配置类文件（**不删日志、不删数据、不删设备身份**）。
 * 调用方负责重启进程；本函数不重启。
 * @returns 删掉的文件数。
 */
int resetSettings(const QString &dataDir);

} // namespace SettingsDomain
