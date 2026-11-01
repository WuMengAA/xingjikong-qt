#pragma once
// 星集控被控端 · 像素格式转换（共享单一真源，2026-11-01）
//
// 为什么有这一层：H264 编码有两条实现（MF 硬件 / OpenH264 软编），
// 两者输入都是抓屏给的 BGRX，输出都要转成 4:2:0 三平面 I420。
// 转换逻辑必须**只有一份**——两份拷贝迟早在某个分辨率/色度采样细节上漂移，
// 表现为「硬编能出图、软编花屏」这种极难查的问题。
//
// 头文件铁律（AGENTS §10.4）：零 Windows / 零 Qt 依赖，纯 C++ 契约。

#include <cstdint>

namespace ste {

/// BGRX（4 字节/像素，字节序 B,G,R,X，stride = w*4，顶部在下）→ I420（YUV420P 三平面）。
/// 色度按 2x2 区块平均降采样（BT.601 系数），远控场景足够。
/// @param bgrx 输入像素；@param w/h 尺寸（须为偶数）；@param i420 输出缓冲，容量须 >= w*h*3/2。
void bgrxToI420(const uint8_t *bgrx, int w, int h, uint8_t *i420);

/// BGRX → NV12（Y 平面 + 半分辨率交织 UV，容量 w*h*3/2）。
/// 供只吃 NV12 的硬件 MFT 用。
void bgrxToNv12(const uint8_t *bgrx, int w, int h, uint8_t *nv12);

} // namespace ste
