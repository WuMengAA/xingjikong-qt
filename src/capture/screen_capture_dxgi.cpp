// DXGI Desktop Duplication 抓屏实现（被控端 in-agent VNC Server 主后端）。
// 详见 screen_capture_dxgi.h。仅 Windows；非 Windows 走文件末尾占位。
// 探针前置验证（_probe/dxgi_probe.cpp，本机 PASS）：硬件 D3D11 + DuplicateOutput +
// staging 可读 + 脏区可用。本文件不依赖 Qt 业务代码，仅用 QDebug 打日志。

#include "screen_capture_dxgi.h"

#include <QDebug>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// Win7 软兼容（2026-10-24）：编译期钉死 Win7 目标 API 面（0x0601）。
// DXGI Desktop Duplication 在 Win7 上运行期必败（E_NOTIMPL），init=false
// 后由门面回落 GdiGrabber —— 但本 TU 仍以 0x0601 编译，防止编译期混入
// 更高系统版本的独占 API 声明。全局不设，仅本纯 Win32 TU 生效。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <objbase.h> // CoInitializeEx / CoUninitialize
#include <d3d11.h>
#include <dxgi1_2.h>
#include <QDateTime>
#include <cstdlib>
#include <cstring>
#include <vector>

// D3D11/DXGI 句柄与中间缓冲，全部藏这里（PIMPL）：头文件零 Windows 依赖。
struct DxgiGrabber::Impl {
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    IDXGIOutputDuplication *dup = nullptr;
    ID3D11Texture2D *staging = nullptr;

    bool firstFrame = true;  // 尚未拿到过完整帧（首帧必须全屏拷）
    bool frameHeld = false;  // AcquireNextFrame 成功但未 ReleaseFrame（防御用）
    bool comInited = false;  // 本线程 CoInitializeEx 成功过（析构时 Uninitialize）
    UINT dupW = 0, dupH = 0; // duplication 桌面尺寸

    // Map 期间的 staging 视图（copyRectScaled 用；Unmap 后置空）
    const uint8_t *mapPtr = nullptr;
    UINT mapRowPitch = 0;

    std::vector<RECT> dirtyBuf;                          // GetFrameDirtyRects 复用缓冲
    std::vector<DXGI_OUTDUPL_MOVE_RECT> moveBuf;         // GetFrameMoveRects 复用缓冲
};

// ── 构造 / 析构 ─────────────────────────────────────────────────────────────
DxgiGrabber::DxgiGrabber() = default;

DxgiGrabber::~DxgiGrabber()
{
    freeRes();
}

void DxgiGrabber::freeRes()
{
    if (!m_p) return;
    Impl *p = m_p;
    if (p->frameHeld && p->dup) {
        // 防御：理论上每条路径都会 ReleaseFrame；真漏了在这里补，避免库状态错乱
        p->dup->ReleaseFrame();
        p->frameHeld = false;
    }
    releaseDuplication();
    if (p->ctx) { p->ctx->Release(); p->ctx = nullptr; }
    if (p->dev) { p->dev->Release(); p->dev = nullptr; }
    if (p->comInited) { CoUninitialize(); p->comInited = false; }
    delete p;
    m_p = nullptr;
    free(m_bgrx);
    m_bgrx = nullptr;
    m_bgrxCap = 0;
    m_w = m_h = 0;
    m_srcW = m_srcH = 0;
    m_ok = false;
}

void DxgiGrabber::releaseDuplication()
{
    if (!m_p) return;
    Impl *p = m_p;
    if (p->frameHeld && p->dup) {
        p->dup->ReleaseFrame();
        p->frameHeld = false;
    }
    if (p->staging) { p->staging->Release(); p->staging = nullptr; }
    if (p->dup) { p->dup->Release(); p->dup = nullptr; }
    p->mapPtr = nullptr;
    p->mapRowPitch = 0;
}

