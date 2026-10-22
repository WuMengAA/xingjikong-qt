// 星集控被控端 · 媒体会话控制（录像启停 / 播放停止）
// 2026-10-22 · AGENTS.md §10 第二刀：从 main.cpp 搬出的**行为层**。
//
// ⚠️ 第二、三刀是**过渡形态**（有意如此）：那两刀只搬行为，状态还留在 main.cpp，
//    用 extern 引用 —— 因为状态同时被 executeAction 里的相机分支直接访问。
//    第四刀把媒体会话分支也抽走后调用面收敛，**6 个状态已收归本模块**（文件内
//    static + 访问器），extern 段只剩 g_shotDir / g_mediaDir 两条存储域的。

#pragma once

#include <QtGlobal>   // qint64
#include <QString>
#include <QJsonObject>

#include "../core/ExecOut.h"   // 指令执行结果（第四刀：本模块开始直接产出回执）

// §10.4：头文件里只用到指针 ⇒ 前向声明，绝不 include 完整定义（避免牵连重编译）。
class QProcess;

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

// ── 录像状态访问器（第四刀：状态已收归本模块，相机指令改走这里）───────────
/** 正在录制的 ffmpeg 进程（nullptr = 没在录）。 */
QProcess *recProc();
/** 设置录制进程；**会先 delete 旧对象**，故传 nullptr 即"终止并放弃"。 */
void setRecProc(QProcess *p);
/** 正在录制的输出 mp4 路径。 */
QString recOutPath();
void setRecOutPath(const QString &p);


// ── 播放时段判定（2026-10-23 逐字搬自 main.cpp，原为匿名域内的 static）──
// 只被播放相关代码调用：media_session_start 放行判定 / 超窗口自动截断 /
// 状态查询回执 / 灵动岛显示 ⇒ 归媒体会话模块是本分。
bool isPlayAllowedNow();      // 没开限制或窗口为空 ⇒ 恒放行；否则落在任一窗口内才 true
qint64 remainingMsInWindow(); // 当前窗口剩余毫秒；无限制/无窗口返回 -1

// ── 播放会话指令（2026-10-23 逐字搬自 executeAction 的分支体）──
// startMediaSession 对应 media_session_start：ffplay 起播 + 岛底会话 + 计时器 + 超窗口截断。
// stopMediaSessionAction 对应 media_session_stop：回执在上层，停止动作复用 stopMediaSession()。
ExecOut startMediaSession(const QJsonObject &params);
ExecOut stopMediaSessionAction();
} // namespace MediaSession
