// 实现：2026-10-23 逐字搬自 main.cpp 的 executeAction（AGENTS.md §10 第 3 刀）。

#include "CameraActions.h"
#include "MediaTools.h"
#include "MediaSession.h"
#include "../core/ProcessUtil.h"

#include "../system/winlauncher.h"  // stelarithHideChildConsole（子进程隐藏控制台，见 CMD 弹窗根治）

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QThread>

// ── 跨 TU 引用 main.cpp 的状态 ──────────────────────────────────────
// 2026-10-23 第四刀：g_recProc / g_recOutPath 已收归 MediaSession（录像状态本就属于
// 媒体会话模块），本文件改走 MediaSession::recProc() / recOutPath() 访问器。
// 只剩 g_shotDir —— 它属「存储/路径」域，被 media / screenshot / 抓屏 三处共用，
// 收进 media 会固化反向依赖，等存储域单独一刀处理。
extern QString   g_shotDir;     // 截图/存图目录（screenshot 动作与抓屏也用它）

namespace CameraActions {

ExecOut listDevices()
{
    ExecOut out;
    const QString ff = MediaTools::ffmpegBin();
    if (ff.isEmpty()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("找不到 ffmpeg（可设 STE_QT_FFMPEG 指向 ffmpeg.exe）");
        return out;
    }
    // ⚠️ `-list_devices` **必须带布尔值 `true`**：只写 `-list_devices` 会把紧跟的 `-f`
    //    当成它的值吃掉，ffmpeg 于是直接去开 dummy，**一条设备都不打印**（2026-10-09 实测真因）。
    //    另外别再挂 `-loglevel error` —— 设备列表是 info 级输出的，挂上就全被吞。
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // #368 需求①：设备枚举也走 ffmpeg（控制台程序），不藏会弹窗
    stelarithHideChildConsole(p);
    p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-list_devices"),
                  QStringLiteral("true"), QStringLiteral("-f"),
                  QStringLiteral("dshow"), QStringLiteral("-i"),
                  QStringLiteral("dummy") });
    if (!ProcessUtil::waitFinished(p, 4000)) {
        p.kill();
        ProcessUtil::waitFinished(p, 1500);
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("ffmpeg 枚举设备超时（DirectShow 可能不可用）");
        return out;
    }
    const QString txt = QString::fromUtf8(p.readAll());
    QJsonArray cams, mics, details;
    MediaTools::parseDshowDevices(txt, cams, mics, &details);
    int virt = 0;
    for (const QJsonValue &v : details)
        if (v.toObject().value(QStringLiteral("virtual")).toBool()) ++virt;
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("cameras"), cams);
    out.data.insert(QStringLiteral("camerasDetailed"), details);   // 含 virtual 标记
    out.data.insert(QStringLiteral("virtualCount"), virt);
    out.data.insert(QStringLiteral("microphones"), mics);
    out.data.insert(QStringLiteral("count"), cams.size());
    out.data.insert(QStringLiteral("boundDevice"), MediaTools::boundCamera());   // "" = 未绑定
    return out;
}

ExecOut bindDevice(const QJsonObject &params)
{
    ExecOut out;
    const QString dev = params.value(QStringLiteral("device")).toString().trimmed();
    if (!MediaTools::saveBoundCamera(dev)) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_bind: 无法写入绑定文件 %1")
                        .arg(QDir::toNativeSeparators(MediaTools::cameraDevPath()));
        return out;
    }
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("boundDevice"), dev);
    out.data.insert(QStringLiteral("note"), dev.isEmpty()
                                                  ? QStringLiteral("已解绑，抓拍/录制回落默认设备")
                                                  : QStringLiteral("已绑定，抓拍/录制默认用它"));
    return out;
}

