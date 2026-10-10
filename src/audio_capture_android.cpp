// 语音对讲 · 管理端音频采集 —— Android stub（2026-10-11）
//
// 桌面版用 winmm waveIn（audio_capture.cpp）；Android 无 waveIn，
// 手机 App 并入管理端后由 CMake 在 ANDROID 分支编本文件代替。
// 语义：保持 AudioCapture 接口完整（viewerbackend 的 startSpeaking 正常编译），
// 但手机上「语音对讲」开不了麦 —— startCapture 返回 false + lastError 说明。
// 桌面功能零影响（桌面仍走 audio_capture.cpp）。

#include "audio_capture.h"

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture() = default;

bool AudioCapture::startCapture()
{
    m_active = false;
    m_lastError = QStringLiteral("手机端不支持语音对讲（无 winmm）");
    return false;
}

void AudioCapture::stopCapture()
{
    m_active = false;
}

void AudioCapture::handleBuffer()
{
    // Android stub：无 waveIn 缓冲回调
}