#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 屏幕采集（被控端 in-agent VNC Server 用）—— 双实现门面
//
// 结构（2026-10-19 起，DXGI 化改造）：
//   ScreenCapture（门面）持一个 Grabber 实现，init() 时按优先级挑选：
//     · DxgiGrabber（screen_capture_dxgi.cpp，仅 Windows）：DXGI Desktop
//       Duplication，GPU 合成路径抓帧 + 系统级脏区（GetFrameDirtyRects/
//       GetFrameMoveRects），脏区逐块交给 libvncserver，CPU 成本远低于全屏重编码；
//     · GdiGrabber（本目录 screen_capture.cpp，仅 Windows）：BitBlt 抓主桌面
//       （原生分辨率）→ StretchBlt 缩放 → 转 BGRX。DXGI 初始化失败时的兜底；
//     · 非 Windows：占位（grab 返回 nullptr）。
//   后端名经 backend() 暴露（"dxgi"/"gdi"/"none"），起服日志打印供真机诊断。
//
// 像素格式：BGRX（32bit，蓝在字节 0），匹配 libvncserver 默认 server 像素格式
//   （trueColour，redShift=16/greenShift=8/blueShift=0）。
//   为什么 BGRX 而不是 RGB：小端 0xAARRGGBB 的内存布局字节 0 = B，喂 BGRX
//   最省一次 shuffle。DXGI 路径的 staging 纹理用 DXGI_FORMAT_B8G8R8A8_UNORM
//   本身就是这个布局，直拷零转换。
//
// 为什么要缩放（2026-10-18 实证）：2560x1440 原生 Raw 编码增量帧 ~40-400KB/批，
//   在 ~250KB/s 的劣化隧道上直接灌爆上行 ⇒ 心跳被挤掉 ⇒ 云端踢线 ⇒ 画面定格。
//   缩到 720p 后像素量降到 1/7，是投入最小、收益最大的一招。两个后端都输出
//   同一目标尺寸（由 init(outW,outH) 给定，v1 固定）。
//
// 脏区协议（本头新增）：grab() 之后调用方调 takeDirtyRects() 取走"本帧相对
//   上一帧变化了的区域"列表（输出坐标系、右下开区间）。GDI 后端没有脏区信息，
//   恒返回全屏矩形；DXGI 后端返回系统报告的脏区（最近邻缩放到输出尺寸）。
//   调用方（vnc_server 抓屏线程）据此逐块 rfbMarkRectAsModified，让
//   libvncserver 只编码变化的部分。
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <vector>

/// 脏区矩形：输出坐标系，右下开区间（[x, x+w) × [y, y+h)）。
struct GrabRect {
    int x = 0, y = 0, w = 0, h = 0;
};

/// 抓屏后端抽象：GDI / DXGI / （未来）X11 实现同一契约。
class Grabber {
public:
    virtual ~Grabber();

    /**
     * 初始化后端资源。outW/outH 为 0（默认）= 输出原生分辨率；
     * 给定非零值时按该尺寸输出（内部抓原生再缩放到目标尺寸）。
     * 桌面分辨率变化由实现自行处理（重建/复用缓冲）。
     */
    virtual bool init(int outW = 0, int outH = 0) = 0;

    /// 抓一帧，输出宽高；返回内部 BGRX 缓冲指针（下次 grab 覆盖，调用方勿长期持有）。
    /// 失败返回 nullptr（此时 takeDirtyRects 可能仍有上次内容，调用方应跳过本帧）。
    virtual const uint8_t *grab(int &outW, int &outH) = 0;

    virtual int width() const = 0;
    virtual int height() const = 0;
    /// init() 是否已成功（供长驻线程在 init 失败后按节奏重试前判断）。
    virtual bool ok() const = 0;

    /// 取走本帧脏区列表（不清空调用方传入的 vector，append 语义）。
    /// GDI 后端恒 push 全屏矩形；DXGI 后端为系统脏区（可能为空 = 无变化）。
    virtual void takeDirtyRects(std::vector<GrabRect> &out) = 0;
};

#ifdef _WIN32
class GdiGrabber;   // screen_capture.cpp：BitBlt 兜底实现
class DxgiGrabber;  // screen_capture_dxgi.cpp：DXGI Desktop Duplication 主实现

/// 2026-10-19 修复②v2：物理目标屏（两个抓屏后端共用同一选择结果）。
/// 背景（真机实证白屏）：本机装 GameViewer Virtual Display Adapter
/// （v15.6.5.199，DeviceID=ROOT\GameViewerIddDriver 软件 IDD 驱动），其
/// 虚拟屏按需激活会**抢走"主屏/原点"位**——GetSystemMetrics 主屏尺寸、
/// GetDC(nullptr)、DXGI"含原点输出"三套老判据全部跟随虚拟屏，抓到一片白
/// （虚拟屏无真实内容）⇒ 管理端远控整屏白（2026-10-19 16:41 实测）。
/// 新判据：枚举 GDI 显示设备，DeviceID 以 "ROOT\" 开头的是软件虚拟驱动
/// （GameViewer / 基本呈现驱动程序 / RDP 显示驱动都是 Root 枚举），
/// 物理屏是 PCI\VEN_xxxx 或 ACPI\ 枚举的真硬件输出。
struct SteTargetDisplay {
    bool ok = false;           ///< 是否选到了目标
    wchar_t device[40] = L"";  ///< L"\\.\DISPLAY1"（GDI CreateDC 设备名；空=主屏）
    int x = 0, y = 0;          ///< 虚拟桌面坐标（DXGI 输出矩形匹配用）
    int w = 0, h = 0;          ///< 原生分辨率
    bool primary = false;      ///< 是否 Windows 主屏
};
/// 枚举并挑选物理目标屏（每次调用重新枚举——拓扑会动态变）。
/// 优先级：物理主屏 > 物理含原点 > 物理面积大 > 首个物理屏；
/// 完全没有物理屏（纯 RDP / 纯虚拟环境）时退回当前主屏，保证仍有画面。
SteTargetDisplay pickTargetDisplay();
#endif

/// 门面：选后端 + 转发。生命周期内只持一个实现；析构时释放。
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture &) = delete;
    ScreenCapture &operator=(const ScreenCapture &) = delete;

    /**
     * 初始化。Windows 上优先 DxgiGrabber（DXGI Desktop Duplication），
     * 初始化失败（无 D3D11 / 老系统 / 独占全屏会话异常）自动回落 GdiGrabber。
     * outW/outH 为 0 = 原生分辨率；给定非零值 = 目标输出尺寸（内部缩放）。
     * 桌面分辨率变化由后端自行处理；目标尺寸保持调用时给定的值不变。
     */
    bool init(int outW = 0, int outH = 0);

    /// 抓取一帧（见 Grabber::grab）。
    const uint8_t *grab(int &outW, int &outH);

    int width() const { return m_impl ? m_impl->width() : 0; }
    int height() const { return m_impl ? m_impl->height() : 0; }
    bool ok() const { return m_impl && m_impl->ok(); }

    /// 当前后端名（"dxgi" / "gdi" / "none"），起服日志与真机诊断用。
    const char *backend() const { return m_backend; }

    /// 取走本帧脏区（见 Grabber::takeDirtyRects）。未初始化时无动作。
    void takeDirtyRects(std::vector<GrabRect> &out)
    {
        if (m_impl) m_impl->takeDirtyRects(out);
    }

    /// 每像素字节数（BGRX）。
    static constexpr int kBytesPerPixel = 4;

private:
    Grabber *m_impl = nullptr;
    const char *m_backend = "none";
};
