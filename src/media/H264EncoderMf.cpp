// 星集控被控端 · H264 编码器实现（Media Foundation，2026-10-31 A 档第①步）
//
// 设计要点（与 viewer 侧 MfxH264Decoder 对称，同一套 annex-B NALU）：
//   输入 BGRX → 转 NV12（编码器硬件通常吃 NV12）→ IMFTransform 异步编码 →
//   取出 IMFSample 里的编出字节 → 已是 annex-B（MF H264 编码器默认吐 00 00 00 01 起始码）。
//
// MF H264 编码器是「异步 MFT」：可以喂多帧再取多帧，且 IDR 首帧前必须发
// MFT_MESSAGE_NOTIFY_BEGIN_STREAMING / START_OF_STREAM，结尾发 END_OF_STREAM 冲刷。
// 我们每帧喂一帧、试取一帧（取不到就下一轮再取），SPS/PPS 由第一帧 IDR 自然带出——
// 对端 MfxH264Decoder 正是按「首包含 SPS/PPS/IDR」设计，无需我们手动插参数集。
//
// 失败纪律：任何一步 FAIL 都记日志并返回 false，绝不抛异常（远控是旁路）。
// Win7 上 MFStartup 或 MFTEnumEx 找不到 H264 编码器 → init 返回 false，调用方回落 VNC。

#include "H264EncoderMf.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>   // eAVEncH264VProfile_* 等级枚举
#include <wincodec.h>   // 仅用于 GUID 常量（不实际加载 WIC）

#include <QDebug>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

// ── BGRX → I420（BT.601，无缩放；w/h 偶数；顶部在下 → I420 也是顶部在下）──
// BGRX 是 4 字节/像素（字节0=B）。I420 = 整面 Y(1B/px) + 半分辨率 U 平面 + 半分辨率 V 平面
// （三个独立平面，非交织）。
// ⚠️ 关键坑（2026-10-31 实测）：Microsoft H264 Video Encoder MFT 的**输入类型协商只认
//    MFVideoFormat_I420**，喂 NV12 会 SetInputType 失败（本机实测 FAIL SetInputType(NV12)）。
//    这与 viewer 侧解码器输出 NV12 不对称——编码器吃 I420，解码器吐 NV12，二者都是 4:2:0、
//    同字节量，只是 UV 排布不同。
static void bgrxToI420(const uint8_t *bgrx, int w, int h, uint8_t *i420)
{
    const uint8_t *rgb = bgrx;                 // 顶部在下，逐行即屏幕顺序
    uint8_t *Y = i420;
    uint8_t *U = i420 + w * h;
    uint8_t *V = U + (w / 2) * (h / 2);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const uint8_t *p = rgb + (j * w + i) * 4;
            const int r = p[2], g = p[1], b = p[0];
            Y[j * w + i] = static_cast<uint8_t>(
                qBound(0, (int)(0.299 * r + 0.587 * g + 0.114 * b), 255));
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
            const int u = static_cast<int>(-0.169 * r - 0.331 * g + 0.500 * b) + 128;
            const int v = static_cast<int>( 0.500 * r - 0.419 * g - 0.081 * b) + 128;
            U[j * (w / 2) + i] = static_cast<uint8_t>(qBound(0, u, 255));
            V[j * (w / 2) + i] = static_cast<uint8_t>(qBound(0, v, 255));
        }
    }
}

// ── BGRX → NV12（Y 平面 + 交织 UV，半分辨率 2B/2px）── 供部分硬件 MFT 的输入。
static void bgrxToNv12(const uint8_t *bgrx, int w, int h, uint8_t *nv12)
{
    const uint8_t *rgb = bgrx;
    uint8_t *Y  = nv12;
    uint8_t *UV = nv12 + w * h;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const uint8_t *p = rgb + (j * w + i) * 4;
            Y[j * w + i] = static_cast<uint8_t>(
                qBound(0, (int)(0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0]), 255));
        }
    for (int j = 0; j < h / 2; ++j)
        for (int i = 0; i < w / 2; ++i) {
            int r = 0, g = 0, b = 0;
            for (int dj = 0; dj < 2; ++dj)
                for (int di = 0; di < 2; ++di) {
                    const uint8_t *p = rgb + ((j * 2 + dj) * w + (i * 2 + di)) * 4;
                    r += p[2]; g += p[1]; b += p[0];
                }
            r /= 4; g /= 4; b /= 4;
            const int u = static_cast<int>(-0.169 * r - 0.331 * g + 0.500 * b) + 128;
            const int v = static_cast<int>( 0.500 * r - 0.419 * g - 0.081 * b) + 128;
            const int idx = (j * (w / 2) + i) * 2;
            UV[idx + 0] = static_cast<uint8_t>(qBound(0, u, 255));
            UV[idx + 1] = static_cast<uint8_t>(qBound(0, v, 255));
        }
}

