#pragma once

#include <QJsonObject>
#include <QString>

/* OTA 下载与周期复查（main 拆分 #16）
 *
 * 从 main.cpp 匿名域整块迁出「OTA 下载簇」：
 *   · startSelfUpdate    —— 校验护栏（http(s)/sha256/文件名白名单/300MB 上限）→ 异步下载
 *   · otaOnFinished      —— 下载结束：关文件 → 对落盘成品流式重算 sha256 → 回滚三件套 → 派重启助手 → 启安装包
 *   · otaSelfCheck       —— 周期性 HTTP 复查 /api/public/ota（把结果翻成与注册回执同形交 applyUpdateInfo）
 *   · otaStartPeriodicCheck —— 起复查定时器（默认 1800s，首次 180s）
 * 以及内部状态 g_ota*（含 2026-10-31 Range 断点续传的 g_otaExisting / g_otaAppendPending）。
 *
 * 与同域其它模块的分工：
 *   ota_state    = 状态机/路径/健康快照（#4a）
 *   ota_update   = 动作层（判版本 → 提示 → 备份 → 写 pending → 回滚）（#15）
 *   ota_download = 下载执行层（护栏 → 拉包 → 续传 → 校验 → 派安装），本模块
 *
 * 链接性约定：导出函数放全局命名空间（外部链接）—— main.cpp 的 ActionDeps 填充行
 * （&startSelfUpdate / &otaSelfCheck）与 1827/3362 等调用点**一字不改**，标识符经本头
 * 解析为外部符号。sendActionReceipt 为 main.cpp 文件作用域外部链接，本模块 extern 复用。
 */

// ── 导出函数（外部链接）──

/** 启动一次自更新（异步：立即回执 started，下载完成后回 installing/failed）。
 *  护栏：仅 http(s)、sha256 必须 64 位小写十六进制、文件名白名单、300MB 上限。 */
void startSelfUpdate(const QString &id, const QJsonObject &params);

/** 周期性复查云端清单（HTTP 轮询 /api/public/ota，结果交 applyUpdateInfo 统一判定）。
 *  失败只留痕、不打扰师生；STE_QT_OTA_DISABLE=1 时直接返回。 */
void otaSelfCheck();

/** 起周期复查定时器（STE_QT_OTA_CHECK_SEC / STE_QT_OTA_CHECK_FIRST_SEC 可调）。 */
void otaStartPeriodicCheck();

/** 是否正有下载/安装在进行（供 applyUpdateInfo 的 maybeAutoUpdate 与 otaUpdate 域判重）。
 *  注意：本函数原为匿名域 static，本次提升为外部链接，因为 ota_update.cpp 经 OtaDeps 注入的
 *  就是这个符号 —— 注入方（main.cpp）与被注入方拿到的必须是同一个实体。 */
bool otaIsBusy();