bool DxgiGrabber::rebuildStaging(int srcW, int srcH)
{
    Impl *p = m_p;
    if (!p || !p->dev || srcW <= 0 || srcH <= 0) return false;
    if (p->staging) { p->staging->Release(); p->staging = nullptr; }

    // B8G8R8A8_UNORM：内存布局即 RFB BGRX（字节 0 = B），拷出来零转换。
    // STAGING + CPU_ACCESS_READ：GPU → CPU 可读。
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(srcW);
    td.Height = static_cast<UINT>(srcH);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const HRESULT hr = p->dev->CreateTexture2D(&td, nullptr, &p->staging);
    if (FAILED(hr)) {
        qWarning("[vnc-cap-dxgi] staging 纹理创建失败 %dx%d hr=%08lx", srcW, srcH,
                 static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

// ── 适配器身份比较（2026-10-19 修复②v2）────────────────────────────────────
// 为什么不能比 COM 指针：wantAdapter 来自本函数里 CreateDXGIFactory 新建的
// 工厂，而设备侧 GetAdapter 返回的指针出自设备内部的工厂——同一个物理适配器
// 在两个工厂里是**不同的 COM 实例**，指针比恒不等 ⇒ 白白换设备。改比适配器
// 描述（VendorId/DeviceId/SubSysId/Revision + 描述串），跨工厂稳定。
static bool sameDxgiAdapter(IDXGIAdapter *a, IDXGIAdapter *b)
{
    if (!a || !b) return false;
    if (a == b) return true;   // 同工厂同实例，快路径
    DXGI_ADAPTER_DESC da{};
    DXGI_ADAPTER_DESC db{};
    if (FAILED(a->GetDesc(&da)) || FAILED(b->GetDesc(&db))) return false;
    return da.VendorId == db.VendorId && da.DeviceId == db.DeviceId
        && da.SubSysId == db.SubSysId && da.Revision == db.Revision
        && _wcsicmp(da.Description, db.Description) == 0;
}

bool DxgiGrabber::buildDuplication()
{
    Impl *p = m_p;
    if (!p) return false;
    releaseDuplication(); // 幂等：重建前先放掉旧的 dup/staging

    // ── 修复②v2（2026-10-19）：按物理目标屏的桌面坐标矩形精确匹配输出 ────────
    // v1 教训（真机实证白屏）：v1 用 GetSystemMetrics 主屏尺寸 + "含原点"选
    //   输出，但 GameViewer 虚拟屏激活后**它自己就是主屏**（含原点），v1 的
    //   "精确命中"直接命中虚拟屏（一片白）；更糟的是虚拟屏所在 IDD 软件适配器
    //   上 D3D11CreateDevice/DuplicateOutput 连败两次（hr=80070057），回落
    //   GDI 抓到的还是虚拟屏 ⇒ 管理端整屏白。
    // v2 判据：pickTargetDisplay() 枚举 GDI 显示设备、按 DeviceID 的 "ROOT\"
    //   前缀排除软件虚拟驱动，钉住真硬件屏；DXGI 侧按该屏的桌面坐标矩形
    //   （位置 + 尺寸）在**全部适配器**上找输出——DesktopCoordinates 是物理
    //   桌面坐标，与进程 DPI 感知无关。设备跟着目标输出所在适配器走。
    const SteTargetDisplay target = pickTargetDisplay();
    if (target.ok) {
        qInfo("[vnc-cap-dxgi] 物理目标屏 %ls %dx%d@(%d,%d)%s", target.device,
              target.w, target.h, target.x, target.y, target.primary ? "（主屏）" : "");
    } else {
        qWarning("[vnc-cap-dxgi] 物理目标屏枚举失败，退回旧口径（主屏尺寸 + 含原点）");
    }
    IDXGIAdapter *wantAdapter = nullptr;   // 目标屏所在适配器（持有 1 条引用，用完 Release）
    IDXGIOutput *wantOutput = nullptr;     // 目标屏输出（持有 1 条引用，用完 Release）
    IDXGIAdapter *anyAdapter = nullptr;    // 备选：任一含原点输出所在适配器（同样持 1 条）
    IDXGIOutput *anyOutput = nullptr;      // 备选：任一含原点输出（同样持 1 条）
    // 三级选择：①输出矩形与物理目标屏完全一致（位置+尺寸，精确）；
    //   ②旧口径兜底：含原点且尺寸与 GetSystemMetrics 主屏一致；
    //   ③备选：任一含原点输出（异构/镜像等怪拓扑的口径）。
    // 引用所有权：want*/any* 各自持 1 条独立引用（转交时另外 AddRef，
    //   EnumOutputs 的引用直接转交不 Release）；循环自己的 ad 引用在迭代末释放。
    {
        IDXGIFactory *factory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                        reinterpret_cast<void **>(&factory)))) {
            for (UINT ai = 0;; ++ai) {
                IDXGIAdapter *ad = nullptr;
                if (FAILED(factory->EnumAdapters(ai, &ad))) break;   // 枚举完
                for (UINT oi = 0;; ++oi) {
                    IDXGIOutput *o = nullptr;
                    if (FAILED(ad->EnumOutputs(oi, &o))) break;      // 枚举完
                    DXGI_OUTPUT_DESC od{};
                    const bool descOk = SUCCEEDED(o->GetDesc(&od));
                    const int ox = descOk ? od.DesktopCoordinates.left : 0;
                    const int oy = descOk ? od.DesktopCoordinates.top : 0;
                    const int ow = descOk ? od.DesktopCoordinates.right - od.DesktopCoordinates.left : 0;
                    const int oh = descOk ? od.DesktopCoordinates.bottom - od.DesktopCoordinates.top : 0;
                    const bool origin = descOk && ox == 0 && oy == 0;
                    bool transferred = false;   // o 的引用是否已转交（不再 Release）
                    if (target.ok && descOk && ox == target.x && oy == target.y
                        && ow == target.w && oh == target.h) {
                        // ① 精确命中：矩形与物理目标屏一致 ⇒ 转交 want*
                        if (anyOutput) { anyOutput->Release(); anyOutput = nullptr; }
                        if (anyAdapter) { anyAdapter->Release(); anyAdapter = nullptr; }
                        wantAdapter = ad;
                        wantAdapter->AddRef();
                        wantOutput = o;     // EnumOutputs 引用直接转交
                        transferred = true;
                    } else if (origin && !wantOutput) {
                        // ② 旧口径兜底（target.ok=false 或矩形没匹配上时）
                        const int primW = GetSystemMetrics(SM_CXSCREEN);
                        const int primH = GetSystemMetrics(SM_CYSCREEN);
                        if (primW > 0 && primH > 0 && ow == primW && oh == primH) {
                            if (anyOutput) { anyOutput->Release(); anyOutput = nullptr; }
                            if (anyAdapter) { anyAdapter->Release(); anyAdapter = nullptr; }
                            wantAdapter = ad;
                            wantAdapter->AddRef();
                            wantOutput = o;
                            transferred = true;
                        } else if (!anyOutput) {
                            anyAdapter = ad;
                            anyAdapter->AddRef();
                            anyOutput = o;  // EnumOutputs 引用直接转交
                            transferred = true;
                        }
                    }
                    if (!transferred) o->Release();
                    if (wantOutput) break;
                }
                ad->Release();   // 循环自己的引用（want/any 若指向它都另持有 AddRef）
                if (wantOutput) break;
            }
            factory->Release();
        }
    }
    // 没有精确命中但有备选 ⇒ 用备选（含原点即主屏）
    if (!wantOutput && anyOutput) {
        wantAdapter = anyAdapter;
        wantOutput = anyOutput;
    }

    // 设备适配器与目标屏不在同一个适配器上 ⇒ 换设备（DuplicateOutput 要求同适配器）。
    // ⚠️ 比较必须走 sameDxgiAdapter（描述判据）：wantAdapter 出自本函数新建的
    //   工厂，设备侧 GetAdapter 出自设备内部工厂，同一物理适配器是两个 COM
    //   实例，指针比恒不等（v1 踩过：无谓换设备 + 日志误导）。
    if (wantOutput && p->dev) {
        IDXGIDevice *dd = nullptr;
        bool same = false;
        if (SUCCEEDED(p->dev->QueryInterface(__uuidof(IDXGIDevice),
                                             reinterpret_cast<void **>(&dd)))) {
            IDXGIAdapter *cur = nullptr;
            if (SUCCEEDED(dd->GetAdapter(&cur))) {
                same = sameDxgiAdapter(cur, wantAdapter);
                cur->Release();
            }
            dd->Release();
        }
        if (!same) {
            if (p->ctx) { p->ctx->Release(); p->ctx = nullptr; }
            if (p->dev) { p->dev->Release(); p->dev = nullptr; }
            qInfo("[vnc-cap-dxgi] 主屏输出不在当前设备适配器上，换到主屏所在适配器");
        }
    }

    if (!p->dev) {
        // COM：抓屏线程 fresh，按 MTA 初始化；本线程已被别人按 STA 初始化过
        // （RPC_E_CHANGED_MODE）则容忍——D3D11 设备创建在两种套间下都能成功。
        const HRESULT chr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (chr == S_OK || chr == S_FALSE) {
            p->comInited = true;
        } else if (chr != RPC_E_CHANGED_MODE) {
            qWarning("[vnc-cap-dxgi] CoInitializeEx 失败 hr=%08lx",
                     static_cast<unsigned long>(chr));
            if (wantOutput) { wantOutput->Release(); wantOutput = nullptr; }
            if (wantAdapter) { wantAdapter->Release(); wantAdapter = nullptr; }
            return false;
        }

        static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = E_FAIL;
        if (wantAdapter) {
            // 指定适配器时驱动类型必须是 UNKNOWN（否则 E_INVALIDARG）。
            hr = D3D11CreateDevice(wantAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, 2,
                                   D3D11_SDK_VERSION, &p->dev, nullptr, &p->ctx);
        }
        if (FAILED(hr)) {
            // 主屏适配器上创建设备失败（老显卡驱动？）⇒ 回默认适配器硬件/WARP
            // 两连击（原逻辑）。此时 wantOutput 属于别的适配器，不能再用它，
            // 下面统一走"设备适配器上找含原点者"的回退路径。
            static const D3D_DRIVER_TYPE kTypes[] = {D3D_DRIVER_TYPE_HARDWARE,
                                                     D3D_DRIVER_TYPE_WARP};
            for (D3D_DRIVER_TYPE t : kTypes) {
                hr = D3D11CreateDevice(nullptr, t, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, 2,
                                       D3D11_SDK_VERSION, &p->dev, nullptr, &p->ctx);
                if (SUCCEEDED(hr)) break;
            }
            if (wantOutput) { wantOutput->Release(); wantOutput = nullptr; }
            if (wantAdapter) { wantAdapter->Release(); wantAdapter = nullptr; }
        }
        if (FAILED(hr)) {
            qWarning("[vnc-cap-dxgi] D3D11CreateDevice 失败 hr=%08lx",
                     static_cast<unsigned long>(hr));
            if (p->comInited) { CoUninitialize(); p->comInited = false; }
            return false;
        }
    }

    // 输出：优先主屏输出（含原点 + 尺寸对齐）；主屏没找到/设备换了适配器，
    // 退回"设备适配器上第一个含原点者"（镜像/异构等怪拓扑的口径）；
    // 一个都没有 ⇒ 语义无法保证，交 GDI 兜底。
    IDXGIOutput *output = nullptr;
    if (wantOutput) {
        output = wantOutput;   // 所有权移交给下面（QI 后 Release）
    } else {
        IDXGIDevice *dxgiDev = nullptr;
        if (FAILED(p->dev->QueryInterface(__uuidof(IDXGIDevice),
                                          reinterpret_cast<void **>(&dxgiDev)))) {
            qWarning("[vnc-cap-dxgi] QueryInterface(IDXGIDevice) 失败");
            return false;
        }
        IDXGIAdapter *adapter = nullptr;
        if (FAILED(dxgiDev->GetAdapter(&adapter))) {
            qWarning("[vnc-cap-dxgi] GetAdapter 失败");
            dxgiDev->Release();
            return false;
        }
        dxgiDev->Release();

        for (UINT i = 0;; ++i) {
            IDXGIOutput *o = nullptr;
            if (FAILED(adapter->EnumOutputs(i, &o))) break; // 枚举完
            DXGI_OUTPUT_DESC od{};
            const bool isPrimary =
                SUCCEEDED(o->GetDesc(&od)) && od.DesktopCoordinates.left == 0 &&
                od.DesktopCoordinates.top == 0;
            if (isPrimary) {
                output = o;
                break;
            }
            o->Release();
        }
        adapter->Release();
        if (!output) {
            qWarning("[vnc-cap-dxgi] 设备适配器上找不到含原点的输出（多显卡异构？），交 GDI 兜底");
            return false;
        }
    }

    IDXGIOutput1 *out1 = nullptr;
    const bool gotOut1 =
        SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1),
                                         reinterpret_cast<void **>(&out1)));
    output->Release();
    if (wantAdapter) { wantAdapter->Release(); wantAdapter = nullptr; }
    if (!gotOut1 || !out1) {
        qWarning("[vnc-cap-dxgi] QueryInterface(IDXGIOutput1) 失败");
        return false;
    }

    const HRESULT hr = out1->DuplicateOutput(p->dev, &p->dup);
    out1->Release();
    if (FAILED(hr)) {
        qWarning("[vnc-cap-dxgi] DuplicateOutput 失败 hr=%08lx（RDP 会话/无输出常见）",
                 static_cast<unsigned long>(hr));
        return false;
    }

    DXGI_OUTDUPL_DESC dd{};
    p->dup->GetDesc(&dd);
    p->dupW = dd.ModeDesc.Width;
    p->dupH = dd.ModeDesc.Height;
    p->firstFrame = true;
    m_firstFrameStartMs = 0; // 重建后重新等待首个有内容的帧
    qInfo("[vnc-cap-dxgi] DuplicateOutput OK，桌面 %ux%u", p->dupW, p->dupH);
    return true;
}