struct H264EncoderMf::Impl {
    IMFTransform *mft = nullptr;
    IMFMediaType *inType = nullptr;
    IMFMediaType *outType = nullptr;
    bool mfStarted = false;
    int w = 0, h = 0;
    bool ready = false;
    GUID inSubtype = GUID_NULL;  // 实际协商到的输入子类型（编码时按它选转换）
    std::vector<uint8_t> i420;   // 复用缓冲（Y+U+V 三平面）

    bool doInit(int w_, int h_, int fps, int bitrateKbps)
    {
        w = w_; h = h_;
        const HRESULT hr0 = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        if (FAILED(hr0)) { qWarning("[h264enc] FAIL MFStartup %08x", hr0); return false; }
        mfStarted = true;

        // 找 H264 编码器 MFT
        IMFActivate **acts = nullptr; UINT32 count = 0;
        MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               nullptr, &outInfo, &acts, &count);
        if (FAILED(hr) || count == 0) {
            qWarning("[h264enc] FAIL 无 H264 编码器 MFT（count=%u hr=%08x）", count, hr);
            return false;
        }
        IMFTransform *enc = nullptr;
        for (UINT32 k = 0; k < count; ++k) {
            if (SUCCEEDED(acts[k]->ActivateObject(IID_PPV_ARGS(&enc)))) break;
        }
        for (UINT32 k = 0; k < count; ++k) acts[k]->Release();
        CoTaskMemFree(acts);
        if (!enc) { qWarning("[h264enc] FAIL ActivateObject 编码器"); return false; }
        mft = enc;

