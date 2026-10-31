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

    // ── dev_act：仅开发用的动作触发器（2026-10-17 T 档验证设施）──────────────
    // 为什么需要：T 档要验"多种进行中状态在岛上并存"，而岛的状态源分散在
    //   通知 / 文件传输 / 媒体会话 / 对讲频谱 / 展开收起 等模块里，**没有一条统一的入口**；
    //   用云端指令触发要连真云端、用插件口又碰不到媒体与频谱。这里给沙箱一条内建口令。
    // ⚠️ 唯一的护栏：**必须 STE_QT_UID 以 `ste-sbx-` 开头**（沙箱实例专用命名前缀），
    //   生产实例即便被下发 dev_act 也只会回 failed，不会执行。这是"默认关闭、显式开启"，
    //   而不是"有口令就能用"——口令本身可能泄到管理端脚本里。
    if (action == QStringLiteral("dev_act")) {
        if (!qEnvironmentVariable("STE_QT_UID").startsWith(QLatin1String("ste-sbx-"))) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("dev_act 仅在沙箱实例（STE_QT_UID=ste-sbx-*）可用");
            return out;
        }
        const QString kind = params.value(QStringLiteral("kind")).toString();
        IslandOverlay *isl = IslandOverlay::instance();
        if (kind == QStringLiteral("ft")) {
            isl->setFileTransfer(params.value(QStringLiteral("name")).toString(),
                                 params.value(QStringLiteral("bytes")).toVariant().toLongLong(),
                                 params.value(QStringLiteral("total")).toVariant().toLongLong());
        } else if (kind == QStringLiteral("ft_clear")) {
            isl->clearFileTransfer();
        } else if (kind == QStringLiteral("media")) {
            isl->setMediaSession(params.value(QStringLiteral("title")).toString(),
                                 params.value(QStringLiteral("totalMs")).toVariant().toLongLong(),
                                 params.value(QStringLiteral("next")).toString(), QString());
        } else if (kind == QStringLiteral("media_clear")) {
            isl->clearMediaSession();
        } else if (kind == QStringLiteral("spectrum")) {
            // 16 位 16kHz 静默帧 → 只走"置位/展开"路径，不需要真的 MIC。
            isl->feedSpectrum(QByteArray(320 * 2, '\0'), 16000, 16);
        } else if (kind == QStringLiteral("spectrum_off")) {
            isl->setSpectrumActive(false);
        } else if (kind == QStringLiteral("expand")) {
            isl->setExpanded(params.value(QStringLiteral("on")).toBool(true));
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("dev_act 未知 kind：%1").arg(kind);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    if (action == QStringLiteral("lock")) {
        // user32 真实导出名就是 LockWorkStation（无 Native 后缀，那个名字不存在）
        if (LockWorkStation()) {
            out.result = QStringLiteral("done");
        } else {
            // 2026-10-14 修（审计）：这里原来用**逗号运算符**当语句分隔符
            //（`out.result = ..., out.error = ...`）—— 靠 QString::operator= 返回引用才凑合成立。
            // 它看起来就像漏了个分号，下一个改这段的人很容易「顺手」补成分号再引入真 bug。
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("LockWorkStation 返回 FALSE，错误码 %1").arg((int)GetLastError());
        }
        return out;
    }

    if (action == QStringLiteral("shutdown") || action == QStringLiteral("reboot")) {
        // ① 去重：老版本云端（还没有 no_resend 那套逻辑）重连时会把同一条再补发一次，
        //    照单全收就是"关了又关、重启了又重启"。命中就直接跳过、如实回执，不重复执行。
        if ((*g_actionDeps->terminalDone).contains((*g_actionDeps->lastInstrId))) {
            const QString dup = QStringLiteral("本机已执行过该指令（指令 id %1），按重复下发保护跳过%2")
                                    .arg((*g_actionDeps->lastInstrId),
                                         action == QStringLiteral("reboot") ? QStringLiteral("重启") : QStringLiteral("关机"));
            qInfo("[agent-qt] ⛔ %s", qPrintable(dup));
            out.result = QStringLiteral("failed");
            out.error  = dup;
            return out;
        }

        // ② 先落记录：这条 id 就算机器马上关了，下次（老版云端补发）也认。
        g_actionDeps->markTerminalDone((*g_actionDeps->lastInstrId));

        // ③ 回执由调用方在本函数返回后发出（第 4206 行附近），所以这里**只排期、不执行**，
        //    留 2.5 秒给回执把 socket 写出去 —— 顺序反了回执就永远发不出去（恶性BUG 根因）。
        const DWORD flags = (action == QStringLiteral("reboot") ? EWX_REBOOT : EWX_SHUTDOWN) | EWX_FORCE;
        const bool isReboot = (action == QStringLiteral("reboot"));
        out.result = QStringLiteral("done");
        out.error.clear();
        out.data.insert(QStringLiteral("shutting_down"), true);
        out.data.insert(QStringLiteral("action"), action);
        QTimer::singleShot(2500, [flags, isReboot]() {
            g_actionDeps->enableShutdownPriv();
            if (ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED))
                qInfo("[agent-qt] ⏻ 已执行%s（EWX 返回 TRUE）", isReboot ? "重启" : "关机");
            else
                // 失败要带系统原话：只写"关机失败"会把四种病藏成一个样，误判过一次
                qWarning("[agent-qt] ⏻ %s 失败（ExitWindowsEx 错误码 %u）—— 多半是没开关机权限或不是管理员起的",
                         isReboot ? "重启" : "关机", (unsigned)GetLastError());
        });
        return out;
    }

    if (action == QStringLiteral("input")) {
        const QString kind = params.value(QStringLiteral("kind")).toString();
        // 2026-10-07：三键。管理端会把 button 一起带过来（left/right/middle）。
        // 旧版不管按的是哪个键，down/up 一律写 MOUSEEVENTF_LEFTDOWN/LEFTUP ——
        // 右键点不出右键菜单、中键的自动滚也全废，而且"按住右键拖动"会被当成左键拖。
        const QString btn = params.value(QStringLiteral("button")).toString();
        const DWORD flagDown = (btn == QStringLiteral("right"))  ? MOUSEEVENTF_RIGHTDOWN
                             : (btn == QStringLiteral("middle")) ? MOUSEEVENTF_MIDDLEDOWN
                             : MOUSEEVENTF_LEFTDOWN;
        const DWORD flagUp = (btn == QStringLiteral("right"))  ? MOUSEEVENTF_RIGHTUP
                           : (btn == QStringLiteral("middle")) ? MOUSEEVENTF_MIDDLEUP
                           : MOUSEEVENTF_LEFTUP;

        // ⚠️ 绝对坐标的基准是**整个虚拟桌面**，不是主屏。
        //   MOUSEEVENTF_VIRTUALDESK 的 dx/dy 按虚拟桌面归一化，而老写法拿
        //   SM_CXSCREEN / SM_CYSCREEN（只要主屏的尺寸）去换算 —— 教室机接投影仪 +
        //   副屏时（虚拟桌面比主屏宽一截，左上还有负偏移）算出来的落点整体偏出去，
        //   越靠右偏得越狠。这就是"我点这儿、光标跑到那儿"的根因。
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        INPUT in = {};
        in.type = INPUT_MOUSE;
        if (kind == QStringLiteral("move") || kind == QStringLiteral("down") || kind == QStringLiteral("up")) {
            // 归一化坐标夹到 [0,1]：越界就别发，免得把光标甩到桌面外去（会被夹回角上）
            const double nx = qBound(0.0, params.value(QStringLiteral("x")).toDouble(), 1.0);
            const double ny = qBound(0.0, params.value(QStringLiteral("y")).toDouble(), 1.0);
            const int px = vx + qRound(nx * (vw - 1));
            const int py = vy + qRound(ny * (vh - 1));
            in.mi.dx = (LONG)((px - vx) * 65535 / (vw - 1));
            in.mi.dy = (LONG)((py - vy) * 65535 / (vh - 1));
            in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            if (kind == QStringLiteral("move"))      in.mi.dwFlags |= MOUSEEVENTF_MOVE;
            else if (kind == QStringLiteral("down")) in.mi.dwFlags |= flagDown;
            else                                     in.mi.dwFlags |= flagUp;
            if (SendInput(1, &in, sizeof(in)) == 1) out.result = QStringLiteral("done");
            else out.result = QStringLiteral("failed"),
                 out.error = QStringLiteral("SendInput 鼠标失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        if (kind == QStringLiteral("wheel")) {
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = (DWORD)params.value(QStringLiteral("delta")).toInt();
            if (SendInput(1, &in, sizeof(in)) == 1) out.result = QStringLiteral("done");
            else out.result = QStringLiteral("failed"),
                 out.error = QStringLiteral("SendInput 滚轮失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        if (kind == QStringLiteral("type")) {
            const QString text = params.value(QStringLiteral("text")).toString();
            bool ok = true;
            for (const QChar c : text) {
                const SHORT v = VkKeyScanW((WCHAR)c.unicode());
                if (v == -1) continue;
                const BYTE vk = (BYTE)(v & 0xFF);
                INPUT ki = {};
                ki.type = INPUT_KEYBOARD;
                ki.ki.wVk = vk;
                if (SendInput(1, &ki, sizeof(ki)) != 1) { ok = false; break; }
                ki.ki.dwFlags = KEYEVENTF_KEYUP;
                if (SendInput(1, &ki, sizeof(ki)) != 1) { ok = false; break; }
            }
            out.result = ok ? QStringLiteral("done") : QStringLiteral("failed");
            if (!ok) out.error = QStringLiteral("SendInput 键盘注入失败，错误码 %1").arg((int)GetLastError());
            return out;
        }
        // 2026-10-17 远控/键鼠补齐：独立按键事件（keydown/keyup）。
        //   · 参数 vk = Windows 虚拟键码（十进制），action = down | up
        //   · 组合键（Ctrl+C 之类 / Ctrl+Alt+Del）由管理端依次发多个 key down 再逐个 key up，
        //     agent 这里只负责单键注入 —— 与鼠标 down/up 语义对齐。
        //   · 不做 VkKeyScan 转换（管理端直接给 vk），保证方向键/F1-F12/修饰键都能下发。
        if (kind == QStringLiteral("key")) {
            const int vk = params.value(QStringLiteral("vk")).toInt();
            const QString ka = params.value(QStringLiteral("action")).toString();
            if (vk <= 0 || vk > 255) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("vk 无效：%1").arg(vk);
                return out;
            }
            INPUT ki = {};
            ki.type = INPUT_KEYBOARD;
            ki.ki.wVk = (WORD)vk;
            if (ka == QStringLiteral("up")) ki.ki.dwFlags = KEYEVENTF_KEYUP;
            // down 无额外标志；extended 键（如方向键）需 KEYEVENTF_EXTENDEDKEY，见下
            if (vk >= VK_LEFT && vk <= VK_DOWN) ki.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            if (SendInput(1, &ki, sizeof(ki)) == 1) out.result = QStringLiteral("done");
            else {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("SendInput 按键失败，错误码 %1").arg((int)GetLastError());
            }
            return out;
        }
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("未知 input 子类型：%1").arg(kind);
        return out;
    }

    // 安全验证专用（不破坏）：写个标记文件证明"真执行链路通了"
    if (action == QStringLiteral("exec-test")) {
        const QString path = QDir::temp().filePath(QStringLiteral("ste-exec-ok.txt"));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(QStringLiteral("done at %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)).toUtf8());
            f.close();
            out.result = QStringLiteral("done");
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("写标记文件失败：%1").arg(path);
        }
        return out;
    }

    /* ══════════ 批次 1：教室刚需（探针 / 音量 / 截图 / 开程序 / 进程查看与结束 / 日志尾） ══════════ */

    // 最便宜的探针：确认"指令真走到了被控端"，而不是只看云端说"发出去了"
    if (action == QStringLiteral("ping")) {
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("pong"), true);
        out.data.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        return out;
    }


    // 主音量 0-100。
    //
    // ⚠️ 2026-10-11 修「音量无法调节」：原来用 winmm 的 waveOutSetVolume，
    //    它调的是**本进程自己的波形输出音量**（Windows Vista 之后音量是每应用/每会话的），
    //    也就是说它只把"星集控被控端这个程序"的音量改了，教室机上正在放听力、放视频的
    //    那些程序一点没变 —— 老师在管理端拖滑块，教室机声音纹丝不动，报上来就是"调不了"。
    //    正确做法：走 Core Audio（WASAPI）的 IAudioEndpointVolume，改**默认播放设备的主音量**。
    //    改完回读一次实际值回给管理端（不拿请求值冒充生效值）。
    if (action == QStringLiteral("set_volume")) {
        int v = 50;
        const QJsonValue jv = params.value(QStringLiteral("value"));
        if (jv.isDouble()) v = jv.toInt();
        else if (jv.isString()) { bool okv = false; const int t = jv.toString().trimmed().toInt(&okv); if (okv) v = t; }
        const int want = v;
        v = qBound(0, v, 100);

        if (!setSystemMasterVolume(v, &out.error)) {
            // Core Audio 走不通（无音频设备 / 服务被禁 / 远程会话无端点）就如实报失败，
            // 并且**回落到老的 waveOutSetVolume** —— 能调一点是一点，但不假装调成功了：
            // 回落时回执里带 fallback 标记，管理端能看出这不是标准路径。
            const quint32 lvl = ((quint32)v * 0xFFFFu) / 100u;
            const quint32 packed = (lvl << 16) | lvl;
            waveOutSetVolume(nullptr, packed);
            qWarning("[agent-qt] set_volume 走 Core Audio 失败（%s），回落 waveOutSetVolume",
                     out.error.toUtf8().constData());
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("volume"), v);
            out.data.insert(QStringLiteral("fallback"), QStringLiteral("waveOut"));
            out.data.insert(QStringLiteral("note"), QStringLiteral("系统主音量接口不可用，只改了被控端进程自身音量"));
            if (want != v) out.data.insert(QStringLiteral("clampedFrom"), want);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("volume"), v);
        if (want != v) out.data.insert(QStringLiteral("clampedFrom"), want);
        return out;
    }

    // 桌面截屏落盘（区别于画面流：这个用来留证/回看）
    if (action == QStringLiteral("screenshot")) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到主屏"); return out; }
        const QPixmap pm = screen->grabWindow(0);
        if (pm.isNull()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("grabWindow 返回空图"); return out; }
        QDir().mkpath(g_shotDir);
        const QString path = g_shotDir + QStringLiteral("/shot-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        if (!pm.save(path, "PNG")) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("保存失败：%1").arg(path);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(path));
        out.data.insert(QStringLiteral("bytes"), (int)QFileInfo(path).size());
        out.data.insert(QStringLiteral("width"), pm.width());
        out.data.insert(QStringLiteral("height"), pm.height());

        // ── 2026-10-11 · #1「截图没有反馈」＝两端都要 ──────────────────────────
        // ① 本机（被控端）知情提示：老师截的是这台机器的屏，使用者应当知道（隐私知情）。
        //    以前截完悄无声息，学生完全不知道自己被截了图。
        //    图标用 "eye"（与「屏幕正在被查看」同一枚，已在用、必然能渲染）。
        islandShow(QStringLiteral("屏幕已被截取"),
                   QStringLiteral("老师远程截取了这台设备的屏幕"),
                   QStringLiteral("eye"), 6000);

        // ② 缩略图回传：管理端以前拿到的 path 是**学生机上的绝对路径**，老师根本
        //    看不到图，等于「执行了但没反馈」。这里把原图缩到宽 640 的 JPEG 塞进
        //    回执（base64），管理端拿到就能直接预览，不必再登学生机取文件。
        {
            const QPixmap thumb = (pm.width() > 640)
                                      ? pm.scaledToWidth(640, Qt::SmoothTransformation)
                                      : pm;
            QByteArray jpg;
            QBuffer buf(&jpg);
            if (buf.open(QIODevice::WriteOnly) && thumb.save(&buf, "JPG", 70)) {
                out.data.insert(QStringLiteral("thumb"), QString::fromLatin1(jpg.toBase64()));
                out.data.insert(QStringLiteral("thumbBytes"), jpg.size());
            } else {
                qWarning("[agent-qt] 截图缩略图编码失败（回执不带 thumb，管理端仍可按 path 提示）");
            }
        }
        return out;
    }

    // 远程打开软件。白名单约束（照抄 Rust）：默认只允许教育/办公类，
    // 避免"任意程序可被远程拉起"变成攻击面。
    if (action == QStringLiteral("launch_app") || action == QStringLiteral("open_app")) {
        const QString target = params.value(QStringLiteral("target")).toString().trimmed();
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("参数缺失：需要 target（进程名或可执行路径）");
            return out;
        }
        // ⚠️ 2026-10-13 三端审计修复：原来白名单用 `target.toLower().contains(item)` ——
        //    **子串**匹配。于是 `incoming\chrome-setup.exe`（file_push 能把它落到收件目录，
        //    见 (*g_actionDeps->recvDir)）里"含有 chrome"就被放行 ⇒ 配合 file_push = 免确认的任意代码执行。
        //    改成**两道闸**（缺一不可）：
        //      ① 名字必须**基名精确**命中（剥路径 + 去 .exe 后缀后全等）；
        //      ② 带路径的 target 必须落在**可信安装目录**内 —— 防"把恶意二进制直接改名成
        //         chrome.exe 再放进收件目录"绕过 ①（file_push 不禁止叫 chrome.exe）。
        auto normExeName = [](const QString &s) {
            QString n = QFileInfo(s.trimmed()).fileName().toLower();
            if (n.endsWith(QLatin1String(".exe"))) n.chop(4);
            return n;
        };
        const QString tName = normExeName(target);

        // ① 名字闸：内置名单（去 .exe 后缀，与归一化对齐）+ 管理员配的放行名单
        bool allowed = false;
        const QString allow = qEnvironmentVariable("STELARITH_AGENT_LAUNCH_ALLOWLIST").toLower();
        const QStringList items = allow.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &item : items)
            if (!item.trimmed().isEmpty() && normExeName(item) == tName) { allowed = true; break; }
        static const char *const kBuiltinSafe[] = {
            "classisland", "xingjikong", "explorer", "msedge", "chrome", "firefox",
            "wechat", "wps", "word", "excel", "powerpnt", "notepad", "cmd", nullptr
        };
        if (!allowed)
            for (const char *const *p = kBuiltinSafe; *p; ++p)
                if (tName == QLatin1String(*p)) { allowed = true; break; }
        if (!allowed) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("launch_app: 「%1」不在内置安全名单（classisland/xingjikong/浏览器/Office/记事本…），"
                                       "被拒绝。可配置 STELARITH_AGENT_LAUNCH_ALLOWLIST 放行。").arg(target);
            return out;
        }

        // ② 目录闸：带路径的 target 必须落在可信安装目录（否则"改名成合法名"就能绕过 ①）。
        //    裸名字（如 "notepad"）不含分隔符 → 交给 ShellExecuteW 走 App Paths/PATH 系统级解析，安全。
        if (target.contains(QLatin1Char('\\')) || target.contains(QLatin1Char('/'))) {
            const QString abs = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath()).toLower();
            QStringList roots;
            const auto addRoot = [&roots](const QString &p) {
                if (!p.trimmed().isEmpty())
                    roots << (QDir::toNativeSeparators(QDir::cleanPath(p)).toLower() + QLatin1Char('\\'));
            };
            addRoot(qEnvironmentVariable("ProgramFiles"));
            addRoot(qEnvironmentVariable("ProgramFiles(x86)"));
            addRoot(qEnvironmentVariable("ProgramW6432"));
            addRoot(qEnvironmentVariable("SystemRoot"));                        // 含 System32 / SysWOW64
            addRoot(qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs"));  // 用户级安装
            // 逃生口：非标准安装位置（如 D:\Programs）由管理员显式追加目录白名单
            for (const QString &extra : qEnvironmentVariable("STELARITH_AGENT_LAUNCH_DIRS")
                                            .split(QLatin1Char(';'), Qt::SkipEmptyParts))
                addRoot(extra);
            bool inTrusted = false;
            for (const QString &root : roots) if (abs.startsWith(root)) { inTrusted = true; break; }
            if (!inTrusted) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("launch_app: 「%1」不在可信安装目录（Program Files / Windows / 用户程序目录），"
                                           "已拒绝。非标准安装位置可配置 STELARITH_AGENT_LAUNCH_DIRS 追加。").arg(target);
                qWarning("[agent-qt] launch_app 拒绝：%s 不在可信目录（防改名绕过）", qPrintable(target));
                return out;
            }
        }
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(target.utf16()),
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("ShellExecuteW 失败，错误码 %1").arg((int)reinterpret_cast<INT_PTR>(r));
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), target);
        return out;
    }

    // 软件运行查看：只列「有主窗口标题」的进程 —— 老师要看的是"教室机上开着什么"，
    // 不是几百个 svchost。只读快照 + 最小权限查询，不提权、不注入。
    if (action == QStringLiteral("process_list")) {
        const int lim = params.value(QStringLiteral("limit")).toInt(kProcessListLimit);
        const int cap = lim > 0 ? qMin(lim, 500) : kProcessListLimit;
        const auto &snap = g_actionDeps->visibleWindowsCached();
        if (snap.names.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("无法获取进程快照"); return out; }
        const QHash<quint32, QString> &names = snap.names;
        const auto &wins = snap.wins;
        QJsonArray items;
        bool truncated = false;
        for (const auto &w : wins) {
            const auto it = names.constFind(w.second);
            if (it == names.constEnd() || it->isEmpty()) continue;
            if ((int)items.size() >= cap) { truncated = true; break; }
            // 2026-10-26（P3）：白名单 = 永不禁用且**不显示在列表**（保护系统稳定 + 免误关）
            if (g_actionDeps->softHit((*g_actionDeps->soft).whitelist, *it)) continue;
            QJsonObject o;
            o.insert(QStringLiteral("pid"), (int)w.second);
            o.insert(QStringLiteral("name"), *it);
            o.insert(QStringLiteral("title"), g_actionDeps->windowTitleOf(w.first));
            o.insert(QStringLiteral("mem_mb"), g_actionDeps->memMbOf(w.second));
            const QString prot = g_actionDeps->protectedCategory(*it);
            if (!prot.isEmpty()) o.insert(QStringLiteral("protected"), prot);
            // 黑名单 → 标红；已冻结 → 标冻结（界面置灰"解冻"）
            if (g_actionDeps->softHit((*g_actionDeps->soft).blacklist, *it)) o.insert(QStringLiteral("blacklisted"), true);
            if ((*g_actionDeps->frozenPids).contains((quint32)w.second)) o.insert(QStringLiteral("frozen"), true);
            // 2026-10-11 需求#6：管理端进程列表要显示图标。图标只能这边取
            // （管理端没有这台机器上的 exe），取不到就留空 —— 那边画首字方块兜底。
            const QString ic = g_actionDeps->windowIconBase64(w.first);
            if (!ic.isEmpty()) o.insert(QStringLiteral("icon"), ic);
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), (int)items.size());
        out.data.insert(QStringLiteral("truncated"), truncated);
        return out;
    }

    // 软件控制：硬禁止名单第一关（命中**连 WM_CLOSE 都不发**）→ 温和关闭（发 WM_CLOSE，软件能存盘）
    // → 最多等 5 秒 → 仍未退出才 TerminateProcess。
    if (action == QStringLiteral("process_stop")) {
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt()
                                        : jpid.toString().trimmed().toInt();
        if (pid <= 0) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("参数缺失：需要 pid"); return out; }
        const QHash<quint32, QString> names = g_actionDeps->snapshotProcessNames();
        const QString exe = names.value((quint32)pid);
        if (exe.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 pid=%1 对应的进程（可能已退出）").arg(pid);
            return out;
        }
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：命中硬禁止名单「%3」").arg(exe).arg(pid).arg(prot);
            return out;
        }
        // ⚠️ 2026-10-13：名字名单之外**再过一次内核闸门**。
        //    名单不可能穷举 —— MemCompression / Registry / Secure System / LogonUI / 任意未列名的
        //    session-0 服务宿主都不在 kProtectedSystem 里，而名单曾是这条指令**唯一**的保护。
        //    这与 2026-10-09 学校机房"反复重启"事故（名单漏项 → TerminateProcess 掉 critical 进程
        //    → CRITICAL_PROCESS_DIED 0xEF）是同一类机理，只是换到了 process_stop 这条路径上；
        //    本端以 Highest 权限运行，杀掉 MemCompression 就是同一个 0xEF。
        //    闸门与 exam_mode 共用 proc_guard（问内核，不猜名字；查不到一律当受保护）。
        const QString blocked = terminateBlockedReason((DWORD)pid, prot);
        if (!blocked.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：%3").arg(exe).arg(pid).arg(blocked);
            return out;
        }
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!h) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("OpenProcess 失败（错误码 %1），可能权限不足或进程受保护").arg((int)GetLastError());
            return out;
        }
        int closed = 0;
        QVector<QPair<HWND, quint32>> wins;
        EnumWindows(g_actionDeps->enumWindowsCb, reinterpret_cast<LPARAM>(&wins));
        for (const auto &w : wins)
            if (w.second == (quint32)pid) { PostMessageW(w.first, WM_CLOSE, 0, 0); closed++; }
        if (closed > 0 && WaitForSingleObject(h, 5000) == WAIT_OBJECT_0) {
            CloseHandle(h);
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("wm_close"));
            out.data.insert(QStringLiteral("detail"), QStringLiteral("已温和关闭（WM_CLOSE，主窗口 %1 个）").arg(closed));
            return out;
        }
        const BOOL killed = TerminateProcess(h, 1);
        const DWORD lastErr = GetLastError();
        CloseHandle(h);
        if (killed) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("terminate"));
            out.data.insert(QStringLiteral("detail"),
                            closed > 0 ? QStringLiteral("温和关闭 5 秒未生效，已强制结束")
                                       : QStringLiteral("该进程没有主窗口，直接强制结束"));
            return out;
        }
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("TerminateProcess 失败（错误码 %1）").arg((int)lastErr);
        return out;
    }

    // 本机日志尾部：装机后没有 stdout，出故障只能靠它
    if (action == QStringLiteral("log_tail")) {
        if (g_logPath.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("本端未启用日志文件");
            return out;
        }
        QFile f(g_logPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("读取日志失败：%1").arg(g_logPath);
            return out;
        }
        int want = params.value(QStringLiteral("lines")).toInt(200);
        if (want <= 0) want = 200;
        want = qMin(want, 2000);
        const QStringList all = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        f.close();
        const int from = qMax(0, (int)all.size() - want);
        QJsonArray lines;
        for (int i = from; i < (int)all.size(); ++i) lines.append(all.at(i));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_logPath));
        out.data.insert(QStringLiteral("lines"), lines);
        out.data.insert(QStringLiteral("count"), (int)lines.size());
        return out;
    }

    // 快捷方式候选：管理端要「从这台机器上挑」，所以这里返回**可选来源**，而不是预置名单。
    // 两类：① 正在运行且有主窗口的程序（同一 exe 只出一条）② 桌面上的文件与文件夹。
    // 纪律：**不把集控自己列进去**（否则会变成"自己开自己"）。
    if (action == QStringLiteral("list_shortcut_candidates")) {
        QJsonArray apps;
        {
            const auto &snap = g_actionDeps->visibleWindowsCached();
            const QHash<quint32, QString> &names = snap.names;
            const auto &wins = snap.wins;
            QSet<QString> seen;
            for (const auto &w : wins) {
                const auto it = names.constFind(w.second);
                if (it == names.constEnd() || it->isEmpty()) continue;
                const QString exe = *it;
                const QString low = exe.toLower();
                if (low.startsWith(QStringLiteral("stelarith")) || low.startsWith(QStringLiteral("xingjikong")))
                    continue;
                if (seen.contains(low)) continue;
                seen.insert(low);
                QJsonObject o;
                o.insert(QStringLiteral("kind"), QStringLiteral("app"));
                o.insert(QStringLiteral("target"), exe);
                o.insert(QStringLiteral("title"), g_actionDeps->windowTitleOf(w.first));
                o.insert(QStringLiteral("pid"), (int)w.second);
                apps.append(o);
            }
        }

        QJsonArray files;
        const QString deskPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        {
            QDir d(deskPath);
            const QFileInfoList list =
                d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            int n = 0;
            for (const QFileInfo &fi : list) {
                if (n++ >= 60) break;   // 桌面上东西多时截断，别把回执撑爆
                QJsonObject o;
                o.insert(QStringLiteral("kind"), fi.isDir() ? QStringLiteral("dir") : QStringLiteral("file"));
                o.insert(QStringLiteral("target"), QDir::toNativeSeparators(fi.absoluteFilePath()));
                o.insert(QStringLiteral("title"), fi.fileName());
                files.append(o);
            }
        }

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("apps"), apps);
        out.data.insert(QStringLiteral("desktop"), files);
        out.data.insert(QStringLiteral("desktopPath"), QDir::toNativeSeparators(deskPath));
        return out;
    }

    /* ══════════ 批次 2：全量补齐（调度器 / 相机 / 媒体 / 远控 / 文件推送） ══════════ */

    // ── 定时关机三件套 ──
    // 参数：at（ISO 8601 绝对时间）或 delay_sec（秒），二选一；what：shutdown|reboot（默认 shutdown）
    // 落盘到 schedules.json，进程重启会自动重挂——所以"定时"不是靠进程活着撑着的假定时。
    if (action == QStringLiteral("schedule_shutdown") || action == QStringLiteral("schedule_reboot")) {
        const QString what = (action == QStringLiteral("schedule_reboot"))
                                 ? QStringLiteral("reboot") : QStringLiteral("shutdown");
        qint64 at = 0;
        const QString atStr = params.value(QStringLiteral("at")).toString().trimmed();
        const QJsonValue dsec = params.value(QStringLiteral("delay_sec"));
        if (!atStr.isEmpty()) {
            // 兼容带 Z 与不带 Z：QT 的 fromString 对 T 分隔 + Z 都能吃
            QDateTime dt = QDateTime::fromString(atStr, Qt::ISODateWithMs);
            if (dt.date() == QDate()) dt = QDateTime::fromString(atStr, Qt::ISODate);
            if (!dt.isValid()) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: at 不是合法时间（要 ISO 8601，如 2026-10-03T22:00:00）");
                return out;
            }
            at = dt.toMSecsSinceEpoch();
        } else if (dsec.isDouble()) {
            // 数字型：直接取。此前这里把 `okv` 留在 false（只在字符串分支赋过值）
            // → 条件 `!okv` 恒真 → **传数字的 delay_sec 一律被拒**，只有字符串型才能安排成功。
            // 数字是下发侧最自然的写法（JSON number），这个洞等于"定时开关机"用不了。
            const int sec = dsec.toInt();
            if (sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else if (dsec.isString() && !dsec.toString().trimmed().isEmpty()) {
            bool okv = false;
            const int sec = dsec.toString().trimmed().toInt(&okv);
            if (!okv || sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 需要 at（ISO 时间）或 delay_sec（秒）其一");
            return out;
        }
        if (at <= QDateTime::currentMSecsSinceEpoch()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 时间已在过去，拒绝安排");
            return out;
        }

        // 同一台机只允许一条待执行任务：教室机上是"排一个就排一个"，多个排队语义复杂且易误伤
        QVector<SchedTask> cur = g_actionDeps->loadSchedules();
        QString replacedId;
        for (int i = 0; i < cur.size(); ++i) {
            const QString oldId = cur.at(i).id;
            // 同步停掉旧定时器，否则会出现"文件里没记录但定时器仍在跑"——到点照样关机
            const auto ti = (*g_actionDeps->schedTimerById).constFind(oldId);
            if (ti != (*g_actionDeps->schedTimerById).constEnd() && ti.value()) {
                ti.value()->stop();
                delete ti.value();
                (*g_actionDeps->schedTimerById).erase(ti);
            }
            replacedId = oldId;
            cur.removeAt(i);
            break;
        }

        SchedTask nt;
        nt.id = QStringLiteral("sch-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        nt.what = what;
        nt.at = at;
        nt.createdMs = QDateTime::currentMSecsSinceEpoch();
        cur.append(nt);
        g_actionDeps->saveSchedules(cur);
        g_actionDeps->armScheduleTimer(nt, (int)qMin<qint64>(at - QDateTime::currentMSecsSinceEpoch(), 2147483647LL));

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), nt.id);
        out.data.insert(QStringLiteral("what"), what);
        out.data.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(at).toString(Qt::ISODateWithMs));
        out.data.insert(QStringLiteral("inMs"), at - QDateTime::currentMSecsSinceEpoch());
        out.data.insert(QStringLiteral("persisted"), true);
        out.data.insert(QStringLiteral("fireAllowed"), g_actionDeps->schedFireAllowed());
        // 明说这条定时**现在就不会真关机器**（未装机开启时）——“报了安排成功”和“真的会关”是两件事
        if (!g_actionDeps->schedFireAllowed())
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("到期不会真执行（STE_QT_ALLOW_SCHED_FIRE≠1），装机时需在 agent.env 设为 1"));
        if (!replacedId.isEmpty()) out.data.insert(QStringLiteral("replaced"), replacedId);
        qInfo("[agent-qt] ⏲ 已安排 %s @ %s（%lld 秒后）", qPrintable(what),
              qPrintable(out.data.value(QStringLiteral("at")).toString()),
              (at - QDateTime::currentMSecsSinceEpoch()) / 1000);
        return out;
    }

    // 列出所有待执行定时任务（管理端"看计划"用）。已到期未执行的不会出现在这里。
    if (action == QStringLiteral("list_schedules")) {
        const QVector<SchedTask> tasks = g_actionDeps->loadSchedules();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        int pending = 0;
        for (const auto &t : tasks) {
            if (t.at <= now) continue;
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("what"), t.what);
            o.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(t.at).toString(Qt::ISODateWithMs));
            o.insert(QStringLiteral("inMs"), t.at - now);
            o.insert(QStringLiteral("inSec"), (t.at - now) / 1000);
            o.insert(QStringLiteral("createdAt"),
                     QDateTime::fromMSecsSinceEpoch(t.createdMs).toString(Qt::ISODateWithMs));
            items.append(o);
            ++pending;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), pending);
        out.data.insert(QStringLiteral("file"), QDir::toNativeSeparators((*g_actionDeps->schedFile)));
        return out;
    }

    // ── 软件管理（2026-10-26 · P3 产品笔记）─────────────────────────────────
    // 读策略：黑白名单 + 守护开关
    if (action == QStringLiteral("soft_policy_get")) {
        QJsonArray bl, wl;
        for (const QString &s : (*g_actionDeps->soft).blacklist) bl.append(s);
        for (const QString &s : (*g_actionDeps->soft).whitelist) wl.append(s);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("blacklist"), bl);
        out.data.insert(QStringLiteral("whitelist"), wl);
        out.data.insert(QStringLiteral("denyRun"), (*g_actionDeps->soft).denyRun);
        out.data.insert(QStringLiteral("file"), QDir::toNativeSeparators((*g_actionDeps->softPolicyFile)));
        return out;
    }

    // 写策略：{ blacklist?: [...], whitelist?: [...], denyRun?: bool }
    if (action == QStringLiteral("soft_policy_set")) {
        const QJsonValue jb = params.value(QStringLiteral("blacklist"));
        const QJsonValue jw = params.value(QStringLiteral("whitelist"));
        if (jb.isArray()) {
            QStringList nl;
            for (const auto &v : jb.toArray()) {
                const QString s = v.toString().trimmed();
                // 护栏：不许把受保护的进程塞进黑名单（否则守护会去杀系统/安全软件）
                if (!s.isEmpty() && g_actionDeps->protectedCategory(s).isEmpty() && !nl.contains(s)) nl.append(s);
            }
            (*g_actionDeps->soft).blacklist = nl;
        }
        if (jw.isArray()) {
            QStringList nl;
            for (const auto &v : jw.toArray()) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty() && !nl.contains(s)) nl.append(s);
            }
            (*g_actionDeps->soft).whitelist = nl;
        }
        const QJsonValue jd = params.value(QStringLiteral("denyRun"));
        if (jd.isBool()) (*g_actionDeps->soft).denyRun = jd.toBool();
        g_actionDeps->saveSoftPolicy();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("blackCount"), (*g_actionDeps->soft).blacklist.size());
        out.data.insert(QStringLiteral("whiteCount"), (*g_actionDeps->soft).whitelist.size());
        out.data.insert(QStringLiteral("denyRun"), (*g_actionDeps->soft).denyRun);
        return out;
    }

    // 冻结 / 解冻进程（仅常用应用；受保护一律拒绝）
    if (action == QStringLiteral("soft_freeze") || action == QStringLiteral("soft_unfreeze")) {
        const bool freeze = (action == QStringLiteral("soft_freeze"));
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt() : jpid.toString().trimmed().toInt();
        if (pid <= 0) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("需要 pid"); return out; }
        const QString exe = g_actionDeps->snapshotProcessNames().value((quint32)pid);
        if (exe.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 pid=%1 的进程（可能已退出）").arg(pid);
            return out;
        }
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("不许对受保护进程操作：%1（%2）").arg(exe, prot);
            return out;
        }
        const bool ok = g_actionDeps->setProcessFrozen((quint32)pid, freeze);
        if (!ok) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("%1 失败：%2（ntdll 接口不可用或权限不足）").arg(freeze ? QStringLiteral("冻结") : QStringLiteral("解冻"), exe);
            return out;
        }
        if (freeze) (*g_actionDeps->frozenPids).insert((quint32)pid);
        else (*g_actionDeps->frozenPids).remove((quint32)pid);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("pid"), pid);
        out.data.insert(QStringLiteral("frozen"), freeze);
        return out;
    }

    // 去网 / 恢复（防火墙出站规则；需管理员，失败如实报）
    if (action == QStringLiteral("soft_deny_net") || action == QStringLiteral("soft_restore_net")) {
        const bool block = (action == QStringLiteral("soft_deny_net"));
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt() : jpid.toString().trimmed().toInt();
        const QString path = (pid > 0) ? g_actionDeps->processPathOf((quint32)pid)
                                       : params.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("需要 pid 或 path（去网按可执行文件路径加规则）");
            return out;
        }
        const QString exe = QFileInfo(path).fileName();
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("不许对受保护程序去网：%1（%2）").arg(exe, prot);
            return out;
        }
        const bool ok = g_actionDeps->setNetBlocked(path, block);
        out.result = ok ? QStringLiteral("done") : QStringLiteral("failed");
        if (!ok)
            out.error = QStringLiteral("%1 失败：netsh 非零退出（多半是**没有管理员权限**）").arg(block ? QStringLiteral("去网") : QStringLiteral("恢复网络"));
        else
            out.data.insert(QStringLiteral("path"), path);
        return out;
    }

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

    // ── 定时通知（2026-10-27 · F 收尾 · 第六刀）：schedule_notify / list_notifies / cancel_notify ──
    // 与闹钟（AlarmDomain）分工：闹钟 = 响铃/锁屏/TTS「叫醒」；这里 = 走通知体系（岛/弹窗/跑马灯/全屏）「告诉」。
    // 产品原话「定时任务里应包括关机、定时闹钟（附有锁屏与相间选时，类似手机闹钟）」——关机与闹钟已有，这条是补的定时通知。
    if (action == QStringLiteral("schedule_notify")) {
        NotifyDomain::NotifyTask t;
        t.id = params.value(QStringLiteral("id")).toString().trimmed();
        if (t.id.isEmpty())
            t.id = QStringLiteral("ntf-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        t.time = params.value(QStringLiteral("time")).toString().trimmed();
        t.times = params.value(QStringLiteral("times")).toInt(1);
        t.intervalMin = params.value(QStringLiteral("intervalMin")).toInt(0);
        t.title = params.value(QStringLiteral("title")).toString();
        t.content = params.value(QStringLiteral("content")).toString();
        t.channel = params.value(QStringLiteral("channel")).toString().trimmed();
        t.createdMs = QDateTime::currentMSecsSinceEpoch();
        const QJsonArray daysArr = params.value(QStringLiteral("days")).toArray();
        for (const auto &dv : daysArr) {
            const int day = dv.toInt(0);
            if (day >= 1 && day <= 7 && !t.days.contains(day)) t.days.append(day);
        }
        QString err;
        if (!NotifyDomain::upsert(t, &err)) {      // 校验失败如实报，不静默
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_notify: ") + err;
            return out;
        }
        const qint64 next = NotifyDomain::nextFireMs(t, QDateTime::currentMSecsSinceEpoch(), 0);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), t.id);
        out.data.insert(QStringLiteral("times"), qMax(1, t.times));
        out.data.insert(QStringLiteral("nextAt"),
                        next > 0 ? QDateTime::fromMSecsSinceEpoch(next).toString(Qt::ISODateWithMs)
                                 : QString());
        if (!t.days.isEmpty() && t.times > 1)
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("days 非空时按每天一次处理，times 被忽略"));
        return out;
    }

    if (action == QStringLiteral("list_notifies")) {
        const QVector<NotifyDomain::NotifyTask> tasks = NotifyDomain::loadTasks();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        for (const auto &t : tasks) {
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("time"), t.time);
            o.insert(QStringLiteral("times"), t.times);
            o.insert(QStringLiteral("intervalMin"), t.intervalMin);
            o.insert(QStringLiteral("title"), t.title);
            o.insert(QStringLiteral("content"), t.content);
            o.insert(QStringLiteral("channel"), t.channel);
            QJsonArray d;
            for (int day : t.days) d.append(day);
            o.insert(QStringLiteral("days"), d);
            const qint64 nx = NotifyDomain::nextFireMs(t, now, 0);
            o.insert(QStringLiteral("nextAt"),
                     nx > 0 ? QDateTime::fromMSecsSinceEpoch(nx).toString(Qt::ISODateWithMs)
                            : QString());
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), items.size());
        return out;
    }

    if (action == QStringLiteral("cancel_notify")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (!NotifyDomain::cancel(id)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_notify: 找不到 id=%1 的定时通知").arg(id);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    // ── 时间同步（2026-10-27 · G3 灵动岛拓展第二项）：time_sync / time_offset / time_status ──
    // 三条指令共用 TimesyncDomain（NTP + 人工偏移）。产品口径：提醒要准点，且学校铃声
    // 与标准时间常有偏差 —— 偏移只改**判定**，不改系统钟（理由见 timesync_domain.h）。
    if (action == QStringLiteral("time_sync")) {
        // 异步：NTP 一轮最长 3s×2 台，阻塞等会让整条指令超时（云端 10s 兜底）。
        // ⇒ 只报"是否真的发起了"，结果让 time_status 查 —— 不谎称已成功。
        const bool started = TimesyncDomain::syncNow();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("started"), started);
        out.data.insert(QStringLiteral("note"),
                        started ? QStringLiteral("已发起 NTP 同步，约 1–6 秒后用 time_status 查看结果")
                                : QStringLiteral("已有一轮同步在进行，本次未重复发起"));
        return out;
    }

    if (action == QStringLiteral("time_offset")) {
        const QJsonValue jm = params.value(QStringLiteral("minutes"));
        const int m = jm.isDouble() ? jm.toInt() : jm.toString().trimmed().toInt();
        if (!jm.isDouble() && jm.toString().trimmed().isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("time_offset: 需要 minutes（整数，正=判定时间往前拨；0=取消偏移）");
            return out;
        }
        if (!TimesyncDomain::setOffsetMinutes(m)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("time_offset: minutes=%1 越界（允许 ±720）").arg(m);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("offsetMin"), m);
        out.data.insert(QStringLiteral("skewMs"), (double)TimesyncDomain::skewMs());
        out.data.insert(QStringLiteral("note"),
                        QStringLiteral("只影响课表/提醒判定，系统时钟未修改"));
        return out;
    }

    if (action == QStringLiteral("time_status")) {
        const TimesyncDomain::Status st = TimesyncDomain::status();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("ntpOk"), st.ntpOk);
        out.data.insert(QStringLiteral("deltaMs"), (double)st.deltaMs);
        out.data.insert(QStringLiteral("offsetMin"), st.offsetMin);
        out.data.insert(QStringLiteral("skewMs"), (double)TimesyncDomain::skewMs());
        out.data.insert(QStringLiteral("server"), st.server);
        out.data.insert(QStringLiteral("lastAttempt"), st.lastAttempt);
        out.data.insert(QStringLiteral("lastSyncAt"),
                        st.lastSyncMs > 0
                            ? QDateTime::fromMSecsSinceEpoch(st.lastSyncMs).toString(Qt::ISODateWithMs)
                            : QString());
        out.data.insert(QStringLiteral("error"), st.error);
        // 判定用时间 + 系统时间的对照：一眼看出偏移生效了没有（诊断必备）
        out.data.insert(QStringLiteral("judgeTime"),
                        TimesyncDomain::now().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        out.data.insert(QStringLiteral("systemTime"),
                        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        return out;
    }

    // ── 岛组件面板（2026-10-27 · G4/G5/G6）：island_panel / island_panel_close ──
    // 对标 ClassIsland 的「组件」：日期 / 课表 / 倒计时。
    // 与通知的区别（产品口径）：通知是"事件"（来一条看完就走），面板是"看板"
    //   （今天第几节、还有多久下课 —— 要停下来看清、反复看）。
    // 所以面板走岛**展开区**并且可以常驻，不像通知那样 6 秒收走。
    if (action == QStringLiteral("island_panel")) {
        const QString face = params.value(QStringLiteral("face")).toString().trimmed().toLower();
        // durationSec：0 = 常驻（等 close 或下一条通知顶掉）；缺省 60 秒。
        // ⚠️ 不用 showIsland 的 5 秒默认值 —— 课表看 5 秒根本读不完。
        const int durSec = params.contains(QStringLiteral("durationSec"))
            ? params.value(QStringLiteral("durationSec")).toInt(60) : 60;
        if (durSec < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("island_panel: durationSec 不能为负（0 = 常驻）");
            return out;
        }
        QString err;
        QJsonObject data;
        if (!PanelDomain::show(face, params, durSec * 1000, &err, &data)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("island_panel: ") + err;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("face"), face);
        out.data.insert(QStringLiteral("durationSec"), durSec);
        // 把**真正显示出去的数据**也回执一份：管理端/端到端脚本能直接断言
        // "课表拿到几节、倒计时目标对不对"，不用去截图猜。
        out.data.insert(QStringLiteral("panel"), data);
        return out;
    }

    if (action == QStringLiteral("island_panel_close")) {
        PanelDomain::close();
        out.result = QStringLiteral("done");
        return out;
    }

    // ── PPT 放映控制（2026-10-27 · I 批，用户称"尤为重要"）：ppt_status / ppt_next / ppt_prev / ppt_goto / ppt_app ──
    // 口径（照用户原话）：**没开 PPT 或没在放映 → 如实 failed，绝不假成功**。
    // 但 data 里**仍然回完整状态**（app/presenting/slide/total/available）——
    // 管理端要靠它显示"未放映"中性徽标，而不是把一条状态查询当红色错误。
    if (action == QStringLiteral("ppt_status") || action == QStringLiteral("ppt_next")
        || action == QStringLiteral("ppt_prev") || action == QStringLiteral("ppt_goto")
        || action == QStringLiteral("ppt_app")) {

        // 三个字段是"本次实际控的是哪个"，每一次回执都带 —— 绝不让老师以为在控 Office 实际控的是 WPS
        const auto fillState = [&out](const PptDomain::Status &st) {
            const QString label = (st.app == QLatin1String("wps"))       ? QStringLiteral("WPS 演示")
                                : (st.app == QLatin1String("powerpoint")) ? QStringLiteral("PowerPoint")
                                                                          : QString();
            out.data.insert(QStringLiteral("app"), st.app);
            out.data.insert(QStringLiteral("appLabel"), label);
            out.data.insert(QStringLiteral("running"), st.running);
            out.data.insert(QStringLiteral("presenting"), st.presenting);
            out.data.insert(QStringLiteral("slide"), st.slide);
            out.data.insert(QStringLiteral("total"), st.total);
            out.data.insert(QStringLiteral("presentation"), st.presentation);
            QJsonArray avail;
            for (const QString &a : st.available) avail.append(a);
            out.data.insert(QStringLiteral("available"), avail);
        };

        if (action == QStringLiteral("ppt_app")) {
            const QString want = params.value(QStringLiteral("app")).toString().trimmed().toLower();
            QString err;
            bool saved = false;
            if (!PptDomain::setPreferredApp(want, &err, &saved)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("ppt_app: ") + err;
                return out;
            }
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("preferred"), PptDomain::preferredApp());
            out.data.insert(QStringLiteral("saved"), saved);
            // 切完立刻探一次：告诉调用方"现在实际能控的是哪个"（互斥时这条最关键）
            fillState(PptDomain::query());
            if (!saved)
                out.data.insert(QStringLiteral("note"), err);
            return out;
        }

        if (action == QStringLiteral("ppt_status")) {
            const PptDomain::Status st = PptDomain::query();
            fillState(st);
            out.data.insert(QStringLiteral("preferred"), PptDomain::preferredApp());
            if (!st.ok || !st.presenting) {
                out.result = QStringLiteral("failed");
                out.error = st.error.isEmpty() ? QStringLiteral("没有在放映") : st.error;
                out.data.insert(QStringLiteral("note"),
                                QStringLiteral("状态已采集，但当前没有可翻页的放映"));
                return out;
            }
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("正在放映：第 %1 / %2 页").arg(st.slide).arg(st.total));
            return out;
        }

        int gotoIndex = 0;
        if (action == QStringLiteral("ppt_goto")) {
            const QJsonValue jv = params.value(QStringLiteral("index"));
            gotoIndex = jv.isDouble() ? jv.toInt() : jv.toString().trimmed().toInt();
            if (gotoIndex < 1) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("ppt_goto: 需要 index（页码从 1 开始）");
                return out;
            }
        }

        PptDomain::Status st;
        QString err;
        const bool done = (action == QStringLiteral("ppt_goto"))
            ? PptDomain::gotoSlide(gotoIndex, &st, &err)
            : PptDomain::step(action == QStringLiteral("ppt_next") ? 1 : -1, &st, &err);
        if (!done) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("%1: %2").arg(action, err);
            // 失败也把**能拿到的状态**带上（例如"没在放映"时 slide/total 为 0，
            // 管理端据此把按钮置灰而不是反复点）
            fillState(PptDomain::query());
            return out;
        }
        fillState(st);
        out.result = QStringLiteral("done");
        if (!st.presenting) {
            // 翻到最后一页 PowerPoint 会结束放映 —— 动作成功了，但要如实说清
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("已翻到末尾，放映已结束（现在没有可翻页的放映）"));
        } else if (action == QStringLiteral("ppt_goto") && st.slide != gotoIndex) {
            // 请求值与实际停的页不一致（PPT 会夹到有效范围）⇒ 说清，不回显请求值
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("请求第 %1 页，实际停在第 %2 / %3 页")
                                .arg(gotoIndex).arg(st.slide).arg(st.total));
        } else {
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("第 %1 / %2 页").arg(st.slide).arg(st.total));
        }
        return out;
    }

    // ── 闹钟（2026-10-26 · P1 产品笔记）：schedule_alarm / list_alarms / cancel_alarm ──
    if (action == QStringLiteral("schedule_alarm")) {
        const QString time = params.value(QStringLiteral("time")).toString().trimmed();
        if (time.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: 需要 time（HH:MM，24 小时制）");
            return out;
        }
        const QStringList tp = time.split(QLatin1Char(':'));
        bool okH = false, okM = false;
        if (tp.size() == 2) { tp.at(0).toInt(&okH); tp.at(1).toInt(&okM); }
        if (!okH || !okM) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: time 格式应为 HH:MM");
            return out;
        }
        AlarmDomain::AlarmTask t;
        t.id = QStringLiteral("alm-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        t.time = time;
        t.text = params.value(QStringLiteral("text")).toString().trimmed();
        t.lock = params.value(QStringLiteral("lock")).toBool(false);
        t.createdMs = QDateTime::currentMSecsSinceEpoch();
        const QJsonArray daysArr = params.value(QStringLiteral("days")).toArray();
        for (const auto &dv : daysArr) {
            const int day = dv.toInt(0);
            if (day >= 1 && day <= 7 && !t.days.contains(day)) t.days.append(day);
        }
        const qint64 next = AlarmDomain::nextAlarmMs(t, QDateTime::currentMSecsSinceEpoch());
        if (next < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: 无法计算触发时间（检查 time/days）");
            return out;
        }
        QVector<AlarmDomain::AlarmTask> cur = AlarmDomain::loadAlarms();
        cur.append(t);
        AlarmDomain::saveAlarms(cur);
        AlarmDomain::armAlarmTimer(t);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), t.id);
        out.data.insert(QStringLiteral("nextAt"),
                        QDateTime::fromMSecsSinceEpoch(next).toString(Qt::ISODateWithMs));
        return out;
    }

    if (action == QStringLiteral("list_alarms")) {
        const QVector<AlarmDomain::AlarmTask> tasks = AlarmDomain::loadAlarms();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        for (const auto &t : tasks) {
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("time"), t.time);
            o.insert(QStringLiteral("text"), t.text);
            o.insert(QStringLiteral("lock"), t.lock);
            QJsonArray d;
            for (int day : t.days) d.append(day);
            o.insert(QStringLiteral("days"), d);
            const qint64 nx = AlarmDomain::nextAlarmMs(t, now);
            o.insert(QStringLiteral("nextAt"),
                     nx > 0 ? QDateTime::fromMSecsSinceEpoch(nx).toString(Qt::ISODateWithMs) : QString());
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), items.size());
        return out;
    }

    if (action == QStringLiteral("cancel_alarm")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_alarm: 需要 id");
            return out;
        }
        // 2026-10-27 第五刀：定时器表随闹钟域一起搬走了，这里只问结果
        if (!AlarmDomain::cancelAlarm(id)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 id=%1 的闹钟（可能已响或已取消）").arg(id);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    // 按 id 取消。找不到就如实报 failed（不算"静默成功"）。
    if (action == QStringLiteral("cancel_schedule")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_schedule: 需要 id");
            return out;
        }
        QVector<SchedTask> cur = g_actionDeps->loadSchedules();
        int hit = -1;
        for (int i = 0; i < cur.size(); ++i)
            if (cur.at(i).id == id) { hit = i; break; }
        if (hit < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 id=%1 的定时任务（可能已执行或已取消）").arg(id);
            return out;
        }
        // 先停定时器再删记录：顺序反了会出现"记录没了但定时器还在跑"，到点照样关机
        const auto ti = (*g_actionDeps->schedTimerById).constFind(id);
        if (ti != (*g_actionDeps->schedTimerById).constEnd() && ti.value()) {
            ti.value()->stop();
            delete ti.value();
            (*g_actionDeps->schedTimerById).erase(ti);
        }
        cur.removeAt(hit);
        g_actionDeps->saveSchedules(cur);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), id);
        out.data.insert(QStringLiteral("cancelled"), true);
        out.data.insert(QStringLiteral("remaining"), (int)cur.size());
        return out;
    }

    // ── 相机六件套（走 ffmpeg DirectShow 输入）──
    // ⚠️ 2026-10-23（AGENTS.md §10 第 3 刀）：分支体已下沉到 src/media/CameraActions.cpp。
    //    这里只留分支判断与调用；**函数体是逐字搬的**，行为零变化。
    if (action == QStringLiteral("camera_list")) return CameraActions::listDevices();

    // 绑定摄像头：管理端「监控中心」选定一枚后落在这里，之后抓拍/录制默认用它。
    // device 传空 = 解绑（回落默认）。绑定存本机（只有本机能枚举），不落 agent.env。
    if (action == QStringLiteral("camera_bind")) return CameraActions::bindDevice(params);

    // 摄像头自检：按绑定设备（或指定 device）真抓一帧，只回"能不能用 + 耗时 + 原因"。
    // 与 camera_snapshot 的区别：这是**诊断**用途（管理端「测试」按钮），不管画面好看不好看。
    if (action == QStringLiteral("camera_test")) return CameraActions::selfTest(params);

    // 拍一张：ffmpeg 从指定设备（默认 video0）抓一帧写 PNG。回执给路径 + 字节数 + base64 缩略图。
    if (action == QStringLiteral("camera_snapshot")) return CameraActions::snapshot(params);

    // 开始录像：后台起 ffmpeg 进程写 mp4。已经在录就如实报 failed（不静默换文件）。
    if (action == QStringLiteral("camera_record_start")) return CameraActions::recordStart(params);

    // 停止录像。回执给实际字节数——0 字节就是失败，不许报 done。
    if (action == QStringLiteral("camera_record_stop")) return CameraActions::recordStop();


    // ── 媒体四件套（扫媒体库目录；删除走回收站）──
    if (action == QStringLiteral("media_list")) {
        const QString root = params.value(QStringLiteral("dir")).toString().trimmed();
        const QString use = root.isEmpty() ? g_mediaDir : root;
        QDir d(use);
        if (!d.exists()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_list: 目录不存在 %1（可设 STE_QT_MEDIA_DIR 或传 dir）")
                            .arg(QDir::toNativeSeparators(use));
            return out;
        }
        static const char *const kMediaExt[] = {
            "mp4", "mkv", "mov", "avi", "wmv", "flv", "webm", "ts",
            "mp3", "wav", "m4a", "aac", "flac", "ogg", "wma", "ape", "opus",
            "pdf", "ppt", "pptx", "doc", "docx", "xls", "xlsx", "txt", nullptr
        };
        const int lim = qMin(params.value(QStringLiteral("limit")).toInt(200), 1000);
        QJsonArray items;
        bool truncated = false;
        const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            QString ext = fi.suffix().toLower();
            bool hit = false;
            for (const char *const *p = kMediaExt; *p; ++p)
                if (ext == QLatin1String(*p)) { hit = true; break; }
            if (!hit) continue;
            if ((int)items.size() >= lim) { truncated = true; break; }
            QJsonObject o;
            o.insert(QStringLiteral("name"), fi.fileName());
            o.insert(QStringLiteral("path"), QDir::toNativeSeparators(fi.absoluteFilePath()));
            o.insert(QStringLiteral("bytes"), (qint64)fi.size());
            o.insert(QStringLiteral("sizeMB"), fi.size() / 1048576.0);
            o.insert(QStringLiteral("ext"), ext);
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("dir"), QDir::toNativeSeparators(use));
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), (int)items.size());
        out.data.insert(QStringLiteral("truncated"), truncated);
        return out;
    }

    // 删除：走 SHFileOperation 的 FO_DELETE + FO_ALLOWUNDO（进回收站，可恢复）。
    // 目标路径必须在媒体库目录内——防 `..` 越界删到系统区。
    if (action == QStringLiteral("media_delete")) {
        const QString target = params.value(QStringLiteral("path")).toString().trimmed();
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 需要 path");
            return out;
        }
        if (!g_actionDeps->pathInside(g_mediaDir, target)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 「%1」不在媒体库目录内（%2），拒绝删除")
                            .arg(QDir::toNativeSeparators(target))
                            .arg(QDir::toNativeSeparators(g_mediaDir));
            return out;
        }
        QFileInfo fi(target);
        if (!fi.exists()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 文件不存在 %1").arg(QDir::toNativeSeparators(target));
            return out;
        }
        SHFILEOPSTRUCTW op = {};
        const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
        // `pFrom` 必须是**双重** null 结尾（路径一个 \0，结构体再一个 \0）。
        // 之前直接把 `native.utf16()` 的指针塞进去：QString 只保证末尾一个 \0，
        // SHFileOperation 于是把第一个 \0 之后的内容当成"下一个要删的文件"接着读 ——
        // 结果是**永远返回错误码 2**，报"可能被占用或权限不足"，
        // 而媒体库里的文件明明一个都没删掉。注释写了规则、代码没照做，才躲不掉这个洞。
        QVector<ushort> pathBuf(native.size() + 2, 0);
        for (int i = 0; i < native.size(); ++i)
            pathBuf[i] = static_cast<ushort>(native.at(i).unicode());
        op.wFunc = FO_DELETE;
        op.pFrom = reinterpret_cast<LPCWSTR>(pathBuf.constData());
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        const int rc = SHFileOperationW(&op);
        if (rc != 0 || op.fAnyOperationsAborted) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("media_delete: 删除失败（错误码 %1）—— 可能被占用或权限不足")
                            .arg(rc);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), native);
        out.data.insert(QStringLiteral("recycleBin"), true);
        return out;
    }

    // 播放：用 ffplay 后台打开指定媒体（或媒体库里的默认第一个）。
    if (action == QStringLiteral("media_session_start")) return MediaSession::startMediaSession(params);

    if (action == QStringLiteral("media_session_stop")) return MediaSession::stopMediaSessionAction();

    // ── P3 播放时段管理：配置允许的播放时段窗口 ──
    if (action == QStringLiteral("play_schedule_set")) {
        AgentSettings::setPlayScheduleEnabled(params.value(QStringLiteral("enabled")).toBool(false));
        const QJsonArray wins = params.value(QStringLiteral("windows")).toArray();
        QStringList list;
        for (const QJsonValue &v : wins) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) list.append(s);
        }
        AgentSettings::setPlayWindows(list);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("enabled"), AgentSettings::playScheduleEnabled());
        out.data.insert(QStringLiteral("windows"), QJsonArray::fromStringList(AgentSettings::playWindows()));
        qInfo("[play] 🎵 播放时段配置：enabled=%d, windows=%d", AgentSettings::playScheduleEnabled(), list.size());
        return out;
    }
    if (action == QStringLiteral("play_schedule_get")) {
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("enabled"), AgentSettings::playScheduleEnabled());
        out.data.insert(QStringLiteral("windows"), QJsonArray::fromStringList(AgentSettings::playWindows()));
        out.data.insert(QStringLiteral("allowedNow"), MediaSession::isPlayAllowedNow());
        return out;
    }

    // ── 远控模式（真实会话状态，不是假标记）──
    // start：记下会话状态 + 提高抓屏帧率（远控要看得清，2 秒一帧太慢）
    // stop：恢复默认帧率
    if (action == QStringLiteral("remote_control_start")) {
        if (g_remoteControl) {
            // ⚠️ 2026-10-18 深夜实证修复（"没有画面"根因之一）：**不能静默复用旧会话**。
            //    RFB 是有状态握手：旧客户端若半途而废（管理端崩溃/换机/上次会话没走完），
            //    server 已在旧连接上发过 ProtocolVersion、永远等不到下一步 —— 新客户端
            //    连上来只会在死握手上干等，画面全黑且两端都无报错。
            //    唯一正确姿势：幂等重启 —— 干净停掉旧 server+bridge，再全新拉起，
            //    让新客户端拿到全新握手。
#ifdef STE_USE_LIBVNCSERVER
            qInfo("[agent-qt] remote_control_start：已有会话，重启 VNC（旧握手作废，对新客户端透明）");
            VncBridge::instance().stop();
            VncServer::instance().stop();
#endif
            g_remoteControl = false;   // 落回未启动态，走下面的全新起服路径
        }
        g_remoteControl = true;
        // 修复①③（2026-10-19）：新会话起点。首帧宽限/整屏帧一次性豁免要一个
        // 干净的起点；vnc-data down 保活时间戳清零，看门狗从新会话重新起算。
        g_vncSessionStartMs = QDateTime::currentMSecsSinceEpoch();
        g_lastVncDownMs = 0;
        g_vncMegaDone = false;
        const int want = qBound(1, params.value(QStringLiteral("fps")).toInt(20), 60);
        const int oldFps = g_currentFps;
        g_currentFps = want;
        // 2026-10-19：远控期冻结灵动岛（P0 崩溃规避，见 island.h setSuppressed）。
        // 必须在下面 islandShow 之前置位——否则远控提示本身就会触发 QML Repeater 重算。
        IslandOverlay::setSuppressed(true);
#ifdef STE_USE_LIBVNCSERVER
        // 统一方案 A（2026-10-18）：起服点在这里（对齐原 spec「remote_control_start
        // 启动 VncServer」）。VncServer 自动选 loopback 端口，VncBridge 连它把 RFB
        // 字节打成 vnc-data 帧走主 WS —— 不再需要云端下发 sid/key/port。
        if (!VncServer::instance().isRunning()) {
            const quint16 p = VncServer::instance().start(0, want);
            if (p == 0) {
                qWarning("[agent-qt] FAIL remote_control_start：VncServer 起服失败，回滚会话");
                g_remoteControl = false;
                g_currentFps = oldFps;
                // 修复③顺带（同一生命周期路径）：上面已把灵动岛冻结，
                // 起服失败不回滚这个冻结 ⇒ 岛将永久冻结（与泄漏同型的静默故障）。
                IslandOverlay::setSuppressed(false);
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("VNC 服务起服失败");
                return out;
            }
            qInfo("[agent-qt] 已在 127.0.0.1:%u 起 in-agent VNC Server（远控开始）", (unsigned)p);
        }
        VncBridge::instance().start(VncServer::instance().port());
#endif
        // A档（2026-10-31）：远控期间并起 H264 推流（与 VNC 同生命周期；MF 不可用/0 产出时
        // start 返回 false、后续 emit 0 帧，静默退化不影响主流程）。want 即远控 fps。
        H264Streamer::instance().start(1280, 720, want);
        // 学生端知情（设计文档 6.5 隐私边界）：远控=屏幕正被老师查看，学生必须知道。
        // 灵动岛形态非打扰式提示（#98：统一走灵动岛，禁原生 QMessageBox），不遮操作。
        // 时长给的比原来的 3 秒长一些：远控一整节课都在，学生错过前 3 秒就再没提示了。
        islandShow(QStringLiteral("老师正在远程控制你的屏幕"),
                   QStringLiteral("屏幕画面正实时传给老师端"), QStringLiteral("eye"), 8000);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), true);
        out.data.insert(QStringLiteral("fps"), want);
        out.data.insert(QStringLiteral("fpsBefore"), oldFps);
        out.data.insert(QStringLiteral("sessionSince"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        qInfo("[agent-qt] 🔴 远控会话开启，抓屏帧率 %d → %d fps（已提示学生知情）", oldFps, want);
        return out;
    }

    if (action == QStringLiteral("remote_control_stop")) {
        if (!g_remoteControl) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("remote_control_stop: 当前没有远控会话在进行");
            return out;
        }
        const int was = g_currentFps;
        // 修复③（2026-10-19）：清理逻辑抽到 stopRemoteControlSession（与泄漏
        // 看门狗共用一段，避免两条路径各写一半、日后改漏）。
        stopRemoteControlSession(QStringLiteral("管理端指令"));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), false);
        out.data.insert(QStringLiteral("fps"), g_currentFps);
        out.data.insert(QStringLiteral("fpsWas"), was);
        qInfo("[agent-qt] 🟢 远控会话关闭，抓屏帧率恢复 5 fps");
        return out;
    }

    // ── A6（2026-10-11 起）：噪音检测（§6.5）。2026-10-13 captureRms 已真实现 ──
    // 收到指令才开采集（不常驻占麦）。采样周期由调用方给（默认 2s），每拍把
    // {db, active, seq} 经 type:"noise_sample" 实时回传（订阅该设备的管理端）。
    // 短窗口采集：每拍"开→录 150ms→关"，不驻留录音、不受语音对讲长期占用。
    if (action == QStringLiteral("noise_monitor_start")) {
        // P0-1（2026-10-14）：本地「噪音监测」开关关了，远端指令也跳过（不占麦、不回传）。
        if (!AgentSettings::noiseEnabled()) {
            out.result = QStringLiteral("skipped");
            out.data.insert(QStringLiteral("reason"), QStringLiteral("本地已关闭噪音监测"));
            return out;
        }
        g_actionDeps->ensureNoise();
        const int iv = qBound(500, params.value(QStringLiteral("intervalMs")).toInt(2000), 60000);
        if ((*g_actionDeps->noise)->running()) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("alreadyActive"), true);
            out.data.insert(QStringLiteral("intervalMs"), iv);
            return out;
        }
        (*g_actionDeps->noise)->start(16000, iv);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), true);
        out.data.insert(QStringLiteral("intervalMs"), iv);
        qInfo("[agent-qt] 🎙 噪音检测开启（每 %d ms 一次）", iv);
        return out;
    }

    if (action == QStringLiteral("noise_monitor_stop")) {
        if (!(*g_actionDeps->noise) || !(*g_actionDeps->noise)->running()) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("active"), false);
            out.data.insert(QStringLiteral("wasActive"), false);
            return out;
        }
        (*g_actionDeps->noise)->stop();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("active"), false);
        out.data.insert(QStringLiteral("wasActive"), true);
        out.data.insert(QStringLiteral("lastDb"), (*g_actionDeps->noise)->lastDb());
        qInfo("[agent-qt] 🔇 噪音检测停止");
        return out;
    }

    // ── 自习模式（2026-10-13）：噪音检测 + 噪音异常自动录制 ──
    // 与 noise_monitor_start 的分工：那个是"只测不录"的底层开关；这个是上层**模式**——
    // 除周期回传 dB 外，还带"连续超阈 → 自动录一段 → 上传云端 → 上报 noise_alert"的闭环。
    if (action == QStringLiteral("study_mode_start")) {
        // P0-1（2026-10-14）：自习模式底层也走噪音采集，本地关了就不开。
        if (!AgentSettings::noiseEnabled()) {
            out.result = QStringLiteral("skipped");
            out.data.insert(QStringLiteral("reason"), QStringLiteral("本地已关闭噪音监测"));
            return out;
        }
        const double th = params.value(QStringLiteral("thresholdDb")).toDouble(-45.0);
        const int need = qBound(1, params.value(QStringLiteral("streakNeed")).toInt(3), 60);
        const int recSec = qBound(5, params.value(QStringLiteral("recordSec")).toInt(30), 300);
        const int iv = qBound(500, params.value(QStringLiteral("intervalMs")).toInt(2000), 60000);
        g_actionDeps->ensureNoise();
        (*g_actionDeps->studyOn) = true;
        g_studyThreshDb = th;
        g_studyStreakNeed = need;
        g_studyRecordSec = recSec;
        g_studyStreak = 0;
        g_studyAlertCount = 0;
        (*g_actionDeps->studyStartTs) = logStudyStart(th, need, recSec);   // P3：长期记录（会话开始）
        if (!(*g_actionDeps->noise)->running()) (*g_actionDeps->noise)->start(16000, iv);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("study"), true);
        out.data.insert(QStringLiteral("thresholdDb"), th);
        out.data.insert(QStringLiteral("streakNeed"), need);
        out.data.insert(QStringLiteral("recordSec"), recSec);
        out.data.insert(QStringLiteral("intervalMs"), iv);
        qInfo("[study] 📖 自习模式开启：阈值 %.1f dB / 连续 %d 拍 / 触发录 %d 秒", th, need, recSec);
        return out;
    }

    if (action == QStringLiteral("study_mode_stop")) {
        (*g_actionDeps->studyOn) = false;
        g_studyStreak = 0;
        StudyMode::stopRecordingTimer();   // 自动录制计时器已迁 study_mode 模块
        if ((*g_actionDeps->noise) && (*g_actionDeps->noise)->running()) (*g_actionDeps->noise)->stop();
        logStudyStop((*g_actionDeps->studyStartTs), g_studyAlertCount);   // P3：长期记录（会话结束 + 时长 + 触发数）
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("study"), false);
        out.data.insert(QStringLiteral("alertCount"), g_studyAlertCount);
        qInfo("[study] 📖 自习模式关闭（本次共触发 %d 次异常录制）", g_studyAlertCount);
        return out;
    }

    // 接收文件的一个分片。params: seq（第几片，从 0）+ data（base64）
    // 必须按 seq 顺序到达才追加，乱序如实报错（不猜顺序，也不静默丢弃）。
    if (action == QStringLiteral("file_chunk")) {
        if ((*g_actionDeps->fileRecvTarget).isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 没有打开的接收会话，请先下发 file_push");
            return out;
        }
        const int seq = params.value(QStringLiteral("seq")).toInt(-1);
        if (seq < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: seq 非法");
            return out;
        }
        if (seq != (*g_actionDeps->fileRecvNext)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 期望第 %1 片，收到第 %2 片（乱序，拒绝）")
                            .arg((*g_actionDeps->fileRecvNext)).arg(seq);
            return out;
        }
        const QByteArray data = QByteArray::fromBase64(
            params.value(QStringLiteral("data")).toString().toLatin1());
        if (data.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: data 为空（base64 解码后 0 字节）");
            return out;
        }
        if (!(*g_actionDeps->fileRecvFile) || !(*g_actionDeps->fileRecvFile)->isOpen()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 接收文件句柄已关闭");
            return out;
        }
        const qint64 written = (*g_actionDeps->fileRecvFile)->write(data);
        if (written != data.size()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 写入失败（写入 %1 / 期望 %2 字节）")
                            .arg(written).arg(data.size());
            return out;
        }
        (*g_actionDeps->fileRecvBytes) += data.size();
        (*g_actionDeps->fileRecvNext)++;
        (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();   // 会话还在动，别清理
        // #198（2026-10-11）：每收一片就推一次进度到灵动岛（分母用声明总字节）。
        IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), (*g_actionDeps->fileRecvBytes), (*g_actionDeps->fileRecvTotal));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("seq"), seq);
        out.data.insert(QStringLiteral("bytes"), written);
        out.data.insert(QStringLiteral("totalBytes"), (*g_actionDeps->fileRecvBytes));
        return out;
    }

    // 结束接收：校验接收到的字节数与声明的 total_bytes 一致，否则视为损坏。
    if (action == QStringLiteral("file_done")) {
        if ((*g_actionDeps->fileRecvTarget).isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error =QStringLiteral("file_done: 当前没有正在进行的接收会话");
            return out;
        }
        if ((*g_actionDeps->fileRecvFile)) {
            (*g_actionDeps->fileRecvFile)->flush();
            (*g_actionDeps->fileRecvFile)->close();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
        }
        const qint64 expect = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        // 取消的判据改看**显式标志**，不再拿 total_bytes<=0 顶替：
        // 那样做等于把"0 字节文件"和"取消"焊成同一件事，一个真要推的空文件会被当成取消删掉。
        // 老管理端不会带 cancel 字段，那时仍按 total_bytes<=0 认取消 —— 兼容不变。
        const bool wantCancel = params.value(QStringLiteral("cancel")).toBool(false)
                                || (!params.contains(QStringLiteral("cancel")) && expect <= 0);
        if (wantCancel) {
            // 发端点了"取消"。半截文件**必须删掉**：留着就是这台机器上打不开的残缺文件，
            // 中间这段时间谁点开谁中招。
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("cancelled"), true);
            out.data.insert(QStringLiteral("bytes"), (*g_actionDeps->fileRecvBytes));
            out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget)));
            QFile::remove((*g_actionDeps->fileRecvTarget));
            (*g_actionDeps->fileRecvTarget).clear();
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvLastMs) = 0;
            IslandOverlay::instance()->clearFileTransfer();   // #198：取消 ⇒ 进度卡撤掉
            return out;
        }
        if (expect != (*g_actionDeps->fileRecvBytes)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_done: 字节数不符（收到 %1 / 声明 %2），文件已删除")
                            .arg((*g_actionDeps->fileRecvBytes)).arg(expect);
            QFile::remove((*g_actionDeps->fileRecvTarget));
            (*g_actionDeps->fileRecvTarget).clear();
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvFile) = nullptr;   // 句柄前面已经关掉了
            IslandOverlay::instance()->clearFileTransfer();   // #198：字节不符 ⇒ 进度卡撤掉
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget)));
        out.data.insert(QStringLiteral("bytes"), (*g_actionDeps->fileRecvBytes));
        out.data.insert(QStringLiteral("chunks"), (*g_actionDeps->fileRecvNext));
        // ── 2026-10-11：「收到文件」的提示从这里发，不再在 file_push 里发 ──
        // 以前开会话的瞬间就报"收到文件"，可那时文件刚被 Truncate 成 0 字节、
        // 一片数据都还没到 —— 学生/老师点开就是个空文件（用户报的"推完的文件是空的"）。
        // 现在放到字节数**比对通过之后**，文件名带体积，让人一眼知道收全了没有。
        {
            const QString shown = QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget));
            const QString desc = QFileInfo((*g_actionDeps->fileRecvTarget)).fileName()
                                 + QStringLiteral("（") + g_actionDeps->humanBytes((*g_actionDeps->fileRecvBytes))
                                 + QStringLiteral("）");
            islandShow(QStringLiteral("收到文件"), desc, QStringLiteral("download"), 6000,
                       true, shown);
            trayNotify(QStringLiteral("收到文件"), desc, QSystemTrayIcon::Information, 10000);
            IslandOverlay::instance()->clearFileTransfer();   // #198：收全 ⇒ 进度卡撤掉，改显"收到文件"
        }
        qInfo("[agent-qt] ✅ 文件收全落盘：%s（%lld 字节 / %d 片）",
              qPrintable((*g_actionDeps->fileRecvTarget)), (long long)(*g_actionDeps->fileRecvBytes), (*g_actionDeps->fileRecvNext));
        (*g_actionDeps->fileRecvTarget).clear();
        (*g_actionDeps->fileRecvNext) = 0;
        (*g_actionDeps->fileRecvBytes) = 0;
        (*g_actionDeps->fileRecvLastMs) = 0;
        return out;
    }

    // ── file_push：接收远程发来的文件 ──
    // 走独立的消息类型（file_chunk / file_done），见下方 handleControlText 的分支。
    // 这个 action 只是"初始化接收会话"：校验目标路径、清掉旧的半截文件。
    if (action == QStringLiteral("file_push")) {
        const QString fname = params.value(QStringLiteral("name")).toString().trimmed();
        const qint64 total = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        if (fname.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 需要 name（文件名）");
            return out;
        }
        if (!fname.contains(QLatin1Char('.'))) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名 %1 缺少扩展名，拒绝（防无后缀可执行）").arg(fname);
            return out;
        }
        // 目标必须落在收件目录内：只取文件名，丢弃任何路径成分
        const QString clean = QFileInfo(fname).fileName();
        if (clean != fname) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名不能带路径（收到 %1）").arg(fname);
            return out;
        }
        // #198（2026-10-11）：进度卡用。先把名字/总字节记下来，开好句柄后在成功分支点亮灵动岛。
        (*g_actionDeps->fileRecvName) = clean;
        (*g_actionDeps->fileRecvTotal) = total;
        QDir().mkpath((*g_actionDeps->recvDir));
        const QString target = (*g_actionDeps->recvDir) + QStringLiteral("/") + clean;
        QFile::remove(target);   // 重名直接覆盖旧文件

        // 开接收句柄（2026-10-04 补上）。原来这两样（收件目录 + 这个句柄）都只有声明没有实现：
        // 结果就是落盘路径跑到当前盘根目录、file_chunk 一律"接收文件句柄已关闭"。
        // 顺带先摘掉上一次没收口的会话 —— 不摘的话两个会话抢同一个句柄，写进去的两批数据会搅一起。
        if ((*g_actionDeps->fileRecvFile)) {
            (*g_actionDeps->fileRecvFile)->close();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
        }
        (*g_actionDeps->fileRecvTarget) = target;
        (*g_actionDeps->fileRecvBytes) = 0;
        (*g_actionDeps->fileRecvNext) = 0;
        (*g_actionDeps->fileRecvFile) = new QFile(target);
        // Truncate 显式带上：只给 WriteOnly 时，撞上"文件存在但处于删除挂起状态"
        // （刚被 QFile::remove 掉、而句柄还没被系统真正释放）会直接失败。
        if (!(*g_actionDeps->fileRecvFile)->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // 失败必须带**系统的原话**回来。以前只报"目标打不开（路径）"，
            // 于是目录不存在 / 没权限 / 被别的进程占着 / 删不掉 —— 四种病长得一模一样，
            // 每次都得从零猜一遍（2026-10-07 就因为这句含糊的话误判成"目录没建"）。
            const QString why = (*g_actionDeps->fileRecvFile)->errorString();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
            // 同名打不开就换个名字再试一次：收件目录里撞名的往往是上一次没收口的残留，
            // 换名能让这次推送**成功**，而不是让用户对着一句"打不开"干瞪眼。
            const QString alt = (*g_actionDeps->recvDir) + QStringLiteral("/") +
                                QFileInfo(clean).completeBaseName() + QStringLiteral("-") +
                                QString::number(QDateTime::currentSecsSinceEpoch()) +
                                QStringLiteral(".") + QFileInfo(clean).suffix();
            (*g_actionDeps->fileRecvFile) = new QFile(alt);
            if (!(*g_actionDeps->fileRecvFile)->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("file_push: 目标打不开（%1）：%2；换名 %3 也失败：%4")
                                .arg(target, why, alt, (*g_actionDeps->fileRecvFile)->errorString());
                delete (*g_actionDeps->fileRecvFile);
                (*g_actionDeps->fileRecvFile) = nullptr;
                (*g_actionDeps->fileRecvTarget).clear();
                return out;
            }
            qInfo("[agent] file_push 原名打不开（%s），已换名落盘 %s",
                  qPrintable(why), qPrintable(alt));
            (*g_actionDeps->fileRecvTarget) = alt;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(alt));
            out.data.insert(QStringLiteral("totalBytes"), total);
            out.data.insert(QStringLiteral("ready"), true);
            IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), 0, (*g_actionDeps->fileRecvTotal));
            // ⚠️ 这里**不提示**"收到文件"。此刻只是把接收句柄开好、目标刚被 Truncate 成
            // 0 字节，一片数据都还没到 —— 在这儿报"收到"就是让人去打开一个空文件
            // （2026-10-11 用户报的"推完的文件是空的"正是这条）。
            // 真正的提示挪到 file_done 字节数比对通过之后。
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("totalBytes"), total);
        out.data.insert(QStringLiteral("ready"), true);
        IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), 0, (*g_actionDeps->fileRecvTotal));
        (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();   // 同上：此刻不提示
        return out;
    }

    // 2026-10-04（乙阶段 2）：大屏通知（notify）——popup 居中弹窗 / island 灵动岛 / fullscreen 全屏遮罩，
    // 可配 TTS 朗读。站点广播（broadcast.ts）改走云端 WS 指令通道后，教室大屏不再依赖 CIMS。
    if (action == QStringLiteral("notify")) {
        const QString err = notifyFromParams(params);
        // 2026-10-21（审查 #7）："skipped:" 前缀 = 门控静默丢弃（考试中），不是失败
        //   —— 管理端不该看到一条 failed 去重试/报警。
        if (err.startsWith(QLatin1String("skipped:"))) {
            out.result = QStringLiteral("skipped");
            out.error = err.mid(8);
            return out;
        }
        if (!err.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = err;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("notice_id"),
                        params.value(QStringLiteral("notice_id")).toVariant().toJsonValue());
        out.data.insert(QStringLiteral("kind"),
                        params.value(QStringLiteral("kind")).toString(QStringLiteral("popup")));
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
