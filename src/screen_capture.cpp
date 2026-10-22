// 屏幕采集实现：Grabber 基类析构 + GdiGrabber（BitBlt 兜底）+ ScreenCapture 门面。
// 详见 screen_capture.h。DXGI 主实现在 screen_capture_dxgi.cpp。
// 本文件不依赖 Qt（除 _WIN32 宏），纯 Win32 + 标准库。

#include "screen_capture.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <QDebug>
#include <QDateTime>

// ── Grabber 基类：析构定义于本 TU（vnc_server / screen_capture_dxgi 共享）──
Grabber::~Grabber() {}

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "screen_capture_dxgi.h" // 门面要 new DxgiGrabber，需完整类型

// ── 物理目标屏选择（2026-10-19 修复②v2）────────────────────────────────────
// 为什么不能只认 GetSystemMetrics 主屏 / GetDC(nullptr)：GameViewer 虚拟屏
// 激活后它自己就是"主屏"，两套老判据全部指向虚拟屏（白色空内容）。只有枚举
// GDI 显示设备、按 DeviceID 的 "ROOT\" 前缀排除软件虚拟驱动，才能稳定钉住
// 真硬件屏。虚拟驱动特征：ROOT\GameViewerIddDriver / ROOT\BASICDISPLAY
// （基本呈现）/ RDP 显示驱动——全部 Root 枚举；真屏是 PCI\VEN_xxxx 或 ACPI\。
SteTargetDisplay pickTargetDisplay()
{
    SteTargetDisplay best;      // 最优物理屏
    SteTargetDisplay fallback;  // 主屏兜底（没有物理屏时用，可能是虚拟屏）
    bool hasFallback = false;

    auto better = [](const SteTargetDisplay &a, const SteTargetDisplay &b) -> bool {
        if (a.primary != b.primary) return a.primary;
        const bool ao = (a.x == 0 && a.y == 0);
        const bool bo = (b.x == 0 && b.y == 0);
        if (ao != bo) return ao;
        return static_cast<long long>(a.w) * a.h > static_cast<long long>(b.w) * b.h;
    };

    DISPLAY_DEVICEW dd{};
    for (DWORD i = 0;; ++i) {
        dd.cb = sizeof(dd);   // EnumDisplayDevicesW 要求调用方填 cb，逐轮重填保平安
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) continue;
        if (dm.dmPelsWidth <= 0 || dm.dmPelsHeight <= 0) continue;   // 挂着但没激活

        SteTargetDisplay t;
        t.ok = true;
        for (int k = 0; k < 39 && dd.DeviceName[k]; ++k) t.device[k] = dd.DeviceName[k];
        t.x = dm.dmPosition.x;
        t.y = dm.dmPosition.y;
        t.w = static_cast<int>(dm.dmPelsWidth);
        t.h = static_cast<int>(dm.dmPelsHeight);
        t.primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

        if (t.primary) { fallback = t; hasFallback = true; }

        // 软件虚拟驱动：DeviceID 以 "ROOT\" 开头（大小写不敏感）
        const bool isVirtual = dd.DeviceID[0] != L'\0'
            && _wcsnicmp(dd.DeviceID, L"ROOT\\", 5) == 0;
        if (isVirtual) continue;   // 虚拟屏不当目标（只可能进主屏兜底）

        if (!best.ok || better(t, best)) best = t;
    }
    if (best.ok) return best;
    if (hasFallback) return fallback;
    // 枚举整体失败（极端老系统？）：退化成"主屏、无设备名"（调用方用 GetDC）
    SteTargetDisplay t;
    t.ok = true;
    t.w = GetSystemMetrics(SM_CXSCREEN);
    t.h = GetSystemMetrics(SM_CYSCREEN);
    t.primary = true;
    return t;
}

// ── GdiGrabber：BitBlt 抓物理目标屏 → 可选 StretchBlt 缩放 → 逐像素转 BGRX ──
// DXGI 初始化失败时的兜底后端；没有系统脏区信息，takeDirtyRects 恒全屏。
// 2026-10-19 修复②v2：源不再是"主桌面"（GetDC(nullptr) 跟随主屏，虚拟屏抢
// 主屏时抓到白屏），而是 pickTargetDisplay() 选出的物理屏（按设备名建 DC）。
class GdiGrabber : public Grabber {
public:
    ~GdiGrabber() override { freeRes(); }

