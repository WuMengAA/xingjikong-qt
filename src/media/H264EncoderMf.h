#pragma once
// 星集控被控端 · H264 编码器（Media Foundation 硬件编码，2026-10-31 A 档第①步）
//
// 定位：远程桌面 H264 全链路的「编码」一段（A 档）。被控端抓屏 → 本编码器 →
// H264 NALU 流 → v1 帧信封 → 主 WS 出海 → 云端中继 → 管理端 MfxH264Decoder 解出。
//
// 为什么走 Media Foundation 而非 FFmpeg：
//   · MF 是 Win8+ 系统自带的 IMFTransform 视频编码器（Microsoft H264 Video Encoder MFT），
//     不引入任何第三方库/二进制，被控端构建零新增依赖；
//   · 教室机硬件异构（NVIDIA/Intel/AMD/无独显），MF 编码器由系统按 GPU 选硬编或软编，
//     覆盖度优于写死某厂商编码器；
//   · 与 viewer 侧已落地的 MfxH264Decoder（mfx_h264_decode.*，2026-10-16）完全对称——
//     同一套 annex-B NALU，对端直接 ProcessInput 即可解出。
//
// 头文件铁律（AGENTS 第 0 条 / §10.4）：零 Windows / 零 MF 依赖，所有 MF 句柄藏进 .cpp 的 Impl。
//   本头只暴露纯 C++ 契约，调用方（H264Streamer）不含任何 windows.h / mf*.h。
//
// 输入：BGRX 紧凑缓冲（stride = w*4，字节序 = libvncserver / ScreenCapture 同款），
//   与 screen_capture_dxgi 的输出像素格式一致 ⇒ 抓屏帧**零转换**直接喂。
// 输出：Annex-B NALU 流（00 00 00 01 起始码），首帧前会自动前置 SPS/PPS（IDR 头）。
//
// 失败语义：init() 失败 = 本机不支持 MF H264 编码（如 Win7 无 MF）→ 调用方回落 VNC；
//   encode() 单帧失败 = 该帧丢弃，不抛异常、不影响后续帧（远控是旁路，绝不以编解码反噬主流程）。

#include <cstdint>
#include <vector>

class H264EncoderMf {
public:
    H264EncoderMf();
    ~H264EncoderMf();

    H264EncoderMf(const H264EncoderMf &) = delete;
    H264EncoderMf &operator=(const H264EncoderMf &) = delete;

    /// 初始化 MF H264 编码器。
    /// @param w/h 编码分辨率（应与抓屏输出一致，如 1280x720）。
    /// @param fps 目标帧率（仅用于码率/时长推算，默认 30）。
    /// @param bitrateKbps 目标码率（默认 2000 = 2Mbps，劣化隧道下 720p 实测够用）。
    /// @return true = 编码器就绪；false = 本机不可用（调用方回落 VNC，不要反复重试）。
    bool init(int w, int h, int fps = 30, int bitrateKbps = 2000);

    /// 编码一帧 BGRX。
    /// @param bgrx 输入像素（stride = w*4，顶部在下，与 ScreenCapture 同款）。
    /// @param out  输出 annex-B NALU（含本帧产生的 SPS/PPS/IDR 或 P 帧；多 NALU 已拼成一串）。
    /// @return true = out 有效；false = 本帧未产出（失败/无输出，调用方跳过）。
    bool encode(const uint8_t *bgrx, int w, int h, std::vector<uint8_t> &out);

    /// 是否已就绪（init 成功后恒真）。
    bool ready() const;

    /// 释放 MF 资源（析构也会调；重复调用安全）。
    void shutdown();

private:
    struct Impl;
    Impl *m_p = nullptr;
};
