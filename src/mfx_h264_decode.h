#pragma once
// viewer H264 解码 · MF 兜底通道（2026-10-16）
//
// 定位：viewer 屏幕轨解码的**备选**（主方案 = ffmpeg 子进程，沙箱已验证）。
// 老师机没装 ffmpeg 时走本通道：Media Foundation「Microsoft H264 Video Decoder MFT」
// （Windows 自带，解码器广泛存在）。⚠️ 本机沙箱 MF 编解码均实测不可靠（0 帧输出，
// 疑缺 DWM/GPU 会话环境）——本类**真机联调验证**，沙箱验证不了。
//
// 用法：MfxH264Decoder dec; dec.init(320,240); ... dec.decodeFrame(nal 帧, &QImage) ...
#include <QImage>
#include <QByteArray>
#include <vector>

class MfxH264Decoder {
public:
    MfxH264Decoder() = default;
    ~MfxH264Decoder() { shutdown(); }
    MfxH264Decoder(const MfxH264Decoder &) = delete;
    MfxH264Decoder &operator=(const MfxH264Decoder &) = delete;

    // 初始化 H264 解码器（输出 320x240；失败返回 false，调用方仍可走 ffmpeg/无画面）
    bool init(int w, int h);
    void shutdown();

    // 喂一个 H264 访问单元（Annex B：SPS/PPS/IDR 或 P 帧）→ 若能解出帧，转 RGB32 填 out。
    // 返回 true = out 是本帧有效画面；false = 输入不足/被缓冲（正常，解码器有参考帧延迟）。
    bool decodeFrame(const QByteArray &nal, QImage &out);

private:
    void *m_mft = nullptr;      // IMFTransform*
    void *m_inType = nullptr;   // IMFMediaType*（H264 输入）
    void *m_outType = nullptr;  // IMFMediaType*（NV12 输出）
    void *m_mfstart = nullptr;  // MFStartup 标记
    std::vector<unsigned char> m_outBuf;   // NV12 输出缓冲
    int m_w = 320, m_h = 240;
    bool m_ready = false;
};