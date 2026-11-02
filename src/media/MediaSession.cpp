// 实现：2026-10-22 逐字搬自 main.cpp（AGENTS.md §10 第二刀）。

#include "MediaSession.h"
#include "MediaTools.h"
#include "../core/ProcessUtil.h"
#include "../core/AgentSettings.h"   // 播放时段开关（第四刀地基：原嵌在 main.cpp 匿名域内）

#include "../island/island.h"         // IslandOverlay::instance()->clearMediaSession()
#include "../system/winlauncher.h"    // stelarithHideChildConsole（子进程隐藏控制台，见 CMD 弹窗根治）

#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTime>               // 播放时段窗口判定（HH:mm 解析 + 跨午夜）
#include <QStringList>
#include <QRegularExpression>

// ── 媒体会话状态（2026-10-23 第四刀：从 main.cpp 收归本模块）──────────────
// 变量名**刻意保持与原先一致**（g_recProc / g_playProc …），这样下面所有逐字搬来的
// 函数体一个字都不用改 —— 收状态这件事不该连带改动业务逻辑。
//
// 为什么可以收：第四刀把 media_session_start/stop 分支也抽走了，main.cpp 里这 6 个
// 状态只剩定义行（外加 1 处 g_recOutPath 的状态显示读）⇒ 调用面已收敛到本模块。
//
// 为什么是文件内 static 而不是 extern：它们本就该只由媒体会话代码动。外部（相机指令
// 的 recordStart/recordStop、状态回执）一律走本模块提供的访问器，不直接碰变量。
static QProcess *g_recProc = nullptr;      // camera_record 正在录制的 ffmpeg 进程
static QString   g_recOutPath;             // 正在录制的输出 mp4 路径
static QProcess *g_playProc = nullptr;     // media_session 正在播放的 ffplay 进程
static QTimer   *g_mediaTick = nullptr;    // 岛底「已播时长」1s 计时器
static QTimer   *g_mediaAutoStop = nullptr;// 超播放时段窗口自动截断
static bool      g_mediaStopping = false;  // 本次停止是我们主动发起的

// ── 仍跨 TU 引用 main.cpp 的（不属于媒体会话域，等各自那一刀）──────────────
extern QString   g_shotDir;      // 截图/存图目录（screenshot / 抓屏 / 相机自检共用 ⇒ 存储域）
extern QString   g_mediaDir;     // 媒体库目录（media_list 默认扫这里 ⇒ 存储域）
#include "../upload/recording_upload.h"   // 云端上传域（录像完成后自动上传 recordings/，原在 main.cpp）

namespace MediaSession {

// ── 录像启停（2026-10-13 抽出，供 camera_record_* 与自习模式自动录制共用）──────
// 这两段逻辑原本只在 executeAction 的 camera_record_* 分支里各一份；自习模式的
// "噪音异常自动录制"要复用它们 —— 若不抽出就会变成"两处各写一遍、改一处漏一处"
// （跨端审计报告点过这个坑）。行为与原地逐字一致，只把结果从 out.xxx 换成返回值。
QString beginRecording(const QString &dev, int maxSec)
{
    const QString ff = MediaTools::ffmpegBin();
    if (ff.isEmpty()) return QStringLiteral("找不到 ffmpeg");
    if (g_recProc && g_recProc->state() != QProcess::NotRunning) {
        return QStringLiteral("已有一条录像在进行中（%1），请先 record_stop").arg(
            QDir::toNativeSeparators(g_recOutPath));
    }
    QDir().mkpath(g_shotDir);
    g_recOutPath = g_shotDir + QStringLiteral("/rec-%1.mp4").arg(QDateTime::currentMSecsSinceEpoch());
    g_recProc = new QProcess();
    g_recProc->setProcessChannelMode(QProcess::MergedChannels);
    // #368 需求①：录像期间 ffmpeg 控制台窗会一直挂在桌上，必须 CREATE_NO_WINDOW
    stelarithHideChildConsole(*g_recProc);
    g_recProc->start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                           QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                           QStringLiteral("dshow"), QStringLiteral("-i"),
                           QStringLiteral("video=%1").arg(dev),
                           QStringLiteral("-t"), QString::number(maxSec),
                           QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
                           QStringLiteral("ultrafast"), QStringLiteral("-crf"), QStringLiteral("28"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                           QStringLiteral("-r"), QStringLiteral("15"), g_recOutPath });
    if (!g_recProc->waitForStarted(4000)) {
        const int code = g_recProc->exitCode();
        delete g_recProc; g_recProc = nullptr;
        return QStringLiteral("ffmpeg 启动失败（退出码 %1，设备 %2）").arg(code).arg(dev);
    }
    return QString();
}

