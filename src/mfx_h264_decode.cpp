// viewer H264 解码 · MF 兜底通道实现（2026-10-16）
// 流程：MFStartup → MFTEnumEx 找 H264 解码器 → 输入 H264_ES / 输出 NV12 →
// 每访问单元喂 ProcessInput → ProcessOutput 取 NV12 → 转 RGB32 → QImage。
// 沙箱实测本机 MF 解码输出 0 帧（环境不可靠），真机联调验证 —— 详见 mfx_h264_decode.h。
#include "mfx_h264_decode.h"

#include <QDebug>
#include <cstring>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

// NV12 → RGB32（BT.601，无缩放；w/h 偶数）
static void nv12ToRgb32(const unsigned char *nv12, int w, int h, unsigned char *rgb)
{
    const unsigned char *Y = nv12;
    const unsigned char *UV = nv12 + w * h;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int y = Y[j * w + i];
            const int vu = (j / 2) * (w / 2) + (i / 2);
            const int v = UV[vu * 2 + 0] - 128;
            const int u = UV[vu * 2 + 1] - 128;
            int r = y + (int)(1.402 * v);
            int g = y - (int)(0.344 * u) - (int)(0.714 * v);
            int b = y + (int)(1.772 * u);
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            unsigned char *px = rgb + (j * w + i) * 4;
            px[0] = (unsigned char)b; px[1] = (unsigned char)g; px[2] = (unsigned char)r; px[3] = 255;
        }
    }
}

bool MfxH264Decoder::init(int w, int h)
{
#ifdef _WIN32
    shutdown();
    m_w = w; m_h = h;
    if (FAILED(MFStartup(MF_VERSION))) { qWarning("[mfx] FAIL MFStartup"); return false; }
    m_mfstart = (void *)1;

    IMFActivate **acts = nullptr; UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                           MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                           nullptr, nullptr, &acts, &count);
    if (FAILED(hr) || count == 0) { qWarning("[mfx] FAIL 无视频解码器"); return false; }
    IMFTransform *mft = nullptr;
    for (UINT32 k = 0; k < count; ++k) {
        IMFTransform *cand = nullptr;
        if (FAILED(acts[k]->ActivateObject(IID_PPV_ARGS(&cand)))) continue;
        bool h264 = false;
        for (DWORD q = 0; q < 8; ++q) {
            IMFMediaType *av = nullptr;
            if (FAILED(cand->GetInputAvailableType(0, q, &av))) break;
            GUID sub = {0};
            if (av->GetGUID(MF_MT_SUBTYPE, &sub) == S_OK &&
                (sub == MFVideoFormat_H264 || sub == MFVideoFormat_H264_ES)) h264 = true;
            av->Release();
            if (h264) break;
        }
        if (h264) { mft = cand; break; }
        cand->Release();
    }
    if (acts) CoTaskMemFree(acts);
    if (!mft) { qWarning("[mfx] FAIL 无 H264 解码器"); return false; }

    IMFMediaType *inM = nullptr, *outM = nullptr;
    MFCreateMediaType(&inM);
    inM->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inM->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264_ES);
    inM->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = mft->SetInputType(0, inM, 0);
    if (FAILED(hr)) {
        inM->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        hr = mft->SetInputType(0, inM, 0);
    }
    if (FAILED(hr)) { qWarning("[mfx] FAIL SetInputType %08x", hr); return false; }
    if (FAILED(mft->GetOutputCurrentType(0, &outM))) {
        for (DWORD q = 0; q < 8; ++q) {
            IMFMediaType *av = nullptr;
            if (FAILED(mft->GetOutputAvailableType(0, q, &av))) break;
            GUID sub = {0};
            if (av->GetGUID(MF_MT_SUBTYPE, &sub) == S_OK) { outM = av; av->AddRef(); }
            av->Release();
            if (outM) break;
        }
    }
    if (!outM || FAILED(mft->SetOutputType(0, outM, 0))) { qWarning("[mfx] FAIL SetOutputType"); return false; }

    mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    m_mft = mft; m_inType = inM; m_outType = outM;
    m_outBuf.assign(size_t(m_w * m_h * 3 / 2), 0);   // NV12
    m_ready = true;
    qInfo("[mfx] MF H264 解码器就绪（%dx%d）", m_w, m_h);
    return true;
#else
    Q_UNUSED(w); Q_UNUSED(h);
    return false;
#endif
}

bool MfxH264Decoder::decodeFrame(const QByteArray &nal, QImage &out)
{
#ifdef _WIN32
    if (!m_ready || !m_mft || nal.isEmpty()) return false;
    auto *mft = static_cast<IMFTransform *>(m_mft);

    // 输入样本
    IMFSample *s = nullptr; IMFMediaBuffer *b = nullptr; BYTE *pb = nullptr;
    MFCreateSample(&s); MFCreateMemoryBuffer((DWORD)nal.size(), &b);
    if (FAILED(b->Lock(&pb, nullptr, nullptr))) { if (b) b->Release(); if (s) s->Release(); return false; }
    memcpy(pb, nal.constData(), size_t(nal.size()));
    b->Unlock();
    s->AddBuffer(b);
    HRESULT hr = mft->ProcessInput(0, s, 0);
    b->Release(); s->Release();
    if (FAILED(hr)) return false;

    // 取输出（可能多帧/不释出）
    bool got = false;
    for (int g = 0; g < 8; ++g) {
        MFT_OUTPUT_DATA_BUFFER odb = {}; IMFSample *os = nullptr;
        MFCreateSample(&os); odb.pSample = os; DWORD st = 0;
        hr = mft->ProcessOutput(0, 1, &odb, &st);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(hr)) { os->Release(); break; }
        IMFMediaBuffer *ob = nullptr; BYTE *data = nullptr; DWORD len = 0;
        if (os->ConvertToContiguousBuffer(&ob) == S_OK && ob->Lock(&data, nullptr, &len) == S_OK) {
            // NV12 → RGB32
            std::vector<unsigned char> rgb(size_t(m_w * m_h * 4));
            nv12ToRgb32(data, m_w, m_h, rgb.data());
            QImage img(rgb.data(), m_w, m_h, m_w * 4, QImage::Format_RGB32);
            out = img.copy();
            got = true;
            ob->Unlock();
        }
        if (ob) ob->Release();
        os->Release();
        if (odb.pEvents) odb.pEvents->Release();
        if (got) break;
    }
    return got;
#else
    Q_UNUSED(nal); Q_UNUSED(out);
    return false;
#endif
}

void MfxH264Decoder::shutdown()
{
#ifdef _WIN32
    if (m_ready && m_mft) {
        if (auto *mft = static_cast<IMFTransform *>(m_mft)) {
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            mft->Release();
        }
    }
    if (m_inType) static_cast<IMFMediaType *>(m_inType)->Release();
    if (m_outType) static_cast<IMFMediaType *>(m_outType)->Release();
    if (m_mfstart) MFShutdown();
    m_mft = nullptr; m_inType = nullptr; m_outType = nullptr; m_mfstart = nullptr; m_ready = false;
#endif
}