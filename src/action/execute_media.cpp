// 星集控被控端 · 媒体与远控指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：相机六件套 / 媒体库 / 播放时段 / 远控会话 / 噪音采集 / 自习模式。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"

ExecOut executeMediaAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // ── 相机六件套（走 ffmpeg DirectShow 输入）──
    // ⚠️ 2026-10-23（AGENTS.md §10 第 3 刀）：分支体已下沉到 src/media/CameraActions.cpp。
    //    这里只留分支判断与调用；**函数体是逐字搬的**，行为零变化。
    if (action == QStringLiteral("camera_list")) { handled = true; return CameraActions::listDevices(); }

    // 绑定摄像头：管理端「监控中心」选定一枚后落在这里，之后抓拍/录制默认用它。
    // device 传空 = 解绑（回落默认）。绑定存本机（只有本机能枚举），不落 agent.env。
    if (action == QStringLiteral("camera_bind")) { handled = true; return CameraActions::bindDevice(params); }

    // 摄像头自检：按绑定设备（或指定 device）真抓一帧，只回"能不能用 + 耗时 + 原因"。
    // 与 camera_snapshot 的区别：这是**诊断**用途（管理端「测试」按钮），不管画面好看不好看。
    if (action == QStringLiteral("camera_test")) { handled = true; return CameraActions::selfTest(params); }

    // 拍一张：ffmpeg 从指定设备（默认 video0）抓一帧写 PNG。回执给路径 + 字节数 + base64 缩略图。
    if (action == QStringLiteral("camera_snapshot")) { handled = true; return CameraActions::snapshot(params); }

    // 开始录像：后台起 ffmpeg 进程写 mp4。已经在录就如实报 failed（不静默换文件）。
    if (action == QStringLiteral("camera_record_start")) { handled = true; return CameraActions::recordStart(params); }

    // 停止录像。回执给实际字节数——0 字节就是失败，不许报 done。
    if (action == QStringLiteral("camera_record_stop")) { handled = true; return CameraActions::recordStop(); }

    // ── 媒体四件套（扫媒体库目录；删除走回收站）──
    if (action == QStringLiteral("media_list")) {
        handled = true;
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
        handled = true;
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
    if (action == QStringLiteral("media_session_start")) { handled = true; return MediaSession::startMediaSession(params); }

    if (action == QStringLiteral("media_session_stop")) { handled = true; return MediaSession::stopMediaSessionAction(); }

    // ── P3 播放时段管理：配置允许的播放时段窗口 ──
    if (action == QStringLiteral("play_schedule_set")) {
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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
        handled = true;
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

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