void DxgiGrabber::rebuildMaps()
{
    if (m_w <= 0 || m_h <= 0 || m_srcW <= 0 || m_srcH <= 0) return;
    m_colMap.resize(static_cast<size_t>(m_w));
    m_rowMap.resize(static_cast<size_t>(m_h));
    // 最近邻：输出像素 ox 采样源列 floor(ox*srcW/m_w)（与 copyRectScaled 的
    // 盒映射同为保守超集语义，保证不漏任何变化像素）。
    for (int ox = 0; ox < m_w; ++ox)
        m_colMap[static_cast<size_t>(ox)] =
            static_cast<int>((static_cast<long long>(ox) * m_srcW) / m_w);
    for (int oy = 0; oy < m_h; ++oy)
        m_rowMap[static_cast<size_t>(oy)] =
            static_cast<int>((static_cast<long long>(oy) * m_srcH) / m_h);
}

void DxgiGrabber::ensureBgrx(int w, int h)
{
    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (m_bgrx && m_bgrxCap >= need) return;
    free(m_bgrx);
    m_bgrx = static_cast<uint8_t *>(malloc(need ? need : 1));
    m_bgrxCap = need;
    if (!m_bgrx)
        qWarning("[vnc-cap-dxgi] 帧缓冲 malloc 失败 %zu 字节", need);
}

