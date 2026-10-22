// 实现：2026-10-22 逐字搬自 main.cpp（AGENTS.md §10 第 1 刀）。
// 函数体未改逻辑，只把 static 换成 namespace 内可见；cameraDevPath 不导出，保留内部链接。

#include "MediaTools.h"

#include <QtGlobal>            // qEnvironmentVariable
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QJsonObject>

namespace MediaTools {

// ffmpeg 定位：STL_AGENT_FFMPEG 显式指定 > STE_QT_FFMPEG > PATH 里找 > 报"没装"。
// 找不到时**如实报错**，绝不假装做了（fail-silent 红线）。
QString ffmpegBin()
{
    const QString env = qEnvironmentVariable("STELARITH_AGENT_FFMPEG")
                         .isEmpty() ? qEnvironmentVariable("STE_QT_FFMPEG")
                                    : qEnvironmentVariable("STELARITH_AGENT_FFMPEG");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return found.isEmpty() ? env : found;   // 空串让调用方判失败
}

// ── 摄像头绑定（2026-10-09）──────────────────────────────────────────────
// 教室机上常有多枚摄像头（含 OBS 这类虚拟摄像头），抓拍/录制到底用哪一枚必须**本机说了算**
// （只有本机能枚举）。所以绑定存在被控端：管理端「监控中心」选一枚 → camera_bind 落到这里；
// 之后 camera_snapshot / camera_record_start 不带 device 时默认用它。
// 存成 `%LOCALAPPDATA%/xingjikong/cameradev.txt`（与 classcode.txt 同款，不动 agent.env）。
QString cameraDevPath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/cameradev.txt");
}
QString boundCamera()   // 空 = 未绑定（调用方回落 video0 / 设备名）
{
    QFile f(cameraDevPath());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QString d = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    return d;
}
bool saveBoundCamera(const QString &dev)
{
    const QString d = dev.trimmed();
    if (d.isEmpty()) { QFile::remove(cameraDevPath()); return true; }   // 传空 = 解绑
    QDir().mkpath(QFileInfo(cameraDevPath()).absolutePath());
    QFile f(cameraDevPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(d.toUtf8());
    f.close();
    return true;
}
/** 解析 `ffmpeg -list_devices true -f dshow -i dummy` 的输出。
 *  实测格式（ffmpeg 9.0，2026-10-09）：
 *      [in#0 @ 0x…] "OBS Virtual Camera" (none)
 *      [in#0 @ 0x…]   Alternative name "@device_sw_{…}"
 *      [in#0 @ 0x…] "麦克风 (High Definition Audio Device)" (audio)
 *  ⚠️ 设备名在**首对引号**里，类别在尾部括号里。别再抓行里第一个 '@' ——
 *     那是 `[in#0 @ 0x…]` 的地址标记，抓它解析必然为空（旧实现的 bug）。
 *  类别：video=摄像头；audio=麦克风；**none=虚拟摄像头（如 OBS），也算摄像头**。 */
void parseDshowDevices(const QString &txt, QJsonArray &cams, QJsonArray &mics,
                       QJsonArray *details)
{
    static const QRegularExpression re(QStringLiteral("\\]\\s*\"([^\"]+)\"\\s*\\(([A-Za-z]+)\\)"));
    for (const QString &line : txt.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) continue;
        const QString name = m.captured(1).trimmed();
        const QString kind = m.captured(2).toLower();
        if (name.isEmpty()) continue;
        if (kind == QLatin1String("audio")) { mics.append(name); continue; }
        cams.append(name);
        // 2026-10-12 · UI 评审第 4 条：kind 不是 `video` 的一律是**虚拟摄像头**
        // （OBS Virtual Camera / Iriun Webcam 等）。它们打不出真实画面，却会被枚举出来、
        // 被当作"真设备"逐个去开，于是刷一大片 `Error opening input file video=…`。
        // 单列出来，界面据此标注「虚拟」，用户就不会选了半天发现没画面。
        if (details) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), name);
            o.insert(QStringLiteral("kind"), kind);
            o.insert(QStringLiteral("virtual"), kind != QLatin1String("video"));
            details->append(o);
        }
    }
}

/** 把 ffmpeg 的原始 stderr 归成**稳定错误码 + 教师看得懂的人话**（2026-10-12 · UI 评审第 4 条）。
 *  ⚠️ 红线条文：`Unable to find a matching device` 这类 FFmpeg 原生输出**绝不能直接上界面**——
 *     老师看不懂，只会判定"这软件坏了"。技术细节一律塞进 `errorRaw`，只进诊断页/日志。
 *     错误码是**机器契约**：管理端靠它决定显示什么文案、要不要给「重试」按钮，
 *     所以这里的字符串不许随手写改动（改了要同步改 CameraCenter.qml 的映射表）。 */
CamErr classifyCamError(const QString &raw)
{
    const QString r = raw.toLower();
    // 顺序有讲究：先判"被占用"再判"找不到"—— 部分驱动被占用时报的其实是 I/O error，
    // 而 I/O error 也出现在设备不存在时，所以先看有没有明确的 busy 字样。
    if (r.contains(QStringLiteral("device is busy"))
        || r.contains(QStringLiteral("already in use"))
        || r.contains(QStringLiteral("being used by another")))
        return { QStringLiteral("device_busy"),
                 QStringLiteral("摄像头被别的程序占用，请先关掉会议软件 / OBS / 系统相机"), true };
    if (r.contains(QStringLiteral("unable to find a matching device"))
        || r.contains(QStringLiteral("could not find"))
        || r.contains(QStringLiteral("no such device"))
        || r.contains(QStringLiteral("device not found"))
        || r.contains(QStringLiteral("error opening input file")))
        return { QStringLiteral("device_not_found"),
                 QStringLiteral("没找到这个摄像头，可能已拔掉、换过接口或被禁用"), false };
    if (r.contains(QStringLiteral("access is denied")) || r.contains(QStringLiteral("permission denied")))
        return { QStringLiteral("permission"),
                 QStringLiteral("没有权限打开摄像头，请检查系统相机权限 / 杀毒软件拦截"), true };
    if (r.contains(QStringLiteral("i/o error")))
        return { QStringLiteral("device_busy"),
                 QStringLiteral("摄像头读写失败，多半正被别的程序占用"), true };
    if (r.contains(QStringLiteral("invalid")) && r.contains(QStringLiteral("device")))
        return { QStringLiteral("device_invalid"),
                 QStringLiteral("这个摄像头名字无效了，请重新枚举并绑定"), false };
    if (r.contains(QStringLiteral("timeout")) || r.contains(QStringLiteral("timed out")))
        return { QStringLiteral("timeout"),
                 QStringLiteral("摄像头响应超时，可能被占用或驱动异常"), true };
    return { QStringLiteral("unknown"),
             QStringLiteral("摄像头打不开（技术细节见诊断日志）"), true };
}

// ffplay 定位：与 ffmpeg 同目录优先（WinGet 装的俩在一处），再退回 PATH。
QString ffplayBin()
{
    const QString env = qEnvironmentVariable("STELARITH_AGENT_FFPLAY");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString ff = ffmpegBin();
    if (!ff.isEmpty()) {
        const QString cand = QFileInfo(ff).absolutePath() + QStringLiteral("/ffplay");
        if (QFileInfo::exists(cand)) return cand;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffplay"));
}

} // namespace MediaTools
