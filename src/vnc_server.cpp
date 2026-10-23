// in-agent VNC Server 实现（libvncserver 胶水）。详见 vnc_server.h。
//
// ⚠️ 本文件依赖 libvncserver 头文件（rfb/rfb.h），仅在 STE_USE_LIBVNCSERVER 开启、
//    且 third_party/libvncserver 已放置预编译库时参与编译。未开启时走文件末尾的占位实现，
//    保证本文件在任何开关下都能独立编译。
//
// ⚠️ API 以 LibVNCServer 标准接口为准，未对照实际 rfb.h 编译过（库到后首编会修正任何
//    签名漂移）。已知要点：rfbMarkRectAsModified 的 (x2,y2) 为右下开区间；
//    帧缓冲由 rfbGetScreen 分配、rfbScreenCleanup 释放（本类不重复 free）。

#include "vnc_server.h"
#include "screen_capture.h"
#include "input_inject.h"

#ifdef STE_USE_LIBVNCSERVER
#include <rfb/rfb.h>
#include <QThread>
#include <QDateTime>
#include <cstring>
#include <atomic>
#include <vector>

// ── VNC 工作线程：抓屏 → 写帧缓冲 → 标记脏区 → rfbProcessEvents 事件循环 ──
// ⚠️ 为什么把"事件循环"也搬进我们自己的 QThread（而不是用库的 rfbRunEventLoop(TRUE)）：
//    backgroundLoop 模式下库会为每个客户端再 spawn 一个 client-to-server 线程，
//    而 win32 threads 构建没有 join —— rfbShutdownServer(TRUE) 会在别的线程上下文里
//    直接 rfbClientConnectionGone 释放客户端，线程还在用 ⇒ UAF，真机两次崩溃均在此。
//    自管线程跑非线程化模式（rfbProcessEvents 自己泵），所有连接/握手/推帧都在**这一个**
//    我们能 wait() 的线程里发生；stop() 先停线程再销毁，零竞态。
// 不继承 QObject（避免 .cpp 内的 moc 依赖）；用 std::atomic 控制退出。
class VncWorkerThread : public QThread {
public:
    VncWorkerThread(rfbScreenInfoPtr s, uint8_t *fb, int w, int h, int grabMs)
        : m_s(s), m_fb(fb), m_w(w), m_h(h), m_grabMs(grabMs) {}
    void run() override {
        ScreenCapture cap;
        // 按 720p 档输出（v1 固定尺寸，起服时定；分辨率变化不动态调整，
        // 仅尺寸一致时推帧——目标尺寸由 VncServer::start 传入并保持不变）。
        if (!cap.init(m_w, m_h))
            qWarning("[vnc] 抓屏线程初始化失败，VNC 无画面（仍服务已有帧缓冲）");
        else
            qInfo("[vnc] 抓屏后端=%s 输出=%dx%d（节奏 %dms）", cap.backend(),
                  cap.width(), cap.height(), m_grabMs);
        qint64 lastGrab = 0;
        // v1 固定尺寸（起服时定），分辨率变化不动态调整，仅尺寸一致时推帧。
        while (!m_stop.load()) {
            // 事件泵：10ms select 超时，处理新连接 / 客户端请求 / 推帧（非线程化模式全在这）。
            rfbProcessEvents(m_s, 10000);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastGrab >= m_grabMs) {   // 抓屏节奏 = 1000/请求fps（clamp 33..1000）
                lastGrab = now;
                if (cap.ok()) {
                    int w = 0, h = 0;
                    const uint8_t *px = cap.grab(w, h);
                    if (px && m_fb && w == m_w && h == m_h) {
                        // ── 逐脏区拷贝 + 逐块 mark（2026-10-19 DXGI 化）──
                        // ⚠️ 不能再整屏 memcpy：DXGI 后端的缓冲只更新了脏区，
                        //    其余部分是上一帧的旧内容，整屏拷会把好数据抹掉。
                        //    GDI 后端 takeDirtyRects 恒给全屏矩形 ⇒ 行为与从前一致。
                        std::vector<GrabRect> rects;
                        cap.takeDirtyRects(rects);
                        int marked = 0;
                        for (const GrabRect &r : rects) {
                            // clamp 到帧缓冲（脏区理论即输出坐标，防御性处理）
                            int x1 = r.x, y1 = r.y, x2 = r.x + r.w, y2 = r.y + r.h;
                            if (x1 < 0) x1 = 0;
                            if (y1 < 0) y1 = 0;
                            if (x2 > m_w) x2 = m_w;
                            if (y2 > m_h) y2 = m_h;
                            if (x1 >= x2 || y1 >= y2) continue;
                            for (int y = y1; y < y2; ++y)
                                std::memcpy(m_fb + (static_cast<size_t>(y) * m_w + x1) * 4,
                                            px + (static_cast<size_t>(y) * m_w + x1) * 4,
                                            static_cast<size_t>(x2 - x1) * 4);
                            // (x2,y2) 为右下开区间（库契约）
                            rfbMarkRectAsModified(m_s, x1, y1, x2, y2);
                            ++marked;
                        }
                        ++m_grabFrames;
                        m_markFrames += marked;
                    }
                }
            }
        }
        qInfo("[vnc] 抓屏线程退出：有效帧 %llu / mark 脏区 %llu 块",
              static_cast<unsigned long long>(m_grabFrames),
              static_cast<unsigned long long>(m_markFrames));
    }
    void requestStop() { m_stop.store(true); }

