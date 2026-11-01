// 星集控被控端 · H264 编码器实现（OpenH264 软件编码，2026-11-01）
//
// 关键设计（都是本轮实测踩出来的，别想当然改回去）：
//  1. **必须运行时动态加载 dll**（LoadLibrary + GetProcAddress），不能链接 .lib。
//     实测：用 dumpbin 从 dll 生成的 .lib 里导入名是 "openh264-2.7.0-win64"，
//     程序启动时按该名去找 dll 会 0xC0000135（找不到），除非 exe 旁边恰好有同名文件。
//     动态加载还顺带得到「缺 dll 可优雅降级」的能力——软编不是必需品。
//  2. OpenH264 的头文件（codec_api.h 等）在第三方目录，**不进头文件**，
//     本 .cpp 内局部 include，避免污染工程头文件风格。
//  3. 输出拼接：每层用 iNalCount + pNalLengthInByte[] 逐 NALU 切分再拼，
//     因为 pBsBuf 里是「NALU 连续体」，逐个切分最稳（起始码 00 00 00 01 已在其中）。
//  4. OpenH264 的 SPS/PPS 随首个 IDR 一起给出（不用自己插参数集），
//     与 viewer 侧 MfxH264Decoder 的「首包含 SPS/PPS/IDR」契约一致。
//  5. 失败纪律：任何一步失败都记日志并返回 false，绝不抛异常（远控是旁路）。

#include "H264EncoderOh264.h"
#include "PixelConvert.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <QDebug>

// 第三方头文件（来自 third_party/openh264，Cisco BSD）
#include "codec_api.h"

#include <cstring>

namespace {

// dll 候选名：先找打包进安装目录的固定名，再找带版本号的原始名。
const char *const kDllCandidates[] = {
    "openh264.dll",
    "openh264-2.7.0-win64.dll",
};

HMODULE loadOh264Dll()
{
    for (const char *name : kDllCandidates) {
        HMODULE m = LoadLibraryA(name);
        if (m) return m;
    }
    return nullptr;
}

} // namespace

struct H264EncoderOh264::Impl {
    HMODULE dll = nullptr;
    ISVCEncoder *enc = nullptr;
    void (*destroy)(ISVCEncoder *) = nullptr;
    bool ready = false;
    int w = 0, h = 0;
    std::vector<uint8_t> i420;      // 复用缓冲
};

H264EncoderOh264::H264EncoderOh264() : m_p(new Impl) {}
H264EncoderOh264::~H264EncoderOh264() { shutdown(); delete m_p; }

bool H264EncoderOh264::available()
{
    HMODULE m = loadOh264Dll();
    if (!m) return false;
    const bool ok = GetProcAddress(m, "WelsCreateSVCEncoder") &&
                    GetProcAddress(m, "WelsDestroySVCEncoder");
    FreeLibrary(m);
    return ok;
}

bool H264EncoderOh264::init(int w, int h, int fps, int bitrateKbps)
{
    if (m_p->ready) shutdown();
    m_p->w = w; m_p->h = h;
    if (w <= 0 || h <= 0 || (w % 2) || (h % 2)) {
        qWarning("[h264oh264] 尺寸非法 %dx%d（须为正偶数）", w, h);
        return false;
    }

    m_p->dll = loadOh264Dll();
    if (!m_p->dll) {
        qWarning("[h264oh264] 找不到 openh264.dll（软件编码不可用）");
        return false;
    }
    typedef int (*PFN_Create)(ISVCEncoder **);
    typedef void (*PFN_Destroy)(ISVCEncoder *);
    PFN_Create create = (PFN_Create)GetProcAddress(m_p->dll, "WelsCreateSVCEncoder");
    m_p->destroy = (PFN_Destroy)GetProcAddress(m_p->dll, "WelsDestroySVCEncoder");
    if (!create || !m_p->destroy) {
        qWarning("[h264oh264] dll 缺少 WelsCreateSVCEncoder 符号");
        FreeLibrary(m_p->dll); m_p->dll = nullptr;
        return false;
    }
    if (create(&m_p->enc) != 0 || !m_p->enc) {
        qWarning("[h264oh264] WelsCreateSVCEncoder 失败");
        FreeLibrary(m_p->dll); m_p->dll = nullptr;
        return false;
    }

    SEncParamExt param;
    memset(&param, 0, sizeof(param));
    param.iUsageType = SCREEN_CONTENT_REAL_TIME;   // 屏幕内容（远控桌面）
    param.iPicWidth = w;
    param.iPicHeight = h;
    param.iTargetBitrate = bitrateKbps * 1000;
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = fps > 0 ? (float)fps : 30.0f;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.sSpatialLayers[0].iVideoWidth = w;
    param.sSpatialLayers[0].iVideoHeight = h;
    param.sSpatialLayers[0].fFrameRate = fps > 0 ? (float)fps : 30.0f;
    param.sSpatialLayers[0].iSpatialBitrate = bitrateKbps * 1000;
    param.sSpatialLayers[0].uiProfileIdc = PRO_MAIN;
    param.bEnableFrameSkip = false;   // 远控不能跳帧（画面会顿）

    const int rv = m_p->enc->InitializeExt(&param);
    if (rv != 0) {
        qWarning("[h264oh264] InitializeExt 失败 rv=%d", rv);
        m_p->destroy(m_p->enc); m_p->enc = nullptr;
        FreeLibrary(m_p->dll); m_p->dll = nullptr;
        return false;
    }

    m_p->i420.assign(size_t(w) * h * 3 / 2, 0);
    m_p->ready = true;
    qInfo("[h264oh264] OpenH264 软件编码器就绪（%dx%d @%dfps %dkbps）", w, h, fps, bitrateKbps);
    return true;
}

