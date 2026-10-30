#pragma once
// 抓帧接口（2026-10-30 · P0-2 分层第 3 层 · 可注入抽象）
//
// 为什么桌面/摄像头做成同一接口：
//   存证要做的事对两者完全一样 ——「抓一帧到文件，告诉我路径或失败原因」。
//   拆成两个不同的接口只会让 Capture 写两套几乎相同的调用代码，且假实现要写两遍。
//
// 为什么必须是可注入接口（而不是在 Capture 里直接 new ScreenCapture/ffmpeg）：
//   ① 容器/无桌面/无摄像头的环境要能验「3 次 → 1 条 incident → 2 张图 → 1 次上报」，
//      这条链路的价值在编排，不在抓帧本身；
//   ② 抓帧会失败（麦克风被占、摄像头被 OBS 虚拟设备抢、Session 0 无桌面）——
//      失败分支必须有测试，否则"抓不到图也照样上报"这种假成功会一直在生产里。
//
// 实现约定：
//   · capture() **允许阻塞**（真实现会等 DXGI 首帧 100ms / ffmpeg 最多 8s），
//     调用方（NoiseEvidenceCapture）负责把它放到工作线程，不在主线程调；
//   · 重入不需要安全（Capture 保证同一时刻只跑一份），实现里可以放可变成员。

class QString;

class IFrameSnapshot
{
public:
    virtual ~IFrameSnapshot() = default;

    /**
     * 抓一帧落盘。
     * @param outPath 成功时写入文件绝对路径（非空）；失败时不动或置空
     * @param why     失败时写入人话原因（可为 nullptr = 调用方不关心）
     * @return true = 文件已写好
     */
    virtual bool capture(QString *outPath, QString *why) = 0;
};