// 停止录像：返回 true = 有有效文件（并已发起上传）；bytes/code/err 为明细。
// ⚠️ 容忍 ffmpeg 因 -t 到点**自己结束**（那时 state 已是 NotRunning，但文件是好的）——
//    自习模式的自动录制到点常走这条；不能因为"进程已退出"就当成"没有录像"。
bool endRecording(qint64 *outBytes, QString *outErr, int *outCode)
{
    if (!g_recProc) {
        if (outErr) *outErr = QStringLiteral("当前没有录像在进行");
        return false;
    }
    if (g_recProc->state() != QProcess::NotRunning) {
        g_recProc->terminate();
        if (!ProcessUtil::waitFinished(*g_recProc, 5000)) { g_recProc->kill(); ProcessUtil::waitFinished(*g_recProc, 2000); }
    } else {
        ProcessUtil::waitFinished(*g_recProc, 1000);   // 已自然结束，等它彻底收尾
    }
    const int code = g_recProc->exitCode();
    const qint64 bytes = QFileInfo(g_recOutPath).exists() ? QFileInfo(g_recOutPath).size() : 0;
    delete g_recProc; g_recProc = nullptr;
    if (outCode) *outCode = code;
    if (bytes <= 0) {
        QFile::remove(g_recOutPath);
        if (outErr) *outErr = QStringLiteral("没有产生有效文件（退出码 %1）").arg(code);
        return false;
    }
    if (outBytes) *outBytes = bytes;
    uploadRecordingAsync(g_recOutPath);   // 完成后自动上传云端 recordings/（上传失败不改本函数结论）
    return true;
}

// 停掉媒体会话：终止 ffplay + 清空岛底「正在播放」+ 停计时器。media_session_stop 与
// 自动截断计时器共用，避免两份停止逻辑。
void stopMediaSession()
{
    g_mediaStopping = true;   // 组件7：本次停止是我们发起的，finished 不当"播完"
    if (g_mediaTick) { g_mediaTick->stop(); }
    if (g_mediaAutoStop) { g_mediaAutoStop->stop(); }
    if (g_playProc && g_playProc->state() != QProcess::NotRunning) {
        g_playProc->terminate();
        if (!ProcessUtil::waitFinished(*g_playProc, 3000)) g_playProc->kill();
    }
    delete g_playProc; g_playProc = nullptr;
    g_mediaStopping = false;
    if (IslandOverlay::instance()) IslandOverlay::instance()->clearMediaSession();
}

bool isStopping() { return g_mediaStopping; }

// ── 状态访问器（第四刀新增：相机指令与状态回执改走这里，不再直接引用全局）──
QProcess *recProc() { return g_recProc; }
void setRecProc(QProcess *p) { if (g_recProc) delete g_recProc; g_recProc = p; }
QString recOutPath() { return g_recOutPath; }
void setRecOutPath(const QString &p) { g_recOutPath = p; }

// ── 播放时段判定（2026-10-23 逐字搬自 main.cpp）──────────────────────
// 没开时段限制 → 随时可播；开了但窗口为空 → 也放开；否则落在任一窗口内才放行。
bool isPlayAllowedNow()
{
    if (!AgentSettings::playScheduleEnabled()) return true;
    const QStringList wins = AgentSettings::playWindows();
    if (wins.isEmpty()) return true;
    const QTime now = QTime::currentTime();
    for (const QString &w : wins) {
        const QStringList p = w.split(QStringLiteral("-"));
        if (p.size() != 2) continue;
        const QTime a = QTime::fromString(p.at(0).trimmed(), QStringLiteral("HH:mm"));
        const QTime b = QTime::fromString(p.at(1).trimmed(), QStringLiteral("HH:mm"));
        if (!a.isValid() || !b.isValid()) continue;
        if (a <= b) { if (now >= a && now <= b) return true; }
        else        { if (now >= a || now <= b) return true; }   // 跨午夜窗口
    }
    return false;
}

