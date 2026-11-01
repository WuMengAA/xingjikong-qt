#pragma once
// 星集控被控端 · H264 编码器（OpenH264 软件编码，2026-11-01）
//
// 为什么要有第二条编码路径（重要，是本轮实测逼出来的）：
//   2026-10-31 在**真实桌面会话（Session 1）**上实测本机 MF H264 编码器：
//   枚举到 1 个 "H264 Encoder MFT"，输出类型协商成功、输入类型协商成功，
//   ProcessInput 前 18 帧也成功，但 ProcessOutput 恒返 MF_E_TRANSFORM_NEED_MORE_INPUT，
//   drain / EOS 之后仍然 **0 产出**（已排除「喂帧不够」「异步未 drain」「profile 缺失」三因）。
//   ⇒ 本机（AMD APU + 虚拟显示器）的 MF H264 硬件编码器**协商得成却不产帧**。
//   这不是本机特有：教室机若同样是虚拟化/无独显编码环境，会一模一样失败。
//
// 于是加一条**纯软件**编码路径：OpenH264（Cisco，BSD 许可，非 GPL，与项目
// 「FFmpeg 必须 LGPL、libx264/x265 禁用」红线不冲突），2026-11-01 本机实测
// 30/30 帧全部出帧、32 个 NALU、IDR 18KB + P 帧约 450B，**且不依赖 GPU/DWM**。
//
// 与 MF 编码器的关系：**同构接口**（init/encode/ready/shutdown），
// H264Streamer 优先用 MF（零依赖、教室机硬件加速），失败时自动回落本编码器，
// 两者都失败才回落 VNC。
//
// 头文件铁律（AGENTS §10.4）：零 Windows / 零 OpenH264 依赖，所有句柄藏进 .cpp 的 Impl。
//
// 输入：BGRX 紧凑缓冲（stride = w*4，字节序与 ScreenCapture 同款）→ 内部转 I420。
// 输出：Annex-B NALU 流（00 00 00 01 起始码），首个 IDR 前自带 SPS/PPS。

#include <cstdint>
#include <vector>

class H264EncoderOh264 {
public:
    H264EncoderOh264();
    ~H264EncoderOh264();

    H264EncoderOh264(const H264EncoderOh264 &) = delete;
    H264EncoderOh264 &operator=(const H264EncoderOh264 &) = delete;

    /// 初始化软件 H264 编码器。
    /// @param w/h 编码分辨率（应与抓屏输出一致，如 1280x720）。
    /// @param fps 目标帧率（默认 30）。
    /// @param bitrateKbps 目标码率（默认 2000 = 2Mbps）。
    /// @return true = 就绪；false = 本机缺 dll / 初始化失败（调用方回落 VNC）。
    bool init(int w, int h, int fps = 30, int bitrateKbps = 2000);

    /// 编码一帧 BGRX。
    /// @param bgrx 输入像素（stride = w*4，顶部在下）。
    /// @param out  输出 annex-B NALU（本帧的 SPS/PPS/IDR 或 P 帧，多 NALU 已拼成一串）。
    /// @return true = out 有效；false = 本帧未产出（调用方跳过）。
    bool encode(const uint8_t *bgrx, int w, int h, std::vector<uint8_t> &out);

    /// 是否已就绪（init 成功后恒真）。
    bool ready() const;

    /// 释放编码器与 dll 句柄（析构也会调；重复调用安全）。
    void shutdown();

    /// 静态探测：本机是否具备软件编码条件（dll 存在且可加载、符号齐全）。
    /// 供上层在启动前决定要不要走软编（只读，不建编码器实例）。
    static bool available();

private:
    struct Impl;
    Impl *m_p = nullptr;
};