    bool init(int outW = 0, int outH = 0) override {
        const SteTargetDisplay t = pickTargetDisplay();
        const int sw = t.ok ? t.w : GetSystemMetrics(SM_CXSCREEN);
        const int sh = t.ok ? t.h : GetSystemMetrics(SM_CYSCREEN);
        if (outW <= 0 || outH <= 0) { outW = sw; outH = sh; }  // 0 = 原生
        m_outW = outW;
        m_outH = outH;
        m_init = realloc(sw, sh, outW, outH, t.ok ? t.device : L"");
        return m_init;
    }

    const uint8_t *grab(int &outW, int &outH) override {
        if (!m_init && !init()) return nullptr;

        // 修复②v2：拓扑会动态变（GameViewer 虚拟屏随时激活/退出）。DXGI 后端
        // 靠"钉在具体输出上"免疫横跳；GDI 没有这种绑定，只能每 ~1s 重挑一次
        // 物理目标屏，设备名或尺寸变了就按新目标重建（重建才几毫秒，可接受）。
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastPickMs >= 1000) {
            m_lastPickMs = nowMs;
            const SteTargetDisplay t = pickTargetDisplay();
            const int sw = t.ok ? t.w : GetSystemMetrics(SM_CXSCREEN);
            const int sh = t.ok ? t.h : GetSystemMetrics(SM_CYSCREEN);
            const std::wstring dev = t.ok ? std::wstring(t.device) : std::wstring();
            if (dev != m_devName || sw != m_srcW || sh != m_srcH) {
                if (!realloc(sw, sh, m_outW, m_outH, dev)) return nullptr;
            }
        }

        // ① 抓目标屏到源 DIB（源坐标 (0,0) 即目标屏左上角）
        HGDIOBJ oldSrc = SelectObject(reinterpret_cast<HDC>(m_memDc), reinterpret_cast<HBITMAP>(m_srcBmp));
        if (!oldSrc) return nullptr;
        const BOOL ok = BitBlt(reinterpret_cast<HDC>(m_memDc), 0, 0, m_srcW, m_srcH,
                               reinterpret_cast<HDC>(m_screenDc), 0, 0, SRCCOPY);
        SelectObject(reinterpret_cast<HDC>(m_memDc), oldSrc);
        if (!ok) return nullptr;

        // ② 缩放（仅在源≠输出时；COLORONCOLOR 是 GDI 缩放的常规质量档，
        //    比 HALFTONE 快一个量级，720p 画质足够）
        if (m_bmp != m_srcBmp) {
            HGDIOBJ oldDst = SelectObject(reinterpret_cast<HDC>(m_memDc), reinterpret_cast<HBITMAP>(m_bmp));
            if (!oldDst) return nullptr;
            const BOOL sok = StretchBlt(reinterpret_cast<HDC>(m_memDc), 0, 0, m_w, m_h,
                                        reinterpret_cast<HDC>(m_memDc), 0, 0, m_srcW, m_srcH, SRCCOPY);
            SelectObject(reinterpret_cast<HDC>(m_memDc), oldDst);
            if (!sok) return nullptr;
        }

        // ③ 输出 DIB（顶部在下 BGR）→ 逐像素转 BGRX
        const uint8_t *src = reinterpret_cast<const uint8_t *>(m_bits);
        uint8_t *dst = m_bgrx;
        const size_t stride = static_cast<size_t>(m_w) * 3;
        for (int y = 0; y < m_h; ++y) {
            const uint8_t *srow = src + static_cast<size_t>(y) * stride;
            uint8_t *drow = dst + static_cast<size_t>(y) * m_w * 4;
            for (int x = 0; x < m_w; ++x) {
                const uint8_t *s = srow + x * 3;
                uint8_t *d = drow + x * 4;
                d[0] = s[0]; // B
                d[1] = s[1]; // G
                d[2] = s[2]; // R
                d[3] = 0;    // X
            }
        }

        outW = m_w;
        outH = m_h;
        return m_bgrx;
    }

    int width() const override { return m_w; }
    int height() const override { return m_h; }
    bool ok() const override { return m_init; }

    void takeDirtyRects(std::vector<GrabRect> &out) override {
        // 无脏区信息：整屏即脏区（libvncserver 侧与非增量请求等效）。
        out.push_back(GrabRect{0, 0, m_w, m_h});
    }