qint64 remainingMsInWindow()
{
    if (!AgentSettings::playScheduleEnabled()) return -1;
    const QStringList wins = AgentSettings::playWindows();
    if (wins.isEmpty()) return -1;
    const QTime now = QTime::currentTime();
    qint64 best = -1;
    for (const QString &w : wins) {
        const QStringList p = w.split(QStringLiteral("-"));
        if (p.size() != 2) continue;
        const QTime a = QTime::fromString(p.at(0).trimmed(), QStringLiteral("HH:mm"));
        const QTime b = QTime::fromString(p.at(1).trimmed(), QStringLiteral("HH:mm"));
        if (!a.isValid() || !b.isValid()) continue;
        qint64 rem = -1;
        if (a <= b) { if (now >= a && now <= b) rem = QDateTime::currentDateTime().secsTo(QDateTime(QDate::currentDate(), b)) * 1000; }
        else        { if (now >= a) rem = QDateTime::currentDateTime().secsTo(QDateTime(QDate::currentDate(), b)) * 1000;
                      else if (now <= b) rem = QDateTime::currentDateTime().secsTo(QDateTime(QDate::currentDate().addDays(1), b)) * 1000; }
        if (rem >= 0 && (best < 0 || rem < best)) best = rem;
    }
    return best;
}

