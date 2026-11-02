// P3 播放时段管理 / 调度 / 软控 / 文件接收 / 进程 / 终端指令 —— main 拆分 #10（2026-10-30）
// executeAction 自 main.cpp（原 3777-5715：1938 行）迁出。函数/注释逐字保留，
// 仅把匿名域符号（内部链接）改为经 g_actionDeps 访问（方案 B）：
//   19 个全局量 g_X → (*g_actionDeps->memberX)；20 个函数 fn(...) → g_actionDeps->fn(...)。
// 函数名/参数不变、语义等价；main() 负责填充 g_actionDeps（同一 TU 才能取匿名域符号地址）。
//
// 为什么用依赖注入而不提升文件作用域：符号散布（文件接收/软控/调度/自习/窗口快照各成一簇），
// 整簇外迁侵入大；方案 B 只改本文件调用点，改动集中、可回退。
//
// 状态全局（g_currentFps/g_logPath/g_mediaDir/g_pluginHost/g_remoteControl/g_shotDir/
// g_vncMegaDone/g_vncSessionStartMs/g_studyRecordSec/g_studyStreak/g_studyStreakNeed/
// g_studyThreshDb/g_studyAlertCount）仍驻 main（文件作用域外部链接），按既有 extern 模式复用
// （同 g_uid/agentEnvPath 模式，见 remote_control）。

#include "execute_action.h"

// 域分发协议与各域入口声明（2026-11-02 拆分）：共享头同时集中了原文件那份 40 行公共
// include 与 extern 全局块。此处显式包含是为了拿到 6 个 executeXxxAction 的原型 ——
// 否则本 TU 只见到 execute_action.h，编译期报「找不到标识符」。
#include "execute_action_shared.h"

#include "../core/ExecOut.h"        // ExecOut（指令执行结果）
#include "../core/AgentSettings.h"  // AgentSettings（本地功能开关）
#include "../modules/modulehost.h"  // ModuleHost（内置模块层）
#include "../plugin/pluginhost.h"   // PluginHost（g_pluginHost 类型）+ PluginResult
#include "../island.h"              // IslandOverlay（岛联动）
#include "../vnc_server.h"          // VncServer（远控服务端）
#include "../vnc_bridge.h"          // VncBridge（键鼠桥接）
#include "../annotate_domain.h"     // Overlay（批注/白板）
#include "../alarm_domain.h"        // AlarmDomain（闹钟域）
#include "../notify_domain.h"       // NotifyDomain（定时通知域）
#include "../timesync_domain.h"     // TimesyncDomain（时间同步域）
#include "../ppt_domain.h"          // PptDomain（PPT 放映控制）
#include "../automation_engine.h"   // AutomationEngine（规则引擎）
#include "../noise_monitor.h"       // NoiseMonitor（噪音采集器）
#include "../p3log/p3_log.h"        // logStudyStart/logStudyStop（自习长期记录）
#include "../remote/remote_control.h" // stopRemoteControlSession（远控收口）
#include "../media/H264Streamer.h"    // A档（2026-10-31）：远控期间并起 H264 推流（与 VNC 同生命周期）
#include "../notify/notify_entry.h" // islandShow（岛气泡）
#include "../media/MediaSession.h"  // MediaSession（播放时段判定）
#include "../panel_domain.h"        // PanelDomain（岛组件面板）
#include "../media/CameraActions.h" // CameraActions（相机六件套）
#include "../study_mode/study_mode.h" // StudyMode（自习模式）
#include "../system_volume.h"       // setSystemMasterVolume（系统音量）
#include "../proc_guard.h"          // terminateBlockedReason（进程终止护栏）

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

ActionDeps *g_actionDeps = nullptr;

