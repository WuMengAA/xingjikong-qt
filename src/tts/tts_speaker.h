#pragma once
// TTS 统一播报（2026-10-30 从 src/main.cpp 抽出 —— main 拆分第一刀）。
//
// 只暴露一个外部入口 speakText()；SAPI 兜底朗读、音量抬升/恢复序列、token 去重护栏、
// 超时兜底等全部封在 tts_speaker.cpp 内部（文件内匿名 namespace 的静态状态）。
// 本模块不读 main.cpp 任何全局 —— 所有状态自持，便于用假 EdgeTts 单独单测。
#include <QString>

// 通知 / 循环提醒朗读统一入口：优先 Edge 神经语音（联网音色自然），失败自动回退
// Windows SAPI 离线朗读。开关由 AgentSettings::voiceEnabled() 控制；未连 WS 也照常朗读
// （朗读是本地增强，不依赖云端）。
void speakText(const QString &text);
