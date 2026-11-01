// 星集控被控端 · 像素格式转换实现（共享单一真源，2026-11-01）
//
// 内容逐字搬自原 H264EncoderMf.cpp 内的静态函数（2026-10-31 版本），
// 只把 qBound 换成手写夹取，避免头文件带 Qt 依赖。

#include "PixelConvert.h"

namespace ste {

namespace {
inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }
} // namespace

void bgrxToI420(const uint8_t *bgrx, int w, int h, uint8_t *i420)
{
    const uint8_t *rgb = bgrx;                 // 顶部在下，逐行即屏幕顺序
    uint8_t *Y = i420;
    uint8_t *U = i420 + w * h;
    uint8_t *V = U + (w / 2) * (h / 2);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const uint8_t *p = rgb + (j * w + i) * 4;
            const int r = p[2], g = p[1], b = p[0];
            Y[j * w + i] = clamp255((int)(0.299 * r + 0.587 * g + 0.114 * b));
        }
    }
    for (int j = 0; j < h / 2; ++j) {
        for (int i = 0; i < w / 2; ++i) {
            // 2x2 区块取平均色（简单下采样，足够远控）
            int r = 0, g = 0, b = 0;
            for (int dj = 0; dj < 2; ++dj)
                for (int di = 0; di < 2; ++di) {
                    const uint8_t *p = rgb + ((j * 2 + dj) * w + (i * 2 + di)) * 4;
                    r += p[2]; g += p[1]; b += p[0];
                }
            r /= 4; g /= 4; b /= 4;
            const int u = (int)(-0.169 * r - 0.331 * g + 0.500 * b) + 128;
            const int v = (int)( 0.500 * r - 0.419 * g - 0.081 * b) + 128;
            U[j * (w / 2) + i] = clamp255(u);
            V[j * (w / 2) + i] = clamp255(v);
        }
    }
}

void bgrxToNv12(const uint8_t *bgrx, int w, int h, uint8_t *nv12)
{
    const uint8_t *rgb = bgrx;
    uint8_t *Y  = nv12;
    uint8_t *UV = nv12 + w * h;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const uint8_t *p = rgb + (j * w + i) * 4;
            Y[j * w + i] = clamp255((int)(0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0]));
        }
    }
    for (int j = 0; j < h / 2; ++j) {
        for (int i = 0; i < w / 2; ++i) {
            int r = 0, g = 0, b = 0;
            for (int dj = 0; dj < 2; ++dj)
                for (int di = 0; di < 2; ++di) {
                    const uint8_t *p = rgb + ((j * 2 + dj) * w + (i * 2 + di)) * 4;
                    r += p[2]; g += p[1]; b += p[0];
                }
            r /= 4; g /= 4; b /= 4;
            const int u = (int)(-0.169 * r - 0.331 * g + 0.500 * b) + 128;
            const int v = (int)( 0.500 * r - 0.419 * g - 0.081 * b) + 128;
            const int idx = (j * (w / 2) + i) * 2;
            UV[idx + 0] = clamp255(u);
            UV[idx + 1] = clamp255(v);
        }
    }
}

} // namespace ste
