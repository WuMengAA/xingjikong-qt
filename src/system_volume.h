#pragma once
// 系统主音量（2026-10-11 · Core Audio / WASAPI）
//
// 为什么单独一个头文件：main.cpp 已经 5500+ 行，直接在那里 include
// <mmdeviceapi.h>/<endpointvolume.h> 实测会把后面 Microphone 的类型解析搞坏
// （编译报 C4430），而且给这个巨型编译单元继续灌 SDK 头只会更容易撞 C1060。
// 这里只暴露一个纯 C++/Qt 签名，SDK 头全部关在 .cpp 里。

#include <QString>

/**
 * 设**系统主音量** 0-100。
 *
 * @param v      目标音量（调用方应先夹紧到 0-100）
 * @param errOut 失败原因（人话，可直接回给管理端显示）；成功时不清空
 * @return true 成功；false 失败（调用方应回落到 waveOutSetVolume）
 */
bool setSystemMasterVolume(int v, QString *errOut);
