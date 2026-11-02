// 星集控被控端 · 插件可用指令白名单（2026-11-02 · 「插件大升级」第 3 项）
// ───────────────────────────────────────────────────────────────────────────
// 补一个**画了两年饼**的权限：`cmd:exec` 从 2026-10-14 起就活在
// `PluginHost::kKnownPermissions` 与 `Stelarith-shared-qml/scripts/UiContract.js`
// 的权限清单里，但全库**没有任何实现** —— 插件在 manifest 里声明了也拿不到，
// 调用就回"未知 api"。这类"清单里有、代码里没有"的权限比明说没有更糟：
// 插件作者照文档写完，上线才发现永远不生效，而且不报错。
//
// ── 为什么是白名单，而不是把 executeAction 直接给插件 ────────────────────────
// 内建指令有一百多条，含 reboot / process_stop / soft_deny_net / file_push 这类
// 能直接影响一台教室机的动作。把它们无差别开放给第三方，等于让"装个课表小工具"
// 拥有和学校管理端同等的破坏力。所以这里只放**四类只读或无害**的：
//
//   ping          连通性自检（插件作者最常用的"我还活着吗"）
//   log_tail      读日志线索（内部已限行数，且只读本机）
//   process_list  只读进程列表（条数已由 kProcessListLimit 限制）
//   time_status   当前时间/校时状态（课表类插件几乎必用）
//
// 新增一条进白名单前，先回答两个问题：①它能不能改变这台机器的状态？
// ②它泄露的信息超出"这台机器现在什么情况"了吗？两个都答"否"才放。
//
// 白名单放在这里而不是 pluginhost：**哪些指令能给第三方是产品决策**，
// 改产品策略不该改 ABI。pluginhost 只管"你有没有 cmd:exec 权限"。
#pragma once

#include <QString>
#include <QJsonObject>
#include "pluginhost.h"    // PluginResult

namespace PluginActionWhitelist {

/** 当前放行的指令名（顺序即错误提示里的展示顺序）。 */
const QStringList &allowed();

/** 该指令是否放行。 */
bool isAllowed(const QString &action);

/**
 * 执行一条插件请求的指令。
 * @returns PluginResult；不在白名单时 result="denied"、error 写清可用清单。
 */
PluginResult run(const QString &action, const QJsonObject &params);

} // namespace PluginActionWhitelist
