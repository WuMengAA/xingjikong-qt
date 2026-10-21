// L0 编解码对拍：合成 440Hz 正弦 -> Opus 编码 -> 解码 -> 时延对齐后算 SNR + 实际码率。
// 注意：Opus 编码器有 lookahead，解码流相对原始有延迟；逐点直接比会得到假低 SNR，
//      必须先做互相关找最佳 lag 再对齐比较。
#include "opus_codec.h"

#include <cmath>
#include <cstdio>
#include <vector>

static const double PI = 3.14159265358979323846;

static double snrAt(const std::vector<int16_t> &a, const std::vector<int16_t> &b,
                    int lag, int startSample, double *corrOut = nullptr)
{
    double sc = 0, ss = 0, rr = 0, noise = 0;
    for (size_t i = static_cast<size_t>(startSample);
         i < a.size() && i + lag < b.size(); ++i) {
        double s = a[i], r = b[i + lag];
        sc += s * r; ss += s * s; rr += r * r; noise += (s - r) * (s - r);
    }
    if (corrOut) *corrOut = sc / std::sqrt(ss * rr + 1e-12);
    return 10.0 * std::log10(ss / (noise > 1e-9 ? noise : 1e-9));
}

int main()
{
    const int Fs = 16000, ch = 1, frame = 320; // 20 ms @ 16k
    const int frames = 75;                     // 1.5 秒（留足尾部给 lag 搜索）

    std::vector<int16_t> src;
    src.reserve(static_cast<size_t>(frames) * frame);
    for (int i = 0; i < frames * frame; ++i) {
        double t = static_cast<double>(i) / Fs;
        src.push_back(static_cast<int16_t>(0.30 * 32767.0 * std::sin(2.0 * PI * 440.0 * t)));
    }

    for (int br : {16000, 24000, 32000}) {
        OpusCodec enc, dec;
        if (!enc.initEncoder(Fs, ch, br) || !dec.initDecoder(Fs, ch)) {
            std::printf("init FAILED @%d\n", br);
            return 1;
        }
        size_t totalBytes = 0;
        int minPkt = 1 << 30, maxPkt = 0;
        std::vector<int16_t> rt;
        rt.reserve(src.size());
        for (int f = 0; f < frames; ++f) {
            auto pkt = enc.encode(src.data() + static_cast<size_t>(f) * frame, frame);
            if (pkt.empty()) { std::printf("encode FAILED @%d\n", br); return 1; }
            totalBytes += pkt.size();
            if ((int)pkt.size() < minPkt) minPkt = (int)pkt.size();
            if ((int)pkt.size() > maxPkt) maxPkt = (int)pkt.size();
            auto pcm = dec.decode(pkt.data(), (int)pkt.size(), frame);
            rt.insert(rt.end(), pcm.begin(), pcm.end());
        }

        // 互相关搜最佳 lag（0..400），跳过前 40ms 的启动瞬态
        const int start = 640;
        int bestLag = 0; double bestCorr = -2.0;
        for (int lag = 0; lag <= 400; ++lag) {
            double c = 0; snrAt(src, rt, lag, start, &c);
            if (c > bestCorr) { bestCorr = c; bestLag = lag; }
        }
        double snr = snrAt(src, rt, bestLag, start);
        std::printf("bitrate=%5d  bytes/s=%5zu (%.1f kbps)  pkt=%d..%d B  lag=%d  corr=%.3f  SNR=%.1f dB\n",
                    br, totalBytes, totalBytes * 8.0 / (frames * 0.02 * 1000.0),
                    minPkt, maxPkt, bestLag, bestCorr, snr);
    }
    return 0;
}