private:
    rfbScreenInfoPtr m_s;
    uint8_t *m_fb;
    int m_w, m_h;
    int m_grabMs = 100;      // 抓屏间隔（VncServer::start 按请求 fps 换算后传入）
    std::atomic<bool> m_stop{false};
    // 诊断计数（stop 时随线程退出日志打印）：有效帧数 / mark 的脏区块数。
    // 真机验收看这两组数判断 DXGI 脏区路径是否真的在工作。
    unsigned long long m_grabFrames = 0;
    unsigned long long m_markFrames = 0;
};

// ── RFB 输入回调：转发到本机注入 ──
static void kbdCb(rfbBool down, rfbKeySym key, rfbClientPtr) {
    InputInject::keyEvent(down != 0, static_cast<uint32_t>(key));
}
static void ptrCb(int buttonMask, int x, int y, rfbClientPtr) {
    InputInject::mouseEvent(static_cast<uint32_t>(buttonMask), x, y);
}

quint16 VncServer::start(quint16 port, int fps) {
    if (m_running) return m_port;

    // 抓屏节奏：按请求帧率换算（2026-10-18 晚修复——原来写死 100ms=10fps，
    // 客户端要 24fps 也永远只有 10fps）。下限 33ms≈30fps（再高抓屏+编码
    // CPU 成本超过收益），fps<=0 兜底 10fps。
    m_grabMs = (fps > 0) ? qBound(33, 1000 / fps, 1000) : 100;

    ScreenCapture probe;
    if (!probe.init()) {
        qWarning("[vnc] ScreenCapture 初始化失败，无法起 VNC");
        return 0;
    }
    const int nw = probe.width(), nh = probe.height();
    if (nw <= 0 || nh <= 0) return 0;

    // ── 输出分辨率：480p 档（2026-10-24 下调，原 720p）────────────────────
    // 原有 720p 是为"Raw 增量 40-400KB/批 灌爆 250KB/s 隧道"设的。实测链路
    // （CF 隧道）仍是限流大头，远控要在几 Mbps 里跑出可交互帧率，只能压像素量：
    // 720p → 480p 像素降 56%，单帧接近腰斩。
    // 可用 STE_VNC_MAX_EDGE 覆盖（默认 854 = 480p 档；0 或负数 = 不缩放原样输出）。
    // 内接保宽高比、**不放大**（屏幕本来就小就按原样）。
    int tw = nw, th = nh;
    const int maxEdge = [] {
        int v = qEnvironmentVariableIntValue("STE_VNC_MAX_EDGE");
        if (v == 0 && qEnvironmentVariableIsSet("STE_VNC_MAX_EDGE")) return 0;  // 显式 0 = 不缩放
        if (v <= 0) v = 854;
        return qBound(320, v, 3840);
    }();
    if (maxEdge > 0 && qMax(nw, nh) > maxEdge) {
        const double s = double(maxEdge) / qMax(nw, nh);
        tw = static_cast<int>(nw * s) & ~1;
        th = static_cast<int>(nh * s) & ~1;
    }
    if (tw <= 0 || th <= 0) { tw = nw; th = nh; }
    const int w = tw, h = th;
    if (w != nw || h != nh)
        qInfo("[vnc] 抓屏按 %dx%d 输出（原生 %dx%d，缩到 %dp 档省带宽）", w, h, nw, nh, maxEdge);

    int argc = 0;
    char **argv = nullptr;
    // ⚠️ 第二参是 char**（直接传 argv），不是 &argv —— 传 &argv 是 char***，C2664。
    rfbScreenInfoPtr server = rfbGetScreen(&argc, argv, w, h, 8, 3, 4);
    if (!server) {
        qWarning("[vnc] rfbGetScreen 失败");
        return 0;
    }

    // ⚠️ 实测（库级探针验证）：rfbGetScreen **不分配 frameBuffer**（返回 NULL）！
    //    必须自己 malloc，再经 rfbNewFramebuffer 交给库；rfbScreenCleanup 会 free 它，
    //    本类不能再手动释放。
    //    ⚠️ 顺序不能反：rfbNewFramebuffer 内部会 rfbInitServerFormat 覆盖 serverFormat，
    //    所以 BGRX 字段必须放在这一步**之后**设置。
    uint8_t *fb = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(w) * h * 4));
    if (!fb) {
        qWarning("[vnc] 帧缓冲分配失败（%dx%d）", w, h);
        rfbScreenCleanup(server);
        return 0;
    }
    // 客户端若在抓屏首帧交付前就连上（非增量全屏请求），让它看到黑而不是
    // malloc 垃圾——DXGI 首帧黑帧跳过期间（实测最长约 1-2s）的兜底观感。
    std::memset(fb, 0, static_cast<size_t>(w) * h * 4);
    rfbNewFramebuffer(server, reinterpret_cast<char *>(fb), w, h, 8, 3, 4);
    m_fb = fb;

    // 像素格式：BGRX（蓝在字节 0），与 ScreenCapture 输出一致。
    server->serverFormat.bitsPerPixel = 32;
    server->serverFormat.depth = 24;
    server->serverFormat.bigEndian = FALSE;
    server->serverFormat.trueColour = TRUE;
    server->serverFormat.redMax = 255;
    server->serverFormat.greenMax = 255;
    server->serverFormat.blueMax = 255;
    server->serverFormat.redShift = 16;
    server->serverFormat.greenShift = 8;
    server->serverFormat.blueShift = 0;

    // 帧缓冲所有权：我们 malloc 的 fb 已交给库（rfbNewFramebuffer），
    // rfbScreenCleanup 负责释放，本类只持有指针不 free。
    server->kbdAddEvent = kbdCb;
    server->ptrAddEvent = ptrCb;
    server->deferUpdateTime = 0;

    // 安全边界：只绑 127.0.0.1（loopback）。
    // ⚠️ listenInterface 的类型是 `in_addr_t`（裸 32 位网络序地址），**不是** sockaddr_in —
    //    写 `listenInterface.s_addr = ...` 直接编不过。
    server->listenInterface = htonl(INADDR_LOOPBACK);
    if (port != 0)
        server->port = port;
    else
        server->autoPort = TRUE; // 系统选空闲端口

    rfbInitServer(server);
    const quint16 actual = static_cast<quint16>(server->port);

    m_thread = new VncWorkerThread(server, m_fb, w, h, m_grabMs);
    static_cast<VncWorkerThread *>(m_thread)->start();
    // 不调 rfbRunEventLoop：事件循环已并入 VncWorkerThread（见类注释——
    // backgroundLoop 的 win32 线程无 join，stop 时会 UAF，真机两次崩溃实证）。

    m_screen = server;
    m_fbW = w;
    m_fbH = h;
    m_port = actual;
    m_running = true;
    qInfo("[vnc] RFB 服务已起，监听 127.0.0.1:%d（只绑 loopback）", actual);
    return actual;
}

