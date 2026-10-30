#pragma once
// 存证编排（应用服务）—— 2026-10-30 · P0-2 分层第 3 层
//
// ── 职责 ────────────────────────────────────────────────────────────────
// 收到一次「该上报了」的事件 → 抓一张桌面 + 一张摄像头 → 返回 NoiseEvidence。
// 只管**编排**：先抓谁、失败怎么记、什么时候算"一张都没有"。
// 不负责：判定（域服务的活）、序列化与发送（Reporter 的活）、抓帧本身（IFrameSnapshot 的活）。
//
// ── 为什么抓帧源必须注入 ────────────────────────────────────────────────
// ① 容器/无摄像头/无桌面（Session 0）环境要能验这条编排链；
// ② 抓帧一定会失败（麦克风被语音对讲占、摄像头被虚拟设备抢、远程会话无桌面）——
//    失败分支不测，就会长期潜伏"抓不到图也照样上报存证成功"的假成功。
// 所以本类只依赖 IFrameSnapshot 抽象，真实现（DesktopSnapshot 等）由 main 装配时传进来。
//
// ── 线程 ────────────────────────────────────────────────────────────────
// capture() 是**同步阻塞**的（真实现抓一帧约 100–8000ms）。调用方必须放到工作线程，
// 不允许在 UI/主线程直接调 —— 否则一次抓帧就能把灵动岛动画卡住。

#include "noise_capture_sources.h"
#include "noise_sample.h"

class NoiseEvidenceCapture
{
public:
    /**
     * @param desktop 桌面抓帧源（必传；内部不拥有，调用方保证生命周期长于本对象）
     * @param camera  摄像头抓帧源（可为 nullptr = 本机无摄像头/未配置，会在证据里记明原因）
     */
    NoiseEvidenceCapture(IFrameSnapshot* desktop, IFrameSnapshot* camera);
    ~NoiseEvidenceCapture() = default;

    /** 抓两帧。同步阻塞。任何一路失败都不抛异常，只把原因写进 NoiseEvidence。 */
    NoiseEvidence capture();

    void setCamera(IFrameSnapshot* camera) { m_camera = camera; }
    const IFrameSnapshot* desktopSource() const { return m_desktop; }
    const IFrameSnapshot* cameraSource() const { return m_camera; }

private:
    IFrameSnapshot* m_desktop = nullptr;   // 不拥有
    IFrameSnapshot* m_camera  = nullptr;   // 不拥有；可为空
};
