#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// in-agent VNC Server（libvncserver 封装，单例）
//
// 方案：本类在 127.0.0.1:<port> 起一个 RFB 服务（只绑 loopback，公网不可达）。
//   · 帧缓冲由 ScreenCapture 抓屏填充（BGRX），按节奏标记脏区推送。
//   · 指针/键盘事件经 InputInject 注入本机。
//   · VncBridge 连 127.0.0.1:<同一个 port>，把 RFB 字节打成主 WS 的 vnc-data 帧
//     （→ 云端按 uid 转发 → 老师端 libvncclient）。本类不碰 WS、不碰网络。
//
// 安全边界（写死）：只绑 127.0.0.1；云端永远拿不到真实 host:port（目标地址由本机决定）。
//
// 头文件不 include rfb.h（用 void* 持有 rfbScreenInfoPtr），所以即便未提供 libvncserver、
// 未开 STE_USE_LIBVNCSERVER 开关，本头也能被 main.cpp 安全 include。
// ─────────────────────────────────────────────────────────────────────────────
#include <QtGlobal>   // quint16（本头不依赖其它 Qt 头，但必须给类型来源）
#include <cstdint>

class VncServer {
public:
    static VncServer &instance();

    /// 在 127.0.0.1:port 起 RFB 服务。port=0 让系统选空闲端口。
    /// fps>0 时抓屏节奏按该帧率走（下限 33ms≈30fps；0=默认 10fps 兜底）。
    /// @return 实际监听端口(>0) 成功；0 失败。
    quint16 start(quint16 port = 0, int fps = 0);

    /// 停服务并释放资源。
    void stop();

    bool isRunning() const { return m_running; }
    quint16 port() const { return m_port; }

private:
    VncServer();
    ~VncServer();
    VncServer(const VncServer &) = delete;
    VncServer &operator=(const VncServer &) = delete;

    bool m_running = false;
    quint16 m_port = 0;
    void *m_screen = nullptr;     // rfbScreenInfoPtr（不在此 include rfb.h）
    void *m_thread = nullptr;     // CaptureThread*（见 .cpp）
    uint8_t *m_fb = nullptr;      // 帧缓冲（BGRX）
    int m_fbW = 0, m_fbH = 0;
    int m_grabMs = 100;          // 抓屏间隔（按请求 fps 换算，clamp 33..1000）
};