// ── 播放会话指令（2026-10-23 逐字搬自 executeAction）──────────────────
ExecOut startMediaSession(const QJsonObject &params)
{
    ExecOut out;
    const QString fp = MediaTools::ffplayBin();
    if (fp.isEmpty()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("media_session_start: 找不到 ffplay（可设 STE_QT_FFPLAY）");
        return out;
    }
    // P3 播放时段管理（"最大最小适配播放时段"）：开了时段限制且当前不在窗口内 → 拦。
    if (AgentSettings::playScheduleEnabled() && !MediaSession::isPlayAllowedNow()) {
        out.result = QStringLiteral("blocked");
        out.error = QStringLiteral("media_session_start: 当前不在允许的播放时段内");
        out.data.insert(QStringLiteral("allowed"), false);
        // 组件7（2026-10-15）：被时段拦住 → 学生端的岛也显示"禁播"这一态。
        // 原来只有管理端能看到 blocked 回执，学生端一片安静：点了"点歌"没反应，
        // 最容易被当成"机器坏了"。这里让岛把"为什么没播"讲清楚（3.5s 自动收）。
        if (IslandOverlay::instance())
            IslandOverlay::instance()->mediaBlocked(QStringLiteral("当前时段不开放播放"));
        return out;
    }
    QString target = params.value(QStringLiteral("path")).toString().trimmed();
    if (target.isEmpty()) {
        QDir d(g_mediaDir);
        const QFileInfoList ls = d.entryInfoList(
            { QStringLiteral("*.mp4"), QStringLiteral("*.mp3") }, QDir::Files, QDir::Name);
        if (!ls.isEmpty()) target = ls.first().absoluteFilePath();
    }
    if (target.isEmpty()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("media_session_start: 没有指定 path，且媒体库里也没有可播放文件");
        return out;
    }
    if (!QFileInfo::exists(target)) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("media_session_start: 文件不存在 %1").arg(QDir::toNativeSeparators(target));
        return out;
    }
    if (g_playProc && g_playProc->state() != QProcess::NotRunning) {
        g_playProc->terminate();
        if (!ProcessUtil::waitFinished(*g_playProc, 3000)) g_playProc->kill();
        delete g_playProc;
        g_playProc = nullptr;
    }
    g_playProc = new QProcess();
    g_playProc->setProcessChannelMode(QProcess::MergedChannels);
    // #368 需求①：播放期间 ffplay 控制台窗会一直挂在桌上，必须 CREATE_NO_WINDOW
    stelarithHideChildConsole(*g_playProc);
    // ── 组件7（2026-10-15）：「播完」态的接线 ────────────────────
    // ffplay 带 -autoexit，播完会**自己退出** —— 原来没人接住这个信号，
    // 岛就一直停在"正在播放"，直到老师手动停 / 时段截断。这里接住：
    // 非主动停止（g_mediaStopping=false）才算"播完"。
    // ⚠️ context 传 g_playProc：进程对象被 delete 时连接自动断开，
    //    lambda 不会在野指针上跑。
    QObject::connect(g_playProc, &QProcess::finished, g_playProc,
                     [](int, QProcess::ExitStatus) {
        if (g_mediaStopping) return;              // 我们主动停的，不是播完
        if (g_mediaTick) g_mediaTick->stop();
        if (IslandOverlay::instance()) IslandOverlay::instance()->mediaFinished();
    });
    g_playProc->start(fp, { QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), target });
    if (!g_playProc->waitForStarted(3000)) {
        const int code = g_playProc->exitCode();
        delete g_playProc; g_playProc = nullptr;
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("media_session_start: ffplay 启动失败（退出码 %1）").arg(code);
        return out;
    }
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(target));
    out.data.insert(QStringLiteral("pid"), (int)g_playProc->processId());
    // P3 播放时段管理：岛底「正在播放」（标题/总时长/下一首/封面）+ 1s 计时器推进已播。
    const QString mTitle = params.value(QStringLiteral("title")).toString();
    const qint64 mTotal = (qint64)params.value(QStringLiteral("totalMs")).toDouble(0);
    const QString mNext  = params.value(QStringLiteral("nextTitle")).toString();
    const QString mCover = params.value(QStringLiteral("nextCover")).toString();
    if (IslandOverlay::instance())
        IslandOverlay::instance()->setMediaSession(
            mTitle.isEmpty() ? QFileInfo(target).fileName() : mTitle, mTotal, mNext, mCover);
    if (!g_mediaTick) {
        g_mediaTick = new QTimer();
        g_mediaTick->setInterval(1000);
        QObject::connect(g_mediaTick, &QTimer::timeout, [] {
            if (IslandOverlay::instance()) IslandOverlay::instance()->advanceMediaElapsed(1000);
        });
    }
    g_mediaTick->start();
    // 最大最小适配：给定总时长超过本窗口剩余 → 到点自动截断（不让播放溢出时段）。
    if (g_mediaAutoStop) g_mediaAutoStop->stop();
    if (mTotal > 0) {
        const qint64 rem = MediaSession::remainingMsInWindow();
        const qint64 stopMs = (rem > 0 ? qMin(mTotal, rem) : mTotal);
        if (stopMs > 0) {
            if (!g_mediaAutoStop) {
                g_mediaAutoStop = new QTimer();
                QObject::connect(g_mediaAutoStop, &QTimer::timeout, [] { MediaSession::stopMediaSession(); });
            }
            g_mediaAutoStop->setSingleShot(true);
            g_mediaAutoStop->start((int)qMin(stopMs, (qint64)0x7FFFFFFF));
        }
    }
    return out;
}

ExecOut stopMediaSessionAction()
{
    ExecOut out;
    if (!g_playProc) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("media_session_stop: 当前没有在播放");
        return out;
    }
    // 「停止」的结果就是**播放停了** —— 不管是被我们停的，还是播放器自己播完退出的
    // （本机没可用输出设备时 ffplay 会秒退）。原来一律回 failed「当前没有在播放」，
    // 老师点了停止却看到"没在播放"，等于把"已经停了"说成了"做失败"。
    const int pid = (int)g_playProc->processId();
    const bool wasRunning = (g_playProc->state() != QProcess::NotRunning);
    MediaSession::stopMediaSession();   // P3：统一停止（终止 ffplay + 清岛底 + 停计时器），与自动截断共用
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("stopped"), true);
    out.data.insert(QStringLiteral("pid"), pid);
    out.data.insert(QStringLiteral("wasRunning"), wasRunning);
    if (!wasRunning)
        out.data.insert(QStringLiteral("note"),
                        QStringLiteral("播放器此前已自行退出（播完或无输出设备），不是我们停的"));
    return out;
}

} // namespace MediaSession
