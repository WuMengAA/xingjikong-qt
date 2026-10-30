#pragma once
// 摄像头抓帧实现（2026-10-30 · P0-2 分层第 3 层 · IFrameSnapshot 的真实现之一）
//
// 走 CameraActions::snapshot（ffmpeg DirectShow 抽 1 帧 PNG）；本类负责：
//   · 把 PNG 转成 JPEG（上限 1280 长边，与桌面同口径，压住体积防止撞 WS 帧上限 4MB）；
//   · 落到 noise_evidence 目录（与桌面同一输出目录，Reporter 统一读取）；
//   · 抓不到如实报原因（OBS 虚拟设备 / 被占 / 无摄像头都算失败分支，必须可测）。
//
// ⚠️ 容器 / 无 ffmpeg / 无摄像头环境：CameraActions::snapshot 会失败 ⇒ capture() 返回 false
//   并给原因，本类的契约和桌面实现一致（失败不抛、只在 why 里写人话）。
#include "noise_capture_sources.h"

class CameraSnapshot : public IFrameSnapshot
{
public:
    CameraSnapshot() = default;
    bool capture(QString *outPath, QString *why) override;

    /// 与 DesktopSnapshot::outputDir() 同目录（Reporter 统一在这里找两张图）。
    static QString outputDir();
};
