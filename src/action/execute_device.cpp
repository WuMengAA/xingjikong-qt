// 星集控被控端 · 设备系统指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：锁屏 / 关机重启 / 键鼠输入 / 音量 / 截图 / 探针与执行测试。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"

ExecOut executeDeviceAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // ── dev_act：仅开发用的动作触发器（2026-10-17 T 档验证设施）──────────────
    // 为什么需要：T 档要验"多种进行中状态在岛上并存"，而岛的状态源分散在
    //   通知 / 文件传输 / 媒体会话 / 对讲频谱 / 展开收起 等模块里，**没有一条统一的入口**；
    //   用云端指令触发要连真云端、用插件口又碰不到媒体与频谱。这里给沙箱一条内建口令。
    // ⚠️ 唯一的护栏：**必须 STE_QT_UID 以 `ste-sbx-` 开头**（沙箱实例专用命名前缀），
    //   生产实例即便被下发 dev_act 也只会回 failed，不会执行。这是"默认关闭、显式开启"，
    //   而不是"有口令就能用"——口令本身可能泄到管理端脚本里。
    if (action == QStringLiteral("dev_act")) {
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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

    // 最便宜的探针：确认"指令真走到了被控端"，而不是只看云端说"发出去了"
    if (action == QStringLiteral("ping")) {
        handled = true;
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
        handled = true;
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
        handled = true;
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

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
