#pragma once
// 桌面抓帧实现（2026-10-30 · P0-2 分层第 3 层 · IFrameSnapshot 的真实现）
//
// 与 VNC / 缩略图的关系：三者都走 screen_capture（DXGI→GDI 门面），但**节奏完全不同**，
// 所以不复用它们的实例：
//   · VncServer 是有人远控时的 10fps 720p 热路径；
//   · ThumbnailReporter 是看板开着时的 15s/台 320px 小图；
//   · 本件是「噪音触发那一下」的一次性存证，一节课最多 3 张 —— 允许它慢、允许它重建。
//
// ⚠️ 每次抓帧都**新建一个 ScreenCapture**（不长期持有），原因不是懒：
//    DXGI 的 grab 在首帧之后是**非阻塞**的（timeout=0，没有新帧就返回 nullptr）。
//    静态桌面上复用一个老实例，很可能一次也抓不到 —— 那是最难查的一类"偶发无图"。
//    新建实例的第一次 grab 走 firstFrame 分支（100ms 等合成器出图），拿得到就是全屏帧。
//    代价 ~200ms（D3D11 设备创建），且发生在工作线程，一节课 3 次完全不值一提。

#include "noise_capture_sources.h"

class DesktopSnapshot : public IFrameSnapshot
{
public:
    DesktopSnapshot() = default;

    /// 抓当前桌面一帧 → <AppData>/Stelarith/noise_evidence/desk-<ms>.jpg
    bool capture(QString *outPath, QString *why) override;

    /// 存证图目录（Reporter 计算相对路径、运维手工找图都用它；不存在则建）。
    static QString outputDir();

    /// 长边上限（存证要"看得清发生了什么"，不是原始像素级取证）。
    static int maxEdge();
};
