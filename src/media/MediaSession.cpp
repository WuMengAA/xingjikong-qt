// 实现：2026-10-22 逐字搬自 main.cpp（AGENTS.md §10 第二刀）。

#include "MediaSession.h"
#include "MediaTools.h"
#include "../core/ProcessUtil.h"

#include "../island.h"         // IslandOverlay::instance()->clearMediaSession()
#include "../winlauncher.h"    // stelarithHideChildConsole（子进程隐藏控制台，见 CMD 弹窗根治）

#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>

// ── 跨 TU 引用 main.cpp 的状态（第三刀抽相机分支时收进来，届时删本段）──────
extern QProcess *g_recProc;      // camera_record 正在录制的 ffmpeg 进程
extern QString   g_recOutPath;   // 正在录制的输出 mp4 路径
extern QProcess *g_playProc;     // media_session 正在播放的 ffplay 进程
extern QTimer   *g_mediaTick;    // 岛底「已播时长」1s 计时器
extern QTimer   *g_mediaAutoStop;// 超播放时段窗口自动截断
extern bool      g_mediaStopping;// 本次停止是我们主动发起的
extern QString   g_shotDir;      // 截图/存图目录（screenshot 也用它，故留 main.cpp）
void uploadRecordingAsync(const QString &mp4Path);   // 定义在 main.cpp（云端上传域）

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

} // namespace MediaSession
