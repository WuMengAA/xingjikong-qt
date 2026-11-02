#pragma once
// agent_panel 内部共享前置（2026-11-02 · 屎山整理 #3）
//
// 拆出缘由：agent_panel.cpp 曾 646 行超红线 600。它不是类方法，而是两个自由函数
// （agentUiFill 145-271 / agentUiWire 273-645），其中 agentUiWire 本身就有 373 行。
// 天然切口只有一处：**"产出数据"与"接信号"分离** ——
//   · agent_panel_data.cpp：三个 builder（自检项 / 插件列表）+ 插件商店条目缓存 + agentUiFill
//   · agent_panel.cpp     ：只剩 agentUiWire（信号接线）
//
// 本文件只放两边都要用的 include 与 extern，避免同一份 extern 清单抄两遍
// —— 抄两遍必然漂移：某天有人只在一处加了 g_xxx，另一处编译报未声明，然后
// 有人图省事改成 static 或者干脆复制一份状态，那才是真正的屎。
//
// ⚠️ 这里的 extern 全部指向 main.cpp 的文件作用域变量/函数（同 g_uid 模式）。
//    agent_ui.h 是 AUTOMOC 单独编译的 Q_OBJECT 头，**不能**引用这些符号，
//    所以它们只能留在本内部头里，不能塞进 agent_ui.h。

#include <QWebSocket>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QAbstractSocket>

#include "agent_panel.h"                // TrayState / AgentPanelDeps / g_panelDeps（公共契约）
#include "../agent_ui.h"                // AgentUi（friend 声明在 agent_ui.h）+ AgentShell
#include "../core/AgentSettings.h"       // AgentSettings（本地功能开关）
#include "../modules/modulehost.h"       // ModuleHost（内置模块层）
#include "../island/island.h"            // IslandOverlay（岛联动）
#include "../media/MediaSession.h"       // MediaSession（播放时段判定）
#include "../domain/settings_domain.h"   // SettingsDomain（存储/备份/重置）
#include "../domain/timesync_domain.h"   // TimesyncDomain（时间配置）
#include "../ota/ota_state.h"            // otaPaused/otaLoadState（升级状态快照）
#include "../ota/channel_persistence.h"  // channelFilePath/persistChannel（更新渠道）
#include "../p3log/p3_reader.h"          // buildNoiseLogText/buildStudyLogText/openFolder
#include "../p3log/p3_record_view.h"     // buildNoiseRecordView/buildStudyRecordView（记录页行）
#include "../system/winlauncher.h"       // stelarithHideChildConsole（插件解压静默）
#include "../plugin/pluginhost.h"        // PluginHost（g_pluginHost 类型）

// ── 复用 main 的状态全局（提升文件作用域外部链接，同 g_uid/agentEnvPath 模式）──
extern const char *kAppVersion;
extern QString g_uid;
extern QWebSocket *g_ws;
extern bool g_registered;
extern QString g_logPath;
extern bool g_configured;
extern bool g_reconnectNow;
extern bool g_updateChecked;
extern QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;
extern bool g_updateMandatory;
extern QString g_channel;
extern int g_backoffMs;
extern QString g_classCode;
extern QString g_deviceRole;
extern PluginHost *g_pluginHost;

// ── 复用 main 已提升文件作用域的开机自启/桌面快捷方式函数（main 拆分 #9）──
extern bool agentAutostartEnabled();
extern bool agentSetAutostart(bool on);
extern bool agentDesktopShortcutExists();
extern bool agentSetDesktopShortcut(bool on);

// agentEnvPath：main.cpp 前向声明，定义见其文件末尾 namespace 之后。
extern QString agentEnvPath();

// ── 两个 TU 之间共享的插件商店缓存 ──
// id -> 云端条目（pluginsRefreshRequested 刷新，pluginInstallRequested 取 downloadUrl，
// buildPluginList 合并）。定义在 agent_panel_data.cpp，出访问器而不是 extern 变量：
// 变量跨 TU 暴露后谁都能清空它，而清空会让「已装插件」从界面上集体消失。
QHash<QString, QJsonObject> &agentPanelStoreEntries();

// 定义在 agent_panel_data.cpp（agentUiWire 里刷新插件列表时要用 URL）。
QString agentPanelPluginRegistryUrl();