bool DxgiGrabber::applyDupSize()
{
    Impl *p = m_p;
    if (!p || !p->dup) return false;
    DXGI_OUTDUPL_DESC dd{};
    p->dup->GetDesc(&dd);
    const int sw = static_cast<int>(dd.ModeDesc.Width);
    const int sh = static_cast<int>(dd.ModeDesc.Height);
    if (sw <= 0 || sh <= 0) {
        qWarning("[vnc-cap-dxgi] duplication 尺寸非法 %dx%d", sw, sh);
        return false;
    }
    if (sw != m_srcW || sh != m_srcH || !p->staging) {
        if (!rebuildStaging(sw, sh)) return false;
    }
    m_srcW = sw;
    m_srcH = sh;

    int tw = m_outW, th = m_outH;
    if (tw <= 0 || th <= 0) { tw = sw; th = sh; } // 0 = 原生
    m_w = tw;
    m_h = th;
    ensureBgrx(tw, th);
    if (!m_bgrx) return false;
    rebuildMaps();
    return true;
}

// ── init / grab ────────────────────────────────────────────────────────────
bool DxgiGrabber::init(int outW, int outH)
{
    if (m_p) freeRes();
    m_p = new Impl();
    m_outW = outW;
    m_outH = outH;
    m_ok = false;
    if (!buildDuplication()) return false;
    if (!applyDupSize()) return false;
    m_ok = true;
    return true;
}