private:
    void freeRes() {
        if (m_memDc) {
            DeleteDC(reinterpret_cast<HDC>(m_memDc));
            m_memDc = nullptr;
        }
        if (m_screenDc) {
            if (m_dcOwned) {
                DeleteDC(reinterpret_cast<HDC>(m_screenDc));   // CreateDC 来的
            } else {
                ReleaseDC(nullptr, reinterpret_cast<HDC>(m_screenDc));  // GetDC 来的
            }
            m_screenDc = nullptr;
            m_dcOwned = false;
        }
        if (m_srcBmp) {
            DeleteObject(reinterpret_cast<HBITMAP>(m_srcBmp));
            m_srcBmp = nullptr;
        }
        if (m_bmp && m_bmp != m_srcBmp) {
            DeleteObject(reinterpret_cast<HBITMAP>(m_bmp));
        }
        m_bmp = nullptr;
        m_bits = nullptr; // DIB section 的位随位图释放而失效，不单独 free
        m_srcBits = nullptr;
        free(m_bgrx);
        m_bgrx = nullptr;
        m_bgrxCap = 0;
        m_srcW = m_srcH = 0;
        m_w = m_h = 0;
    }

    bool realloc(int srcW, int srcH, int outW, int outH, const std::wstring &device) {
        freeRes();
        if (srcW <= 0 || srcH <= 0 || outW <= 0 || outH <= 0) {
            qWarning("[vnc-cap] realloc 拒绝非法尺寸 src=%dx%d out=%dx%d\n", srcW, srcH, outW, outH);
            return false;
        }

        // 屏幕 DC：修复②v2 起按目标设备名建（CreateDC），抓指定物理屏；
        // 设备名为空或 CreateDC 失败才退 GetDC(nullptr)（= 主屏，老行为）。
        // ⚠️ 两种 DC 的释放方式不同：CreateDC → DeleteDC，GetDC → ReleaseDC，
        //    用 m_dcOwned 记住，混用会泄漏或崩。
        m_dcOwned = false;
        if (!device.empty()) {
            m_screenDc = reinterpret_cast<void *>(CreateDCW(L"DISPLAY", device.c_str(), nullptr, nullptr));
            if (!m_screenDc) {
                // 有些驱动只认 (NULL, devname) 形式，再试一种写法
                m_screenDc = reinterpret_cast<void *>(CreateDCW(nullptr, device.c_str(), nullptr, nullptr));
            }
            if (m_screenDc) m_dcOwned = true;
        }
        if (!m_screenDc) {
            m_screenDc = reinterpret_cast<void *>(GetDC(nullptr));
            if (!m_screenDc) {
                qWarning("[vnc-cap] GetDC(nullptr) 失败 err=%lu\n", GetLastError());
                return false;
            }
        }
        m_memDc = reinterpret_cast<void *>(CreateCompatibleDC(reinterpret_cast<HDC>(m_screenDc)));
        if (!m_memDc) {
            qWarning("[vnc-cap] CreateCompatibleDC 失败 err=%lu\n", GetLastError());
            return false;
        }

        // 源：原生分辨率
        m_srcBmp = createBgrDib(m_memDc, srcW, srcH, &m_srcBits);
        if (!m_srcBmp) {
            qWarning("[vnc-cap] 源 DIB 创建失败 err=%lu %dx%d\n", GetLastError(), srcW, srcH);
            return false;
        }

        if (outW == srcW && outH == srcH) {
            // 不缩放：源即输出，省一块 DIB 和一次 StretchBlt
            m_bmp = m_srcBmp;
            m_bits = m_srcBits;
        } else {
            m_bmp = createBgrDib(m_memDc, outW, outH, &m_bits);
            if (!m_bmp) {
                qWarning("[vnc-cap] 目标 DIB 创建失败 err=%lu %dx%d\n", GetLastError(), outW, outH);
                return false;
            }
        }

        m_bgrxCap = static_cast<size_t>(outW) * outH * 4;
        m_bgrx = static_cast<uint8_t *>(malloc(m_bgrxCap));
        if (!m_bgrx) {
            qWarning("[vnc-cap] 帧缓冲 malloc 失败 %zu 字节\n", m_bgrxCap);
            return false;
        }

        m_srcW = srcW;
        m_srcH = srcH;
        m_w = outW;
        m_h = outH;
        m_devName = device;
        return true;
    }

    /** 建一块 24bit BGR 的 DIB section（biHeight 取负 → 位图顶部在下，免翻转）。 */
    static void *createBgrDib(void *dc, int w, int h, void **outBits)
    {
        BITMAPINFOHEADER bh{};
        bh.biSize = sizeof(BITMAPINFOHEADER);
        bh.biWidth = w;
        bh.biHeight = -h;
        bh.biPlanes = 1;
        bh.biBitCount = 24; // BGR
        bh.biCompression = BI_RGB;
        void *bits = nullptr;
        void *bmp = reinterpret_cast<void *>(CreateDIBSection(reinterpret_cast<HDC>(dc),
                                                              reinterpret_cast<BITMAPINFO *>(&bh),
                                                              DIB_RGB_COLORS, &bits, nullptr, 0));
        if (!bmp) return nullptr;
        *outBits = bits;
        return bmp;
    }

    int m_w = 0, m_h = 0;        // 输出尺寸（= 目标尺寸，缩放时即 m_outW/H）
    int m_outW = 0, m_outH = 0;  // 调用方要求的目标尺寸（0 = 原生）

    std::wstring m_devName;      // 修复②v2：当前抓的物理屏设备名（空=主屏）
    void *m_screenDc = nullptr;  // HDC（目标屏；CreateDC/GetDC 来的）
    bool m_dcOwned = false;      // m_screenDc 是否 CreateDC 来的（决定释放方式）
    qint64 m_lastPickMs = 0;     // 修复②v2：上次重挑物理目标屏的时刻（~1s 一次）
    void *m_memDc = nullptr;      // HDC（兼容内存 DC，源与目标共用）
    void *m_srcBmp = nullptr;     // 原生分辨率 DIB section（BitBlt 目标）
    void *m_srcBits = nullptr;
    void *m_bmp = nullptr;        // 输出 DIB section（缩放目标；未缩放时与源同一块）
    void *m_bits = nullptr;       // 输出 DIB section 位（顶部在下，BGR）
    uint8_t *m_bgrx = nullptr;    // 转换后的 BGRX 缓冲（顶部在下）
    size_t m_bgrxCap = 0;
    int m_srcW = 0, m_srcH = 0;   // 当前桌面分辨率
    bool m_init = false;
};