ExecOut selfTest(const QJsonObject &params)
{
    ExecOut out;
    const QString ff = MediaTools::ffmpegBin();
    if (ff.isEmpty()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("找不到 ffmpeg（可设 STE_QT_FFMPEG）");
        return out;
    }
    QString dev = params.value(QStringLiteral("device")).toString().trimmed();
    const bool fromBind = dev.isEmpty();
    if (dev.isEmpty()) dev = MediaTools::boundCamera();
    if (dev.isEmpty()) dev = QStringLiteral("video0");

    QDir().mkpath(g_shotDir);
    const QString path = g_shotDir + QStringLiteral("/_camtest-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    // ⚠️ 输入串是 `video=<设备名>`。**不是** `video_device=` —— 后者 ffmpeg 直接报
    //    "Malformed dshow input string"（2026-10-09 实测，旧实现对相机永远打不开）。
    // #368 需求①：抓拍也走 ffmpeg（控制台程序），不藏会弹窗
    stelarithHideChildConsole(p);
    p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                  QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                  QStringLiteral("dshow"), QStringLiteral("-i"), QStringLiteral("video=%1").arg(dev),
                  QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"),
                  QStringLiteral("2"), path });
    const bool finished = ProcessUtil::waitFinished(p, 9000);
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - t0;
    if (!finished) {
        p.kill();
        ProcessUtil::waitFinished(p, 1500);
        QFile::remove(path);
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("自检超时（%1ms，设备 %2）：设备被占用或不出帧").arg(elapsed).arg(dev);
        out.data.insert(QStringLiteral("errorCode"), QStringLiteral("timeout"));
        out.data.insert(QStringLiteral("device"), dev);
        out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
        return out;
    }
    const qint64 bytes = QFileInfo::exists(path) ? QFileInfo(path).size() : 0;
    if (p.exitCode() != 0 || bytes == 0) {
        QFile::remove(path);
        const QString raw = QString::fromUtf8(p.readAllStandardError()).trimmed();
        // ⚠️ 2026-10-12：原来这里把 ffmpeg 原生 stderr 直接拼进 out.error 上报，
        //    管理端界面于是显示 `Unable to find a matching device` 这种只有开发者看得懂的话。
        //    现在：out.error 只给**人话**结论；errorCode 给界面决定要不要「重试」；
        //    errorRaw 留给诊断页/日志，绝不上界面。
        const MediaTools::CamErr ce = MediaTools::classifyCamError(raw);
        out.result = QStringLiteral("failed");
        out.error  = QStringLiteral("自检失败：%1（设备 %2）").arg(ce.msg, dev);
        out.data.insert(QStringLiteral("errorCode"), ce.code);
        out.data.insert(QStringLiteral("errorRaw"), raw.left(300));
        out.data.insert(QStringLiteral("retryable"), ce.retryable);
        out.data.insert(QStringLiteral("device"), dev);
        out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
        return out;
    }
    QFile::remove(path);   // 自检不留垃圾
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("device"), dev);
    out.data.insert(QStringLiteral("fromBinding"), fromBind);
    out.data.insert(QStringLiteral("elapsedMs"), (int)elapsed);
    out.data.insert(QStringLiteral("bytes"), (qint64)bytes);
    out.data.insert(QStringLiteral("ok"), true);
    return out;
}

ExecOut snapshot(const QJsonObject &params)
{
    ExecOut out;
    const QString ff = MediaTools::ffmpegBin();
    if (ff.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("找不到 ffmpeg"); return out; }
    // 设备：显式 device > 本机绑定的摄像头 > video0（2026-10-09）
    QString dev = params.value(QStringLiteral("device")).toString().trimmed();
    if (dev.isEmpty()) dev = MediaTools::boundCamera();
    if (dev.isEmpty()) dev = QStringLiteral("video0");
    if (dev.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("camera_snapshot: device 不能为空"); return out; }
    QDir().mkpath(g_shotDir);
    const QString path = g_shotDir + QStringLiteral("/cam-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    QProcess p;
    // 通道必须分着收：之前用 MergedChannels，ffmpeg 把"设备不存在"写进 stderr，
    // 结果 readAllStandardError() 永远是空的 → 回执里 reason 是**空字符串**，
    // 排障的人只看到"失败"，看不到为什么失败（失败必须给原因，这条红线上栽过）。
    p.setProcessChannelMode(QProcess::SeparateChannels);
    // -frames:v 1 = 只要一帧；-y 覆盖；抽帧失败（设备不存在/占用）ffmpeg 会非零退出
    // #368 需求①：截图也走 ffmpeg（控制台程序），不藏会弹窗
    stelarithHideChildConsole(p);
    p.start(ff, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                  QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                  QStringLiteral("dshow"), QStringLiteral("-i"), QStringLiteral("video=%1").arg(dev),
                  QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"),
                  QStringLiteral("2"), path });
    if (!ProcessUtil::waitFinished(p, 8000)) {
        p.kill();
        ProcessUtil::waitFinished(p, 1500);
        QFile::remove(path);
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_snapshot: ffmpeg 超时（设备 %1）").arg(dev);
        return out;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        QFile::remove(path);
        const QString raw = QString::fromUtf8(p.readAllStandardError()).trimmed();
        // 同上（2026-10-12）：FFmpeg 原生输出不上界面，只给分类后的人话 + 错误码。
        const MediaTools::CamErr ce = MediaTools::classifyCamError(raw);
        out.result = QStringLiteral("failed");
        out.error  = QStringLiteral("抓拍失败：%1（设备 %2）").arg(ce.msg, dev);
        out.data.insert(QStringLiteral("errorCode"), ce.code);
        out.data.insert(QStringLiteral("errorRaw"), raw.left(300));
        out.data.insert(QStringLiteral("retryable"), ce.retryable);
        return out;
    }
    if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_snapshot: ffmpeg 退出 0 但没写出图片（设备 %1 可能不可用）").arg(dev);
        return out;
    }
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(path));
    out.data.insert(QStringLiteral("bytes"), (int)QFileInfo(path).size());
    out.data.insert(QStringLiteral("device"), dev);
    return out;
}