const uint8_t *DxgiGrabber::grab(int &outW, int &outH)
{
    Impl *p = m_p;
    if (!p) return nullptr;
    m_dirty.clear();

    // ── 重建路径（上次 ACCESS_LOST 重建失败 / 从未建成）：限流 1s 重试 ──
    if (!p->dup) {
        const long long now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastRebuildMs < 1000) return nullptr;
        m_lastRebuildMs = now;
        if (!buildDuplication()) return nullptr;
        if (!applyDupSize()) return nullptr;
        // 重建成功：本帧仍无新内容，返回 nullptr；下一帧按首帧全屏处理。
        return nullptr;
    }

    // 防御：异常路径漏掉的 ReleaseFrame 在这里补（正常路径下方都成对释放）
    if (p->frameHeld) {
        p->dup->ReleaseFrame();
        p->frameHeld = false;
    }

    DXGI_OUTDUPL_FRAME_INFO fi{};
    IDXGIResource *res = nullptr;
    // 首帧给 100ms 等合成器出图；其后非阻塞（0ms）——没有新帧就本帧不推。
    const HRESULT hr = p->dup->AcquireNextFrame(p->firstFrame ? 100 : 0, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return nullptr; // 无新帧：m_dirty 空，vnc_server 无块可 mark
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // 桌面切换 / 分辨率变化 / UAC 切屏：释放后限流重建
        releaseDuplication();
        const long long now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastRebuildMs >= 1000) {
            m_lastRebuildMs = now;
            if (buildDuplication() && applyDupSize())
                qInfo("[vnc-cap-dxgi] ACCESS_LOST 后重建成功（桌面 %dx%d）", m_srcW, m_srcH);
            else
                qWarning("[vnc-cap-dxgi] ACCESS_LOST 后重建失败，按 1s 节奏重试");
        }
        return nullptr;
    }
    if (FAILED(hr)) {
        qWarning("[vnc-cap-dxgi] AcquireNextFrame 失败 hr=%08lx",
                 static_cast<unsigned long>(hr));
        if (res) res->Release();
        return nullptr;
    }
    if (!res) {
        p->dup->ReleaseFrame();
        return nullptr;
    }
    p->frameHeld = true;

    ID3D11Texture2D *src = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&src));
    res->Release();
    if (!src) {
        p->dup->ReleaseFrame();
        p->frameHeld = false;
        return nullptr;
    }

    // ── 收集本帧要处理的源矩形（桌面坐标）与对应输出脏区盒 ──
    // 首帧：全屏（duplication 首帧即完整桌面，且此时 m_bgrx 是未初始化缓冲）。
    // ⚠️ m_dirty 的首帧全屏盒在下方黑帧检查通过后才 push（见文末）。
    std::vector<GrabRect> srcRects;
    if (p->firstFrame) {
        srcRects.push_back(GrabRect{0, 0, m_srcW, m_srcH});
    } else {
        UINT need = 0;
        if (SUCCEEDED(p->dup->GetFrameDirtyRects(0, nullptr, &need)) && need > 0) {
            p->dirtyBuf.resize(need);
            UINT got = 0;
            if (SUCCEEDED(p->dup->GetFrameDirtyRects(need, p->dirtyBuf.data(), &got))) {
                for (UINT i = 0; i < got; ++i) {
                    const RECT &r = p->dirtyBuf[i];
                    srcRects.push_back(GrabRect{r.left, r.top, r.right - r.left,
                                                r.bottom - r.top});
                    GrabRect o;
                    if (mapSrcToOut(r.left, r.top, r.right, r.bottom, o))
                        m_dirty.push_back(o);
                }
            }
        }
        if (SUCCEEDED(p->dup->GetFrameMoveRects(0, nullptr, &need)) && need > 0) {
            p->moveBuf.resize(need);
            UINT got = 0;
            if (SUCCEEDED(p->dup->GetFrameMoveRects(need, p->moveBuf.data(), &got))) {
                for (UINT i = 0; i < got; ++i) {
                    const DXGI_OUTDUPL_MOVE_RECT &m = p->moveBuf[i];
                    const int mw = m.DestinationRect.right - m.DestinationRect.left;
                    const int mh = m.DestinationRect.bottom - m.DestinationRect.top;
                    if (mw <= 0 || mh <= 0) continue;
                    // 新帧纹理已是移动后内容：目标区 + 源区都从新纹理重取即正确。
                    srcRects.push_back(GrabRect{m.DestinationRect.left, m.DestinationRect.top,
                                                mw, mh});
                    srcRects.push_back(GrabRect{m.SourcePoint.x, m.SourcePoint.y, mw, mh});
                    GrabRect o;
                    if (mapSrcToOut(m.DestinationRect.left, m.DestinationRect.top,
                                    m.DestinationRect.right, m.DestinationRect.bottom, o))
                        m_dirty.push_back(o);
                    if (mapSrcToOut(m.SourcePoint.x, m.SourcePoint.y,
                                    m.SourcePoint.x + mw, m.SourcePoint.y + mh, o))
                        m_dirty.push_back(o);
                }
            }
        }
    }

    // 无变化：跳过 GPU→CPU 全纹理拷贝（静态画面零成本，这是 DXGI 路的核心收益）。
    if (srcRects.empty()) {
        src->Release();
        p->dup->ReleaseFrame();
        p->frameHeld = false;
        return nullptr;
    }

    // GPU 纹理 → staging → Map → 逐源矩形最近邻拷进输出 BGRX
    p->ctx->CopyResource(p->staging, src);
    src->Release();
    D3D11_MAPPED_SUBRESOURCE map{};
    const HRESULT mhr = p->ctx->Map(p->staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(mhr)) {
        qWarning("[vnc-cap-dxgi] staging Map 失败 hr=%08lx",
                 static_cast<unsigned long>(mhr));
        p->dup->ReleaseFrame();
        p->frameHeld = false;
        return nullptr;
    }
    p->mapPtr = static_cast<const uint8_t *>(map.pData);
    p->mapRowPitch = map.RowPitch;
    for (const GrabRect &r : srcRects)
        copyRectScaled(r.x, r.y, r.x + r.w, r.y + r.h);
    p->ctx->Unmap(p->staging, 0);
    p->mapPtr = nullptr;
    p->mapRowPitch = 0;
    p->dup->ReleaseFrame();
    p->frameHeld = false;

    if (p->firstFrame) {
        // ── 已知库行为（2026-10-19 真机实证）：DuplicateOutput 后的**首帧恒为全黑** ──
        // 实测：DWM 合成开着、输出/格式都对，但首帧 LastPresentTime=0、纹理全 0，
        // 第 2 帧起才有真实内容（本机 present 节奏约 1s，由岛时钟等重绘驱动）。
        // 若把黑帧交付给客户端就是「开机黑屏 1s」；若不交付又可能永远等不到
        // （真黑桌面）。折中：黑帧跳过并保持 firstFrame 重试，2s 预算内等到
        // 首个有内容的帧就交付；预算过后仍黑 ⇒ 桌面本身就是黑的，如实交付。
        const long long now = QDateTime::currentMSecsSinceEpoch();
        if (m_firstFrameStartMs == 0) {
            m_firstFrameStartMs = now;
            qInfo("[vnc-cap-dxgi] 首帧黑帧（已知库行为），等待首个有内容的帧（预算 2s）");
        }
        const bool black = isBlackFrame();
        if (black && now - m_firstFrameStartMs < 2000) {
            m_dirty.clear();
            return nullptr; // 不交付黑帧；下帧重试（firstFrame 保持 true）
        }
        p->firstFrame = false;
        m_dirty.push_back(GrabRect{0, 0, m_w, m_h});
        if (m_firstFrameStartMs != 0)
            qInfo("[vnc-cap-dxgi] 首帧已交付（%s，等待 %lldms）", black ? "黑帧(预算耗尽)" : "有内容",
                  now - m_firstFrameStartMs);
        m_firstFrameStartMs = 0;
    }
    outW = m_w;
    outH = m_h;
    return m_bgrx;
}