// ── ScreenCapture 门面（Windows）────────────────────────────────────────────
ScreenCapture::ScreenCapture() {}
ScreenCapture::~ScreenCapture() { delete m_impl; m_impl = nullptr; }

bool ScreenCapture::init(int outW, int outH) {
    delete m_impl;
    m_impl = nullptr;
    m_backend = "none";

    // 主实现：DXGI Desktop Duplication（GPU 合成路径 + 系统级脏区）。
    // 初始化失败（无 D3D11、远程会话、老系统）自动回落 GDI 兜底。
    DxgiGrabber *dxgi = new DxgiGrabber();
    if (dxgi->init(outW, outH)) {
        m_impl = dxgi;
        m_backend = "dxgi";
        return true;
    }
    qWarning("[vnc-cap] DXGI 抓屏初始化失败，回落 GDI 兜底");
    delete dxgi;

    GdiGrabber *gdi = new GdiGrabber();
    if (gdi->init(outW, outH)) {
        m_impl = gdi;
        m_backend = "gdi";
        return true;
    }
    delete gdi;
    qWarning("[vnc-cap] GDI 抓屏初始化也失败，VNC 无画面");
    return false;
}

const uint8_t *ScreenCapture::grab(int &outW, int &outH) {
    if (!m_impl && !init()) return nullptr;
    outW = 0;
    outH = 0;
    return m_impl->grab(outW, outH);
}

#else // ── 非 Windows：占位 ──
ScreenCapture::ScreenCapture() {}
ScreenCapture::~ScreenCapture() {}
bool ScreenCapture::init(int, int) { return false; }
const uint8_t *ScreenCapture::grab(int &outW, int &outH) {
    (void)outW;
    (void)outH;
    return nullptr;
}
#endif
