// 星集控被控端 · 相机指令（DirectShow / ffmpeg）
// 2026-10-23 · AGENTS.md §10 第 3 刀：从 main.cpp 的 executeAction 抽出的**分支体**。
//
// ── 相机四件套（走 ffmpeg DirectShow 输入）──
// 用 ffmpeg 而不是 Qt Multimedia：本机 Qt 6.8.1 装的是最小集，没有 Qt6Multimedia。
// 找不到 ffmpeg / 没有相机都如实报错，绝不回 done。
//
// ⚠️ 每一个函数对应且只对应 executeAction 里的一个动作分支，**函数体是逐字搬的**
//    （连踩坑注释一起搬），行为与原地完全一致。main.cpp 只留分支判断 + 一行调用。

#pragma once

#include <QString>
#include <QJsonObject>

#include "../core/ExecOut.h"

namespace CameraActions {

ExecOut listDevices();
ExecOut bindDevice(const QJsonObject &params);
ExecOut selfTest(const QJsonObject &params);
ExecOut snapshot(const QJsonObject &params);
ExecOut recordStart(const QJsonObject &params);
ExecOut recordStop();

} // namespace CameraActions