ExecOut executeAction(const QString &action, const QJsonObject &params)
{
    ExecOut out;

    // ── 内置模块前置路由（2026-10-16）：官方模块**优先于**进程外插件认领 ──
    // 顺序理由：内置模块是官方能力（U盘/联动/岛界面），第三方插件只能补它没覆盖的洞；
    // 反过来会让第三方插件有机会顶掉官方实现（"装了个插件，U 盘提示就变了"）。
    {
        ModuleResult mr;
        if (ModuleHost::instance()->dispatch(action, params, mr)) {
            out.result = mr.result;
            out.error = mr.error;
            out.data = mr.data;
            return out;
        }
    }

    // ── P2 插件前置路由：插件 register 的 action 优先走插件宿主（本地 WS 同步调用）──
    if (g_pluginHost) {
        PluginResult pr;
        if (g_pluginHost->dispatch(action, params, pr)) {
            out.result = pr.result;
            out.error = pr.error;
            out.data = pr.data;
            return out;
        }
    }

    // ── P3 第三方能力友好提示 ─────────────────────────────────────
    // 插件前置路由没接住的能力名（如广播/点歌/歌词），说明对应第三方插件未安装。
    // 给管理端一条明确的"去哪装"提示，而不是默默 failed（把"能力外迁"落到实处）。
    static const QStringList kThirdPartyCaps = {
        QStringLiteral("voicehub_broadcast"), QStringLiteral("song_request"),
        QStringLiteral("lyrics"), QStringLiteral("broadcast_audio")
    };
    if (kThirdPartyCaps.contains(action)) {
        islandShow(QStringLiteral("需要安装插件"),
                    QStringLiteral("「广播 / 点歌 / 歌词」等能力由第三方插件 VoiceHub 广播提供。\n请到托盘菜单「插件市场…」安装该能力后重试。"),
                    QStringLiteral("puzzle"), 9000, false, QString(),
                    IslandOverlay::Form::Centered, false);
        out.result = QStringLiteral("needs-plugin");
        out.error = QStringLiteral("未安装 VoiceHub 广播插件（请到插件市场安装）");
        return out;
    }

    // ── 以下按域分发（2026-11-02 拆分）：各域只管自己的指令，handled=false 表示不认领 ──
    // 每域各占一个花括号作用域：各自的 ExecOut r 互不干扰，也不污染下面的本地量。
    {
        bool handled = false;
        ExecOut r = executeDeviceAction(action, params, handled);
        if (handled) return r;
    }
    {
        bool handled = false;
        ExecOut r = executeAppProcessAction(action, params, handled);
        if (handled) return r;
    }
    {
        bool handled = false;
        ExecOut r = executeScheduleAction(action, params, handled);
        if (handled) return r;
    }
    {
        bool handled = false;
        ExecOut r = executeNoticeAction(action, params, handled);
        if (handled) return r;
    }
    {
        bool handled = false;
        ExecOut r = executeMediaAction(action, params, handled);
        if (handled) return r;
    }
    {
        bool handled = false;
        ExecOut r = executeFileAction(action, params, handled);
        if (handled) return r;
    }


    /* ══════════ 批次 1：教室刚需（探针 / 音量 / 截图 / 开程序 / 进程查看与结束 / 日志尾） ══════════ */


    /* ══════════ 批次 2：全量补齐（调度器 / 相机 / 媒体 / 远控 / 文件推送） ══════════ */


    // ── 批注 / 白板（2026-10-27 · H 批 · 替代 ICC CE）────────────────────
    // 用户 00:42 裁决："集成 ICC CE 相关元素，代替 ICC CE 的画图地位…成为我们软件的一部分"。
    // 一条前缀分支收全部子指令，避免再往主文件塞 N 段 if（main.cpp 已 8550 行）。
    //   annotate_start{board}  显示批注层（board 空/transparent=透明批注；white/black/green/#RRGGBB=白板）
    //   annotate_stop          隐藏（穿透态 = hide，不切 window flags）
    //   annotate_status        只读状态
    //   annotate_pen|highlighter|eraser / annotate_tool{tool} / annotate_color{color} / annotate_width{width}
    //   annotate_undo / annotate_redo / annotate_clear / annotate_page{key} / annotate_board{board}
    //   annotate_page_add / annotate_page_next / annotate_page_prev（H2 白板多页）
    // 本地热键（窗口无焦点，走低层键盘钩子）：ESC 收层 / Ctrl+Z 撤销 / Ctrl+Y 重做 /
    // Ctrl+Shift+Z 重做 / Ctrl+E 清空 / Ctrl+PgUp·Ctrl+PgDn 翻批注页
    //（裸 PageUp/PageDown 不抢 —— 那两个键在 PPT 放映里是翻课件）。
    if (action.startsWith(QStringLiteral("annotate_"))) {
        auto *ov = AnnotateDomain::Overlay::instance();
        const QString sub = action.mid(9);      // "annotate_" = 9 字符
        QString err;

        if (sub == QStringLiteral("start")) {
            if (!ov->start(&err)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("annotate_start: ") + err;
                return out;
            }
            ov->setBoard(params.value(QStringLiteral("board")).toString());
        } else if (sub == QStringLiteral("stop")) {
            ov->stop();
        } else if (sub == QStringLiteral("status")) {
            // 只读，落到下面统一回执
        } else {
            AnnotateDomain::Canvas *c = ov->canvas();
            if (!c) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("批注层还没启动（先发 annotate_start）");
                return out;
            }
            if (sub == QStringLiteral("pen") || sub == QStringLiteral("highlighter")
                || sub == QStringLiteral("eraser")) {
                c->setTool(sub);
            } else if (sub == QStringLiteral("tool")) {
                c->setTool(params.value(QStringLiteral("tool")).toString());
            } else if (sub == QStringLiteral("color")) {
                c->setColor(params.value(QStringLiteral("color")).toString());
            } else if (sub == QStringLiteral("width")) {
                c->setWidth(params.value(QStringLiteral("width")).toDouble(4.0));
            } else if (sub == QStringLiteral("undo")) {
                c->undo();
            } else if (sub == QStringLiteral("redo")) {
                c->redo();
            } else if (sub == QStringLiteral("clear")) {
                c->clearAll();
            } else if (sub == QStringLiteral("page")) {
                c->setPageKey(params.value(QStringLiteral("key")).toString());
            } else if (sub == QStringLiteral("page_add")) {
                c->addPage();       // 白板多页：末页之后新建并切过去
            } else if (sub == QStringLiteral("page_next")) {
                c->nextPage();
            } else if (sub == QStringLiteral("page_prev")) {
                c->prevPage();
            } else if (sub == QStringLiteral("board")) {
                ov->setBoard(params.value(QStringLiteral("board")).toString());
            } else if (sub == QStringLiteral("follow")) {
                // I3：批注跟 PPT 翻页开关（默认开；白板模式下内部自动不跟）
                ov->setFollowPpt(params.value(QStringLiteral("on")).toBool(true));
            } else {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("未知批注子指令：%1（支持 start/stop/status/pen/highlighter/"
                                           "eraser/tool/color/width/undo/redo/clear/page/page_add/"
                                           "page_next/page_prev/board/follow）").arg(sub);
                return out;
            }
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), ov->isActive());
        out.data.insert(QStringLiteral("board"), ov->boardBg());
        out.data.insert(QStringLiteral("status"), ov->statusLine());
        // 结构化字段（管理端面板不许去解析 status 字符串：那是给人看的，不是协议）
        if (AnnotateDomain::Canvas *ac = ov->canvas()) {
            out.data.insert(QStringLiteral("tool"), ac->tool());
            out.data.insert(QStringLiteral("color"), ac->colorHex());
            out.data.insert(QStringLiteral("width"), ac->penWidth());
            out.data.insert(QStringLiteral("strokes"), ac->strokeCount());
            out.data.insert(QStringLiteral("page"), ac->pageKey());
            out.data.insert(QStringLiteral("pageIndex"), ac->pageIndex());
            out.data.insert(QStringLiteral("pageCount"), ac->pageCount());
        }
        return out;
    }


    // ── 定时自动化规则引擎（2026-10-27 Phase 3）：管理端下发 / 拉取 ────────
    // 纯转发壳：规则的真执行（notify/音量/截图/锁屏/启动程序）由 fireRule 经
    // actionRequested → executeAction 复用既有词表完成，这里**只**负责规则的 CRUD。
    // 若引擎未初始化（极端：构造早于引擎），如实回 failed，不静默吞。
    if ((*g_actionDeps->automationEngine)) {
        static const QStringList kAuto = {
            QStringLiteral("automation_list"), QStringLiteral("automation_upsert"),
            QStringLiteral("automation_remove"), QStringLiteral("automation_toggle"),
            QStringLiteral("automation_run")
        };
        if (kAuto.contains(action)) {
            if (action == QStringLiteral("automation_list")) {
                out.data = QJsonObject{{ QStringLiteral("rules"), (*g_actionDeps->automationEngine)->listRules() }};
                out.result = QStringLiteral("ok");
                return out;
            }
            if (action == QStringLiteral("automation_upsert")) {
                const QJsonObject rule = params.value(QStringLiteral("rule")).toObject();
                out.result = (*g_actionDeps->automationEngine)->upsertFromRemote(rule)
                                 ? QStringLiteral("ok") : QStringLiteral("failed");
                if (out.result == QStringLiteral("failed"))
                    out.error = QStringLiteral("automation_upsert：规则非法（缺 id 或字段无法解析）");
                return out;
            }
            const QString rid = params.value(QStringLiteral("id")).toString();
            if (action == QStringLiteral("automation_remove")) {
                out.result = (*g_actionDeps->automationEngine)->removeRule(rid)
                                 ? QStringLiteral("ok") : QStringLiteral("failed");
                if (out.result == QStringLiteral("failed"))
                    out.error = QStringLiteral("automation_remove：未找到规则 id=%1").arg(rid);
                return out;
            }
            if (action == QStringLiteral("automation_toggle")) {
                const bool en = params.value(QStringLiteral("enabled")).toBool(true);
                out.result = (*g_actionDeps->automationEngine)->toggleRule(rid, en)
                                 ? QStringLiteral("ok") : QStringLiteral("failed");
                if (out.result == QStringLiteral("failed"))
                    out.error = QStringLiteral("automation_toggle：未找到规则 id=%1").arg(rid);
                return out;
            }
            if (action == QStringLiteral("automation_run")) {
                (*g_actionDeps->automationEngine)->runNow(rid);   // 仍判条件，通过点才点火
                out.result = QStringLiteral("ok");
                return out;
            }
        }
    }

    out.result = QStringLiteral("failed");
    out.error = QStringLiteral("未知指令：%1").arg(action);
    return out;
}
