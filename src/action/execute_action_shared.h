// 星集控被控端 · executeAction 各域分发共享前置（2026-11-02 · 屎山整理）
//
// 背景：AGENTS.md §10.5 文件规模红线（.cpp ≤ 600），而 src/action/execute_action.cpp
// 被门禁判为「新增超限 2029 行」——它是 **一个函数里的 54 个指令分支**（另有 5 条
// automation_ CRUD 按设计留下，归规则引擎不属任何域），随产品演进步步膨胀。
// 本轮按域机械拆成 6 个文件，**只搬家不改逻辑**（每块体逐字保留）。
//
// 拆分规模（实测，见根仓 _audit_split.py 的五项断言；块数按域计共 54）：
//   execute_action.cpp     318 行（主分发 + automation_ CRUD 5 条）
//   execute_device.cpp     328 行 /  8 块      execute_app.cpp      300 行 /  5 块
//   execute_schedule.cpp   286 行 /  7 块      execute_notice.cpp   420 行 / 13 块
//   execute_media.cpp      349 行 / 18 块      execute_file.cpp     241 行 /  3 块
//
// 为什么不按 "helper/util/manager" 这种横切词拆：那些词不划分责任边界，拆完仍然是一个
// 巨函数的碎片。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。
//
// 各域之间的调用协议（最简单的那个，够用就不过度设计）：
//     bool handled = false;
//     ExecOut r = executeXxx(action, params, handled);
//     if (handled) return r;      // handled=false ⇒ 这条指令不归我管，继续往下问
//
// 为什么用 `handled` 出参而不是让每域返回 bool：分支体里 `return out` 有 140 余处，
// 逐字保留它们是最强的等价性保证；包一层 `handled = true; return out;` 只在每块的
// **最外层**改一次，比改写 140 个 return 安全得多。

#pragma once

#include "execute_action.h"        // ActionDeps / SchedTask / SoftPolicy / WinCache
#include "../core/ExecOut.h"       // ExecOut 完整定义（各域均按值返回它）
#include "../core/AgentSettings.h"

// ── 与原 execute_action.cpp 逐字一致的公共头集合 ──
// 拆出的每个域都复用同一份，避免"漏一个 include = 某个域编不过"这种拆分期专属故障。
#include "../modules/modulehost.h"
#include "../plugin/pluginhost.h"
#include "../island/island.h"
#include "../remote/vnc_server.h"
#include "../remote/vnc_bridge.h"
#include "../annotate/annotate_domain.h"
#include "../domain/alarm_domain.h"
#include "../domain/notify_domain.h"
#include "../domain/timesync_domain.h"
#include "../ppt/ppt_domain.h"
#include "../automation/automation_engine.h"
#include "../noise/noise_monitor.h"
#include "../p3log/p3_log.h"
#include "../remote/remote_control.h"
#include "../media/H264Streamer.h"
#include "../notify/notify_entry.h"
#include "../media/MediaSession.h"
#include "../domain/panel_domain.h"
#include "../media/CameraActions.h"
#include "../study_mode/study_mode.h"
#include "../media/system_volume.h"
#include "../system/proc_guard.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QProcess>
#include <QUrl>
#include <QImage>
#include <QPixmap>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QVariantList>
#include <QVariantMap>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QMessageBox>
#include <QGuiApplication>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QWebSocket>
#include <QScreen>

// ── 复用 main 的文件作用域外部链接全局（同 g_uid / agentEnvPath 模式）──
// 拆分前这份 extern 块只写在 execute_action.cpp 里；现在每个域的 .cpp 都要用，
// 因此集中到本头。**内容与拆分前逐字一致**（少了它链接期报 LNK2001）。
extern int g_currentFps;
extern qint64 g_lastVncDownMs;
extern QString g_logPath;
extern QString g_mediaDir;
extern PluginHost *g_pluginHost;
extern bool g_remoteControl;
extern QString g_shotDir;
extern bool g_vncMegaDone;
extern qint64 g_vncSessionStartMs;
extern int g_studyRecordSec;
extern int g_studyStreak;
extern int g_studyStreakNeed;
extern double g_studyThreshDb;
extern int g_studyAlertCount;

// 各域入口。**声明放在同一个头**，是为了让 execute_action.cpp 一处就能看全自己往下
// 问了谁——散在 5 个头里，下一个人容易漏掉某个域（漏了 = 那批指令静默失效）。
ExecOut executeDeviceAction(const QString &action, const QJsonObject &params, bool &handled);
ExecOut executeAppProcessAction(const QString &action, const QJsonObject &params, bool &handled);
ExecOut executeNoticeAction(const QString &action, const QJsonObject &params, bool &handled);
ExecOut executeScheduleAction(const QString &action, const QJsonObject &params, bool &handled);
ExecOut executeMediaAction(const QString &action, const QJsonObject &params, bool &handled);
ExecOut executeFileAction(const QString &action, const QJsonObject &params, bool &handled);