        // ── 输出类型：从 GetOutputAvailableType 取到的真实 H264 子类型设入 ──
        // ⚠️ 诊断(2026-10-31)实测：本机微软 H264 Encoder MFT 的 GetOutputAvailableType 返回
        //    唯一子类型就是 H264（GUID 34363248-...）；但**直接构造 MFVideoFormat_H264 写死
        //    SetOutputType 反而失败**——必须用枚举拿到的那个 GUID 实例去设。故这里取枚举值。
        bool outOk = false;
        for (DWORD q = 0; q < 32; ++q) {
            IMFMediaType *av = nullptr;
            if (FAILED(mft->GetOutputAvailableType(0, q, &av))) break;
            GUID sub = {0};
            if (SUCCEEDED(av->GetGUID(MF_MT_SUBTYPE, &sub)) && sub != GUID_NULL) {
                MFCreateMediaType(&outType);
                outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                outType->SetGUID(MF_MT_SUBTYPE, sub);
                outType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrateKbps * 1000));
                outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                // ⚠️ 必须用 MFSetAttributeSize / MFSetAttributeRatio 设这些"打包属性"，
                // 直接 SetUINT32 手写 UINT64 会被部分 MFT 视为缺失 → SetOutputType 返 C00D36BD。
                MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
                MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps, 1);
                if (SUCCEEDED(mft->SetOutputType(0, outType, 0))) { outOk = true; break; }
                outType->Release(); outType = nullptr;
            }
            av->Release();
        }
        if (!outOk) { qWarning("[h264enc] FAIL 无可用 H264 输出类型"); return false; }

        // ── 输入类型：已知候选子类型逐个试 SetInputType ──
        // ⚠️ 诊断实测(2026-10-31)：本机微软 H264 Encoder MFT 的 GetInputAvailableType 返回 0 个
        // （惰性），必须直接试候选子类型。且 **I420 subtype 协商失败(C00D36B4)、IYUV 成功**；
        // I420 与 IYUV 字节布局完全相同（YUV420P 三平面），故以 IYUV 优先、I420/NV12 兜底。
        static const GUID *kIns[] = {
            &MFVideoFormat_IYUV, &MFVideoFormat_I420, &MFVideoFormat_NV12
        };
        bool inOk = false;
        for (const GUID *g : kIns) {
            MFCreateMediaType(&inType);
            inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            inType->SetGUID(MF_MT_SUBTYPE, *g);
            inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, w, h);
            MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps, 1);
            if (SUCCEEDED(mft->SetInputType(0, inType, 0))) {
                inOk = true;
                inSubtype = *g;   // 记下实际输入格式，编码时按它转
                break;
            }
            inType->Release(); inType = nullptr;
        }
        if (!inOk) { qWarning("[h264enc] FAIL 无可用输入类型"); return false; }

        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

        i420.assign(size_t(w) * h * 3 / 2, 0);
        ready = true;
        qInfo("[h264enc] MF H264 编码器就绪（%dx%d @%dfps %dkbps）", w, h, fps, bitrateKbps);
        return true;
    }

    bool doEncode(const uint8_t *bgrx, std::vector<uint8_t> &out)
    {
        out.clear();
        if (!ready || !mft || !bgrx) return false;
        // 按实际协商到的输入子类型转：I420/IYUV（字节布局相同，均走 bgrxToI420）/ NV12（交织）
        if (inSubtype == MFVideoFormat_NV12) {
            bgrxToNv12(bgrx, w, h, i420.data());   // i420 缓冲复用为 NV12 布局（同字节量）
        } else {
            // I420 / IYUV 都是 YUV420P 三平面，布局一致 → 统一走 bgrxToI420
            bgrxToI420(bgrx, w, h, i420.data());
        }

        IMFSample *s = nullptr; IMFMediaBuffer *buf = nullptr; BYTE *pb = nullptr;
        DWORD i420len = static_cast<DWORD>(i420.size());
        if (FAILED(MFCreateMemoryBuffer(i420len, &buf)) ||
            FAILED(buf->Lock(&pb, nullptr, nullptr))) {
            if (buf) buf->Release();
            return false;
        }
        memcpy(pb, i420.data(), i420len);
        buf->Unlock();
        buf->SetCurrentLength(i420len);
        if (FAILED(MFCreateSample(&s)) || FAILED(s->AddBuffer(buf))) {
            if (buf) buf->Release();
            if (s) s->Release();
            return false;
        }
        // 时间截：递增（编码器用做 DTS/PTS，单位 100ns）
        static LONGLONG t = 0;
        s->SetSampleTime(t);
        s->SetSampleDuration(333333);   // ≈30fps
        t += 333333;

        HRESULT hr = mft->ProcessInput(0, s, 0);
        buf->Release(); s->Release();
        // NOTACCEPTING(INVALIDARG 同)= 编码器暂不接受（队列满/沙箱管线不全）→ 静默跳过本帧。
        // 不刷 warning、不抛异常：远控是旁路，绝不以编解码反噬主流程，调用方下一帧再试。
        if (FAILED(hr)) { return false; }

        // 试取输出（异步 MFT 可能这帧不产，下一帧才出）
        bool got = false;
        for (int g = 0; g < 8; ++g) {
            MFT_OUTPUT_DATA_BUFFER odb = {}; IMFSample *os = nullptr;
            if (FAILED(MFCreateSample(&os))) break;
            odb.pSample = os;
            DWORD status = 0;
            hr = mft->ProcessOutput(0, 1, &odb, &status);
            // NEED_MORE_INPUT = 还需喂更多输入（帧已接受，本次无产出，正常）；
            // 其它失败（含沙箱 INVALIDARG 死锁）一律静默视为"本帧无产出"，不刷 warning、
            // 不抛异常——远控是旁路，绝不以编解码反噬主流程，调用方下一帧再试。
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(hr)) { os->Release(); break; }
            IMFMediaBuffer *ob = nullptr; BYTE *data = nullptr; DWORD len = 0;
            if (os->ConvertToContiguousBuffer(&ob) == S_OK &&
                ob->Lock(&data, nullptr, &len) == S_OK && len > 0) {
                out.assign(data, data + len);   // 已是 annex-B（含起始码）
                got = true;
                ob->Unlock();
            }
            if (ob) ob->Release();
            os->Release();
            if (odb.pEvents) odb.pEvents->Release();
            if (got) break;
        }
        return got;
    }

    void doShutdown()
    {
        if (ready && mft) {
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            mft->Release();
        }
        if (inType)  inType->Release();
        if (outType) outType->Release();
        if (mfStarted) MFShutdown();
        mft = nullptr; inType = nullptr; outType = nullptr;
        mfStarted = false; ready = false;
    }
};

H264EncoderMf::H264EncoderMf() : m_p(new Impl) {}
H264EncoderMf::~H264EncoderMf() { shutdown(); delete m_p; }

bool H264EncoderMf::init(int w, int h, int fps, int bitrateKbps)
{
    if (m_p->ready) shutdown();
    return m_p->doInit(w, h, fps, bitrateKbps);
}

bool H264EncoderMf::encode(const uint8_t *bgrx, int w, int h, std::vector<uint8_t> &out)
{
    if (!m_p->ready) return false;
    if (w != m_p->w || h != m_p->h) { qWarning("[h264enc] 尺寸漂移 %dx%d != %dx%d", w, h, m_p->w, m_p->h); return false; }
    return m_p->doEncode(bgrx, out);
}

bool H264EncoderMf::ready() const { return m_p && m_p->ready; }

void H264EncoderMf::shutdown() { if (m_p) m_p->doShutdown(); }

#else // 非 Windows：本模块不参与编译（被控端仅 Windows），统一返回 false。

H264EncoderMf::H264EncoderMf() : m_p(nullptr) {}
H264EncoderMf::~H264EncoderMf() {}
bool H264EncoderMf::init(int, int, int, int) { return false; }
bool H264EncoderMf::encode(const uint8_t *, int, int, std::vector<uint8_t> &) { return false; }
bool H264EncoderMf::ready() const { return false; }
void H264EncoderMf::shutdown() {}

#endif