ExecOut recordStart(const QJsonObject &params)
{
    ExecOut out;
    // 设备：显式 device > 本机绑定的摄像头 > video0（2026-10-09）
    QString dev = params.value(QStringLiteral("device")).toString().trimmed();
    if (dev.isEmpty()) dev = MediaTools::boundCamera();
    if (dev.isEmpty()) dev = QStringLiteral("video0");
    const int maxSec = qBound(1, params.value(QStringLiteral("duration_sec")).toInt(300), 3600);
    const QString startErr = MediaSession::beginRecording(dev, maxSec);
    if (!startErr.isEmpty()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_record: ") + startErr;
        return out;
    }
    // 光"进程起来了"不算数：ffmpeg 遇到不存在的 dshow 设备会**自己立刻退出**，
    // 而 waitForStarted() 这种情况照样返回 true → 回执 done、一个字节都没录到（假成功）。
    // 所以真等两秒，看输出文件有没有真的长出来；没有就 kill + 删文件 + 如实报失败。
    QThread::msleep(2000);
    const bool reallyRecording = QFileInfo::exists(MediaSession::recOutPath())
                                 && QFileInfo(MediaSession::recOutPath()).size() > 0;
    if (!reallyRecording) {
        MediaSession::recProc()->kill();
        ProcessUtil::waitFinished(*MediaSession::recProc(), 3000);
        QFile::remove(MediaSession::recOutPath());
        MediaSession::setRecProc(nullptr);      // 等价于 delete 旧对象 + 置空
        MediaSession::setRecOutPath(QString());
        out.result = QStringLiteral("failed");
        out.error  = QStringLiteral("开录失败：2 秒内没拿到任何画面（设备 %1 没找到或被占用）—— 现在并没有在录").arg(dev);
        out.data.insert(QStringLiteral("errorCode"), QStringLiteral("device_not_found"));
        out.data.insert(QStringLiteral("retryable"), true);
        return out;
    }
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("recording"), true);   // 明示：这条回执是"确认真的在录"，不是"进程开了"
    out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(MediaSession::recOutPath()));
    out.data.insert(QStringLiteral("device"), dev);
    out.data.insert(QStringLiteral("maxSec"), maxSec);
    return out;
}

ExecOut recordStop()
{
    ExecOut out;
    if (!MediaSession::recProc()) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_record_stop: 当前没有录像在进行");
        return out;
    }
    const QString recPath = MediaSession::recOutPath();
    qint64 bytes = 0; QString err2;
    // ⚠️ 2026-10-13：不再要求"进程必须还在跑"才停。ffmpeg 带 -t 到点会**自己结束**，
    //    那时 state 已是 NotRunning，但文件是完整的 —— 旧写法会把这种"已录好的文件"
    //    判成"没有录像在进行"而丢掉（自习模式自动录制到点常走这条）。endRecording 容忍它。
    if (!MediaSession::endRecording(&bytes, &err2)) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("camera_record_stop: ") + err2;
        return out;
    }
    out.result = QStringLiteral("done");
    out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(recPath));
    out.data.insert(QStringLiteral("bytes"), bytes);
    // 班级监控（设计文档 3.6 第一版）：录制完成后自动上传到云端 recordings/（endRecording 内已发起），
    // 供管理端列表 + 回放。上传失败不改变回执（本地文件仍在，可手工取）。
    return out;
}

} // namespace CameraActions
