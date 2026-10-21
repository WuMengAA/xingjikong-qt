#pragma once
#include <cstdint>
#include <vector>

// 星集控 · Opus 编解码薄封装（Qt-free，agent/viewer 共用同一份算法）。
// 语音对讲参数：16 kHz / mono / 20 ms 帧（=320 样本）/ VOIP 模式。
// 设计：核心不依赖 Qt，便于单测（L0）与两端复用；Qt 侧自行做 QByteArray <-> vector 转换。
class OpusCodec {
public:
    OpusCodec() = default;
    ~OpusCodec();
    OpusCodec(const OpusCodec &) = delete;
    OpusCodec &operator=(const OpusCodec &) = delete;

    // bitrateBps: 16000 / 24000 / 32000（P1 三档）
    bool initEncoder(int sampleRate = 16000, int channels = 1, int bitrateBps = 16000);
    bool initDecoder(int sampleRate = 16000, int channels = 1);

    // pcm: interleaved int16；samples = 每声道采样数（20ms@16k = 320）。返回一个 Opus 包（空=失败）。
    std::vector<uint8_t> encode(const int16_t *pcm, int samples);

    // 解一个 Opus 包 -> interleaved int16 PCM。maxSamplesPerChannel 建议 = 320（20ms）。
    std::vector<int16_t> decode(const uint8_t *data, int len, int maxSamplesPerChannel);

    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    bool encoderReady() const { return m_enc != nullptr; }
    bool decoderReady() const { return m_dec != nullptr; }

private:
    void *m_enc = nullptr;   // OpusEncoder*
    void *m_dec = nullptr;   // OpusDecoder*
    int m_sampleRate = 16000;
    int m_channels = 1;
};
