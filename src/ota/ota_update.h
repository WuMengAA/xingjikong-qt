#pragma once

#include <QJsonObject>
#include <QString>

/* OTA 无人值守自更新（main 拆分 #15）
 *
 * 从 main.cpp 匿名域迁出「OTA 动作簇」：
 *   compareVersion / applyUpdateInfo / otaBackupExe / otaWritePending /
 *   otaMarkStartedOk / otaRollback
 * 以及仅簇内使用的内部件：maybeAutoUpdate / desktopIdleMs /
 *   kOtaRollbackMs / kOtaMinFreeBytes。
 *
 * 与既有 src/ota/ota_state 同域分工：
 *   ota_state = 状态机 + 路径 + 健康快照（#4a 已迁）；
 *   ota_update = 动作层（判版本 → 提示 → 备份 → 写 pending → 装 → 回滚）。
 * 执行段本体 startSelfUpdate（定义仍在 main.cpp 匿名域 3052）不在本模块内。
 *
 * 链接性约定（对齐既有 main 拆分模式）：
 *   · 导出函数放**全局命名空间**（外部链接）—— main.cpp 里 startSelfUpdate 等
 *     调用点、ActionDeps 填充行（&compareVersion）**一字不改**（标识符经本头解析为外部符号）；
 *   · startSelfUpdate / otaIsBusy（定义在 main.cpp 匿名域，内部链接）经 **OtaDeps** 注入；
 *   · 状态全局（kAppVersion / g_latestVer / g_update* / g_otaPromptedVer / g_actUpdate /
 *     g_tray / g_unconfigured）为 main.cpp 文件作用域外部链接，本模块 extern 复用；
 *     ⚠️ 其中 g_otaPromptedVer 原为文件作用域 **static**（内部链接），本刀去掉 static
 *     提升为外部链接（全仓仅 applyUpdateInfo 读写，无他处引用，行为不变）。
 */

// ── 注入上下文：本域需要但定义在别处（main.cpp 匿名域）的依赖 ──
struct OtaDeps {
    void (*startSelfUpdate)(const QString &id, const QJsonObject &params);  // 真正拉包安装（main.cpp 3052）
    bool (*otaIsBusy)();                                                    // 上一个下载任务还在跑吗（main.cpp 2939）
};
extern OtaDeps *g_otaDeps;

// 回滚判定窗口：升级后新版本这么多秒内没注册成功 → 回滚。
// ⚠️ main.cpp 启动时的回滚看门狗也读它 ⇒ 必须外部链接（C++ 命名空间 const 默认内部链接，显式 extern）。
extern const int kOtaRollbackMs;

// ── 导出函数（外部链接；main.cpp 与 executeAction 经同名调用）──
int     compareVersion(const QString &a, const QString &b);
void    applyUpdateInfo(const QJsonObject &pay);
QString otaBackupExe();                                        // 备份当前 exe（回滚命根子），失败返回空串
void    otaWritePending(const QString &prevExe, const QString &targetVer);
void    otaMarkStartedOk();                                    // 新版起来并注册成功 → 清 pending
void    otaRollback();                                         // 用备份覆盖自己并拉起旧版

// ── 装配（main() 调用一次）──
void otaUpdateInit(const OtaDeps &deps);