void VncServer::stop() {
    if (!m_running) return;
    if (m_thread) {
        static_cast<VncWorkerThread *>(m_thread)->requestStop();
        static_cast<VncWorkerThread *>(m_thread)->wait();  // 事件循环/抓屏全停，无库线程残留
        delete static_cast<VncWorkerThread *>(m_thread);
        m_thread = nullptr;
    }
    rfbScreenInfoPtr server = reinterpret_cast<rfbScreenInfoPtr>(m_screen);
    if (server) {
        // 线程已退出 ⇒ 非线程化模式下没有任何库线程在用 server，此刻销毁安全
        // （TRUE：顺手断开仍连着的客户端；都在本线程内联完成）。
        rfbShutdownServer(server, TRUE);
        rfbScreenCleanup(server); // 释放 server 与其 frameBuffer
    }
    m_fb = nullptr;
    m_screen = nullptr;
    m_running = false;
    m_port = 0;
    qInfo("[vnc] RFB 服务已停止");
}

#else // ── STE_USE_LIBVNCSERVER 关闭：占位（不链接 libvncserver）──
quint16 VncServer::start(quint16) { return 0; }
void VncServer::stop() {}
#endif

// ── 以下两函数在两种开关下都定义（实例/析构，跨分支安全）──
VncServer::VncServer() = default;
VncServer::~VncServer() {
    if (m_running) stop();
}

VncServer &VncServer::instance() {
    static VncServer s;
    return s;
}
