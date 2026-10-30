#pragma once
#include <QString>
#include <QJsonObject>

// OTA 状态机 + 健康快照上报（M3，2026-10-20）。
// 2026-10-30 main 拆分 #4a：otaBaseDir/路径/otaLoadState/otaSaveState/otaRetryDelayMs/
// otaNoteFailure/otaNoteSuccess/otaPaused/otaReportHealth 从 main.cpp 迁出。
// 依赖 main 的文件作用域符号（g_latestVer / g_channel / kAppVersion / sendEvent）按既有
// extern 模式复用；无人值守自更新执行段（startSelfUpdate/doSelfUpdate/otaRollback 等）仍留 main。

QString otaOkPath();      // 数据目录 ota-last-ok.txt（注册成功标记，回滚判定）
QString otaBaseDir();     // 数据目录 ota（下载/状态/备份都在这里）
QString otaPrevPath();    // 数据目录 prev/stelarith-agent-qt.exe（回滚备份）
QString otaPendingPath(); // 数据目录 ota-pending.json（看门狗标记）
QString otaPausePath();   // 数据目录 ota-pause（人工紧急刹车空文件）

QJsonObject otaLoadState();
bool otaPaused();
void otaNoteFailure(const QString &why);
void otaNoteSuccess();
void otaReportHealth(const QString &why);