bool DxgiGrabber::isBlackFrame() const
{
    if (!m_bgrx || m_w <= 0 || m_h <= 0) return false;
    // 8x8 网格 + 底边（任务栏区）采样，全部为 0 才判黑。
    for (int gy = 0; gy < 8; ++gy) {
        const int y = (gy == 7) ? (m_h - 1) : (gy * m_h / 8);
        for (int gx = 0; gx < 8; ++gx) {
            const int x = (gx * m_w) / 8;
            const uint8_t *p = m_bgrx + (static_cast<size_t>(y) * m_w + x) * 4;
            if ((p[0] | p[1] | p[2]) != 0) return false;
        }
    }
    return true;
}

void DxgiGrabber::takeDirtyRects(std::vector<GrabRect> &out)
{
    out.insert(out.end(), m_dirty.begin(), m_dirty.end());
    m_dirty.clear();
}

bool DxgiGrabber::mapSrcToOut(int sx1, int sy1, int sx2, int sy2, GrabRect &out)
{
    if (m_w <= 0 || m_h <= 0 || m_srcW <= 0 || m_srcH <= 0) return false;

    // clamp 源矩形到桌面范围（DXGI 脏区偶尔会给桌面外的坐标）
    if (sx1 < 0) sx1 = 0;
    if (sy1 < 0) sy1 = 0;
    if (sx2 > m_srcW) sx2 = m_srcW;
    if (sy2 > m_srcH) sy2 = m_srcH;
    if (sx1 >= sx2 || sy1 >= sy2) return false;

    // 源矩形 → 输出盒：保守超集（宁多编一列，不漏像素——漏了就是画面残影）。
    // 下界 floor、上界 ceil；同尺寸时公式退化为恒等。
    const int ox1 = static_cast<int>((static_cast<long long>(sx1) * m_w) / m_srcW);
    const int ox2 = static_cast<int>((static_cast<long long>(sx2) * m_w + m_srcW - 1) / m_srcW);
    const int oy1 = static_cast<int>((static_cast<long long>(sy1) * m_h) / m_srcH);
    const int oy2 = static_cast<int>((static_cast<long long>(sy2) * m_h + m_srcH - 1) / m_srcH);
    if (ox1 >= ox2 || oy1 >= oy2) return false;
    out = GrabRect{ox1, oy1, ox2 - ox1, oy2 - oy1};
    return true;
}

