#pragma once
#include <QString>

// 自习模式（2026-10-13）：噪音异常自动录制闭环。
// 2026-10-30 从 src/main.cpp 迁出（原 sendNoiseAlert / studyOnSample / studyTriggerRecord，约 4261–4370）。
// 本模块只含「超阈判定 + 触发摄像头自动录制」逻辑；状态全局（g_study*）仍驻 main，
// 按既有 extern 模式（g_uid / agentEnvPath 同款，提升外部链接）复用，保证纯搬迁、零行为变化。
namespace StudyMode {

// 噪音采样超阈判定：被 main 的 ensureNoise lambda 每拍调用；连续 N 拍超阈则触发自动录制。
void onSample(double db);

// 自习停止时停掉自动录制计时器（study_mode_stop 指令在 main 调用）。
void stopRecordingTimer();

}
