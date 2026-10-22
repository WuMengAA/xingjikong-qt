// 星集控被控端 · 媒体工具层（ffmpeg 定位 / 摄像头绑定 / 设备解析 / 错误归类）
//
// 2026-10-22 · AGENTS.md §10 第 1 刀：从 main.cpp（8142 行）搬出的**零内部依赖纯函数层**。
//
// ── 为什么先搬这一层 ────────────────────────────────────────────────────
// 相机四件套（camera_list / snapshot / record / media_session）全部依赖它，
// 而它自己**不依赖 main.cpp 的任何东西** —— 是 8142 行里唯一能"纯搬家零重构"的一层。
// 下一刀把相机分支从 executeAction 巨型函数里抽出来时，地基已经在了。
//
// ── 搬家纪律 ────────────────────────────────────────────────────────────
// 函数体逐字搬自 main.cpp，未改任何逻辑。注释里的实测结论（ffmpeg 9.0 输出格式、
// 虚拟摄像头判定、错误码是机器契约）一并搬来 —— 那是踩坑换来的，不能丢。

#pragma once

#include <QString>
#include <QJsonArray>

namespace MediaTools {

/** ffmpeg 定位：STL_AGENT_FFMPEG > STE_QT_FFMPEG > PATH > 报"没装"。
 *  找不到时返回空串，由调用方如实报错（fail-silent 红线）。 */
QString ffmpegBin();

/** ffplay 定位：与 ffmpeg 同目录优先（WinGet 装的俩在一处），再退回 PATH。 */
QString ffplayBin();

/** 绑定文件落盘路径（%LOCALAPPDATA%/xingjikong/cameradev.txt）。
 *  导出它的唯一理由：camera_bind 写失败时要把"写不进去的是哪个文件"如实报错，
 *  路径不许由调用方另猜一份（fail-silent 红线）。 */
QString cameraDevPath();

/** 本机绑定的摄像头设备名（空 = 未绑定，调用方回落 video0 / 设备名）。
 *  教室机常有多枚摄像头（含 OBS 虚拟摄像头），用哪枚必须本机说了算。 */
QString boundCamera();

/** 写入绑定；传空 = 解绑。 */
bool saveBoundCamera(const QString &dev);

/** 解析 `ffmpeg -list_devices true -f dshow -i dummy` 的输出。
 *  details 非空时额外给出每枚设备的 kind / virtual 标记。 */
void parseDshowDevices(const QString &txt, QJsonArray &cams, QJsonArray &mics,
                       QJsonArray *details = nullptr);

/** 摄像头错误：稳定错误码 + 教师看得懂的人话。
 *  ⚠️ code/msg 是**机器契约**（管理端 CameraCenter.qml 有映射表），不许随手改。 */
struct CamErr {
    QString code;
    QString msg;
    bool retryable = false;
};
CamErr classifyCamError(const QString &raw);

} // namespace MediaTools