void DxgiGrabber::copyRectScaled(int sx1, int sy1, int sx2, int sy2)
{
    Impl *p = m_p;
    if (!p || !p->mapPtr || !m_bgrx) return;

    GrabRect box;
    if (!mapSrcToOut(sx1, sy1, sx2, sy2, box)) return;
    const int ox1 = box.x, oy1 = box.y;
    const int ox2 = box.x + box.w, oy2 = box.y + box.h;

    // 1:1（屏幕 ≤ 720p 档）：整行 memcpy
    if (m_w == m_srcW && m_h == m_srcH) {
        for (int oy = oy1; oy < oy2; ++oy) {
            const uint8_t *srow = p->mapPtr + static_cast<size_t>(oy) * p->mapRowPitch +
                                  static_cast<size_t>(ox1) * 4;
            uint8_t *drow = m_bgrx + (static_cast<size_t>(oy) * m_w + ox1) * 4;
            std::memcpy(drow, srow, static_cast<size_t>(ox2 - ox1) * 4);
        }
        return;
    }

    for (int oy = oy1; oy < oy2; ++oy) {
        const uint8_t *srow = p->mapPtr +
                              static_cast<size_t>(m_rowMap[static_cast<size_t>(oy)]) *
                                  p->mapRowPitch;
        uint8_t *drow = m_bgrx + static_cast<size_t>(oy) * m_w * 4;
        for (int ox = ox1; ox < ox2; ++ox) {
            const uint8_t *s = srow + static_cast<size_t>(m_colMap[static_cast<size_t>(ox)]) * 4;
            uint8_t *d = drow + static_cast<size_t>(ox) * 4;
            d[0] = s[0]; // B
            d[1] = s[1]; // G
            d[2] = s[2]; // R
            d[3] = 0;    // X
        }
    }
}

#else // ── 非 Windows：占位（DXGI 不存在，ScreenCapture 门面会回落 GDI/无后端）──
DxgiGrabber::DxgiGrabber() = default;
DxgiGrabber::~DxgiGrabber() = default;
bool DxgiGrabber::init(int, int) { return false; }
const uint8_t *DxgiGrabber::grab(int &outW, int &outH)
{
    (void)outW;
    (void)outH;
    return nullptr;
}
void DxgiGrabber::takeDirtyRects(std::vector<GrabRect> &) {}
#endif
