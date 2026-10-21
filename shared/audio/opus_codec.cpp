#include "opus_codec.h"

#include <opus.h>   // vendored: third_party/opus/include/opus.h

OpusCodec::~OpusCodec()
{
    if (m_enc) { opus_encoder_destroy(static_cast<OpusEncoder *>(m_enc)); m_enc = nullptr; }
    if (m_dec) { opus_decoder_destroy(static_cast<OpusDecoder *>(m_dec)); m_dec = nullptr; }
}

bool OpusCodec::initEncoder(int sampleRate, int channels, int bitrateBps)
{
    if (m_enc) { opus_encoder_destroy(static_cast<OpusEncoder *>(m_enc)); m_enc = nullptr; }
    int err = OPUS_OK;
    OpusEncoder *enc = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc) return false;
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrateBps));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    // 抗丢包：inband FEC + 10% 期望丢包（与 4.5 降级状态机的弱网场景配套）
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(10));
    m_enc = enc;
    m_sampleRate = sampleRate;
    m_channels = channels;
    return true;
}

bool OpusCodec::initDecoder(int sampleRate, int channels)
{
    if (m_dec) { opus_decoder_destroy(static_cast<OpusDecoder *>(m_dec)); m_dec = nullptr; }
    int err = OPUS_OK;
    OpusDecoder *dec = opus_decoder_create(sampleRate, channels, &err);
    if (err != OPUS_OK || !dec) return false;
    m_dec = dec;
    m_sampleRate = sampleRate;
    m_channels = channels;
    return true;
}

std::vector<uint8_t> OpusCodec::encode(const int16_t *pcm, int samples)
{
    if (!m_enc || !pcm || samples <= 0) return {};
    // 20ms 语音包最坏远小于 1500B；4000 足够任何合法帧
    std::vector<uint8_t> out(4000);
    const int n = opus_encode(static_cast<OpusEncoder *>(m_enc), pcm, samples,
                              out.data(), static_cast<opus_int32>(out.size()));
    if (n < 0) return {};
    out.resize(static_cast<size_t>(n));
    return out;
}

std::vector<int16_t> OpusCodec::decode(const uint8_t *data, int len, int maxSamplesPerChannel)
{
    if (!m_dec || !data || len <= 0 || maxSamplesPerChannel <= 0) return {};
    std::vector<int16_t> pcm(static_cast<size_t>(maxSamplesPerChannel) * m_channels, 0);
    const int n = opus_decode(static_cast<OpusDecoder *>(m_dec), data, len,
                              pcm.data(), maxSamplesPerChannel, /*decode_fec=*/0);
    if (n < 0) return {};
    pcm.resize(static_cast<size_t>(n) * m_channels);
    return pcm;
}
