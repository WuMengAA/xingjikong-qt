// 星集控被控端 · 媒体会话控制（录像启停 / 播放停止）
// 2026-10-22 · AGENTS.md §10 第二刀：从 main.cpp 搬出的**行为层**。
//
// ⚠️ 过渡形态（有意如此）：这三个函数操作的状态（g_recProc / g_playProc /
//    g_mediaTick …）**还在 main.cpp**，这里用 extern 引用。原因：那些全局同时被
//    executeAction 里的相机分支直接访问，那片是第三刀的地盘。这一刀只搬行为，
//    第三刀把相机分支抽成函数时，状态随之收进本模块，届时删掉 extern 段。

#pragma once

#include <QtGlobal>   // qint64
#include <QString>

namespace MediaSession {

/** 起一段录像（后台 ffmpeg 写 mp4）。返回空串=成功，非空=人话错误原因。
 *  已在录则如实返回"已有一条录像在进行中"，绝不静默换文件。 */
QString beginRecording(const QString &dev, int maxSec);

/** 停止录像。返回 true = 有有效文件（并已发起上传）。
 *  ⚠️ 容忍 ffmpeg 因 -t 到点**自己结束**（state 已 NotRunning 但文件是好的）——
 *     自习模式自动录制常走这条，不能当成"没有录像"丢掉。 */
bool endRecording(qint64 *outBytes, QString *outErr, int *outCode = nullptr);

/** 停媒体会话：终止 ffplay + 清岛底「正在播放」+ 停计时器。
 *  media_session_stop 与超窗口自动截断共用这一份，避免两套停止逻辑。 */
void stopMediaSession();

/** 是否正在主动停止（true = 这次 finished 是我们 terminate 的，不是播完）。
 *  ffplay 的 finished 回调靠它区分"播完"和"老师点了停止"。 */
bool isStopping();

} // namespace MediaSession
