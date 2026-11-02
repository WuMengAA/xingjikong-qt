#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DXGI Desktop Duplication 抓屏（被控端 in-agent VNC Server 主后端，仅 Windows）
//
// 为什么换掉 GDI BitBlt（2026-10-18 晚实证）：
//   · BitBlt 走 GDI 合成路径，每帧全屏 CPU 拷贝 + 逐像素 BGR→BGRX 转换，
//     2560x1440 下抓一帧 ~15-25ms CPU，30fps 就吃掉近一个核；
//   · DXGI Desktop Duplication 直接读 DWM 合成后的 GPU 纹理，staging 纹理
//     用 B8G8R8A8_UNORM（内存布局即 RFB BGRX，零转换），且系统直接报告
//     脏区矩形（GetFrameDirtyRects / GetFrameMoveRects）——只拷变化的部分，
//     静态画面几乎零成本；
//   · 抓屏线程改「非阻塞 AcquireNextFrame」：没有新帧就本帧不推，
//     libvncserver 侧无脏区可编码，天然按内容变化率推送。
//
// 关键设计：
//   · PIMPL：D3D11/DXGI 句柄全部藏在 .cpp 的 Impl 里，头文件零 Windows 依赖；
//   · 输出 = DesktopCoordinates 含原点 (0,0) 的那个输出（= 主显示器），
//     与 GDI GetDC(nullptr) 语义一致；
//   · 脏区最近邻缩放到输出尺寸：源矩形 [sx1,sx2) → 输出盒
//     [floor(sx1*dstW/srcW), ceil(sx2*dstW/srcW))（取**超集**，宁多编一列
//     不可漏像素——漏像素 = 画面撕裂残留）；
//   · MoveRects：新帧纹理已是移动后内容，目标区 + 源区都从新纹理重取即正确；
//   · ACCESS_LOST（桌面切换/模式变化/UAC 切屏）：释放 duplication 后按
//     节奏重建（限流 1s 一次），重建后首帧按全屏处理；
//   · 独占全屏 DX 游戏：DWM 不合成 ⇒ 可能长时间无帧或 ACCESS_LOST，
//     这是 DXGI 的已知边界（GDI 兜底同样抓不到），v1 接受。
//
// Win7 软兼容（2026-10-24）：DXGI Desktop Duplication 是 Win8+ 的系统能力，
// Win7 上 CreateDXGIFactory/D3D11CreateDevice 可能成功（WDDM 1.1 有 D3D11），
// 但 DuplicateOutput 必败（hr=E_NOTIMPL/DXGI_ERROR_UNSUPPORTED）⇒ init=false，
// 门面 ScreenCapture::init 自动回落 GdiGrabber（screen_capture.cpp），
// 缩略图上报与 VNC 在 Win7 上稳定走 GDI 兜底——**预期行为，不是错误**。
// ─────────────────────────────────────────────────────────────────────────────
#include "screen_capture.h" // Grabber / GrabRect
#include <cstdint>

class DxgiGrabber : public Grabber {
public:
    DxgiGrabber();
    ~DxgiGrabber() override;
    DxgiGrabber(const DxgiGrabber &) = delete;
    DxgiGrabber &operator=(const DxgiGrabber &) = delete;

    bool init(int outW = 0, int outH = 0) override;
    const uint8_t *grab(int &outW, int &outH) override;
    int width() const override { return m_w; }
    int height() const override { return m_h; }
    bool ok() const override { return m_ok; }
    void takeDirtyRects(std::vector<GrabRect> &out) override;

private:
    struct Impl; // D3D11/DXGI 句柄（.cpp 内定义）
    Impl *m_p = nullptr;

    uint8_t *m_bgrx = nullptr; // 输出 BGRX 缓冲（顶部在下）
    size_t m_bgrxCap = 0;
    int m_w = 0, m_h = 0;        // 输出尺寸
    int m_outW = 0, m_outH = 0;  // 调用方要求的目标尺寸（0 = 跟随桌面）
    int m_srcW = 0, m_srcH = 0;  // duplication 桌面尺寸
    bool m_ok = false;
    std::vector<GrabRect> m_dirty; // 本帧脏区（输出坐标系）

    long long m_lastRebuildMs = 0; // 上次重建 duplication 的时刻（限流 1s，见 grab）
    long long m_firstFrameStartMs = 0; // 首帧等待计时起点（黑帧跳过预算，见 grab）

    // 缩放映射表（输出坐标 → 源坐标），尺寸变化时重建；避免每像素做除法
    std::vector<int> m_colMap; // 大小 m_w
    std::vector<int> m_rowMap; // 大小 m_h

    void freeRes();
    bool buildDuplication();    // 建 dev/ctx/dup（幂等前置：先释放 dup）
    void releaseDuplication();  // 只释放 dup/staging（保留 dev/ctx）
    /// 按当前 dup 尺寸定 staging/输出尺寸/帧缓冲/映射表；init 与重建后调用。
    bool applyDupSize();
    bool rebuildStaging(int srcW, int srcH);
    void rebuildMaps();
    void ensureBgrx(int w, int h);
    /// 源矩形（桌面坐标）按最近邻拷进输出缓冲；要求 staging 已 Map。
    void copyRectScaled(int sx1, int sy1, int sx2, int sy2);
    /// 源矩形（桌面坐标）→ 输出脏区盒（与 copyRectScaled 同一保守超集公式）。
    /// 返回 false = 映射后为空（无需交付）。
    bool mapSrcToOut(int sx1, int sy1, int sx2, int sy2, GrabRect &out);
    /// 当前输出缓冲是否全黑（首帧黑帧检测用：网格 + 底边采样）。
    bool isBlackFrame() const;
};