bool H264EncoderOh264::encode(const uint8_t *bgrx, int w, int h, std::vector<uint8_t> &out)
{
    out.clear();
    if (!m_p || !m_p->ready || !m_p->enc || !bgrx) return false;
    if (w != m_p->w || h != m_p->h) return false;

    ste::bgrxToI420(bgrx, w, h, m_p->i420.data());

    SSourcePicture pic;
    memset(&pic, 0, sizeof(pic));
    pic.iPicWidth = w;
    pic.iPicHeight = h;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = w;
    pic.iStride[1] = w / 2;
    pic.iStride[2] = w / 2;
    pic.pData[0] = m_p->i420.data();
    pic.pData[1] = m_p->i420.data() + size_t(w) * h;
    pic.pData[2] = m_p->i420.data() + size_t(w) * h * 5 / 4;

    SFrameBSInfo info;
    memset(&info, 0, sizeof(info));
    const int rv = m_p->enc->EncodeFrame(&pic, &info);
    if (rv != 0 || info.iLayerNum <= 0) return false;   // 本帧无产出（正常路径，不刷屏）

    // 逐 NALU 切分拼接（pBsBuf 里是 NALU 连续体，起始码已在其中）。
    // ⚠️ SLayerBSInfo 里**没有**层总长字段，只有 iNalCount + pNalLengthInByte[]，
    //    故边界校验用「已累加长度」与整帧字节数比对。
    out.clear();
    const int frameBytes = info.iFrameSizeInBytes;
    int copied = 0;
    for (int l = 0; l < info.iLayerNum; ++l) {
        SLayerBSInfo &li = info.sLayerInfo[l];
        if (!li.pBsBuf || li.iNalCount <= 0 || !li.pNalLengthInByte) continue;
        int offset = 0;
        for (int n = 0; n < li.iNalCount; ++n) {
            const int len = li.pNalLengthInByte[n];
            if (len <= 0) continue;
            if (frameBytes > 0 && copied + len > frameBytes) break;   // 越界保护
            out.insert(out.end(), li.pBsBuf + offset, li.pBsBuf + offset + len);
            offset += len;
            copied += len;
        }
    }
    return !out.empty();
}

bool H264EncoderOh264::ready() const { return m_p && m_p->ready; }

void H264EncoderOh264::shutdown()
{
    if (!m_p) return;
    if (m_p->enc) {
        m_p->enc->Uninitialize();
        if (m_p->destroy) m_p->destroy(m_p->enc);
        m_p->enc = nullptr;
    }
    if (m_p->dll) {
        FreeLibrary(m_p->dll);
        m_p->dll = nullptr;
    }
    m_p->destroy = nullptr;
    m_p->ready = false;
    m_p->i420.clear();
}

#else // 非 Windows：软件编码路径不存在

struct H264EncoderOh264::Impl {};
H264EncoderOh264::H264EncoderOh264() : m_p(new Impl) {}
H264EncoderOh264::~H264EncoderOh264() { delete m_p; }
bool H264EncoderOh264::available() { return false; }
bool H264EncoderOh264::init(int, int, int, int) { return false; }
bool H264EncoderOh264::encode(const uint8_t *, int, int, std::vector<uint8_t> &out) { out.clear(); return false; }
bool H264EncoderOh264::ready() const { return false; }
void H264EncoderOh264::shutdown() {}

#endif
