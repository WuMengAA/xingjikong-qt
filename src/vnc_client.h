#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 远控 · 管理端 VNC 客户端（libvncclient · RFB 003.008 over WebSocket）
//
// 设计要点（对应开发约束）：
//  • 底层强制 libvncclient（rfb/rfbclient.h）。手写 RFB 协议解析（旧 rfb_client）已废弃。
//  • 网络**不**直连 5900 / 公网：libvncclient 只连**本进程内的回环端口**（127.0.0.1:0
//    上的 QTcpServer，起在本机、外部不可达），另一端由本类把字节与主 WS 的
//    vnc-data 帧互相搬运：
//        云端 vnc-data → feedIncoming() → 回环 socket → libvncclient 读
//        libvncclient 写 → 回环 socket → readyRead → vncOut → 包 vnc-data 帧发云端
//    ⚠️ 为什么不用「读写 hook」：upstream libvncclient 没有公开的 read/write hook API
//    （只有 TurboVNC 分支提供）；靠假设写 hook 会一编就炸。回环 socket 只用公开字段
//    （serverHost / serverPort）就能把库的阻塞式 IO 接进异步 WS，且不引入任何私有头。
//  • 线程：rfbInitClient / rfbProcessEvents / Send*Event 全部在专属 QThread 里跑，
//    主线程只碰 Qt socket 与事件队列（禁主线程死循环读）。
//  • 帧缓冲回调把像素拷成 QImage 经 frameReady 抛给 QML（QQuickImageProvider，禁 QWidget）。
//
// 编译门控：rfb/rfbclient.h 仅在 STE_USE_LIBVNCCLIENT 开启时引入；关闭时本类为安全桩，
// 整个工程仍可干净编译（不依赖预编译 libvncclient）。
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QMutex>
#include <QThread>
#include <QSize>
#include <QList>
#include <QAtomicInt>

class QTcpServer;
class QTcpSocket;

// 前向声明，避免头文件污染（rfb/rfbclient.h 只在 .cpp 内按需 include）
// ⚠️ 必须用库的真实 tag：rfbclient.h 里是 `typedef struct _rfbClient {...} rfbClient;`。
//    写成 `struct rfbClient;` 会造出另一个类型，与库里的 typedef 撞成 C2371「重定义、
//    不同基类型」，且之后所有解引用都报「使用未定义类型」。
struct _rfbClient;
typedef struct _rfbClient rfbClient;

// 鼠标/键盘事件（主线程入队，工作线程抽空经 libvncclient 发送，避免跨线程调非重入 API）
struct VncOutEvt {
    enum Kind { Ptr, Key } kind;
    double nx = 0, ny = 0;
    quint8 mask = 0;
    int vk = 0;
    bool down = false;
};

class VncClient : public QObject
{
    Q_OBJECT
public:
    enum State { Idle = 0, Connecting = 1, Connected = 2, Error = 3 };

    explicit VncClient(QObject *parent = nullptr);
    ~VncClient() override;

    /// 启动：本机回环监听 + 解码线程。password 用于 VNC 标准 DES 认证。
    /// 不发起任何对公网/5900 的连接；RFB 字节通过 feedIncoming / vncOut 与 WS 互通。
    void start(const QString &password);
    void stop();
    bool isActive() const { return m_active.loadRelaxed() != 0; }
    QSize frameSize() const { return m_fb; }

public slots:
    /// 云端下发的 vnc-data 原始 RFB 字节 → 写进回环 socket（线程安全，主线程调用）。
    void feedIncoming(const QByteArray &data);
    /// 老师端鼠标：nx,ny ∈ [0,1]（画面内归一化绝对坐标）；mask 按钮位（1=左 2=右 4=中）。
    void sendPointer(double nx, double ny, quint8 mask);
    /// 老师端键盘：VK（Windows 虚拟键码）→ 内部转 X11 keysym 发 RFB KeyEvent。
    void sendKeyVk(int vk, bool down);

signals:
    void frameReady(const QImage &img);   // 解码出的一帧完整画面
    void stateChanged(int st);            // 见 State 枚举
    void vncOut(const QByteArray &data);  // 待发 RFB 字节（主线程包 vnc-data 帧发云端）

private slots:
    void onLocalNewConnection();
    void onLocalReadyRead();

private:
    void runLoop();                 // 工作线程体（libvncclient 主循环）
    void flushPendingInput();       // 连接建好后补写早到的入站字节
    // 帧缓冲更新回调（libvncclient 解出一帧后调用）
    static void updateScreenTrampoline(rfbClient *cl, int x, int y, int w, int h);
    // ── 渲染三件套（文档要求 all-or-none）──────────────────────────────
    // 实测：libvncclient 的 Raw/ultra 等位图编码**都**经 GotBitmap 回调把位图交给应用，
    // 库**不会**自动写 frameBuffer —— 数据落屏是应用责任（默认实现是库里的 CopyRectangle，
    // 行为相同；这里显式接管，语义更明确）。
    static void bitmapTrampoline(rfbClient *cl, const uint8_t *buf, int x, int y, int w, int h);
    static void fillRectTrampoline(rfbClient *cl, int x, int y, int w, int h, uint32_t colour);
    static void copyRectTrampoline(rfbClient *cl, int sx, int sy, int w, int h, int dx, int dy);

    QTcpServer *m_srv = nullptr;     // 127.0.0.1:0 回环监听（仅本机）
    QTcpSocket *m_sock = nullptr;    // 被 libvncclient 连接的那一端
    QThread *m_thread = nullptr;

    QMutex m_inMutex;
    QByteArray m_pendingIn;          // socket 就绪前早到的字节

    QMutex m_outMutex;
    QList<VncOutEvt> m_evts;         // 待发的鼠标/键盘事件（工作线程抽空）

    rfbClient *m_cl = nullptr;       // libvncclient 句柄（无开关时为 nullptr）
    QAtomicInt m_active{0};
    QAtomicInt m_stopReq{0};
    quint16 m_localPort = 0;
    QString m_password;

    QSize m_fb;                      // 帧缓冲尺寸（ServerInit 后填）

    int m_dbgOutLogged = 0;          // 出站日志计数（只打头几帧，诊断用）
    int m_dbgInLogged = 0;           // 入站日志计数（只打头几帧，诊断用）
    qint64 m_lastKeepaliveMs = 0;    // 修复③：上次管理端保活时刻（每 5s 一个 1x1 增量请求）

#ifdef STE_USE_LIBVNCCLIENT
    // 仅开开关时编译的 libvncclient 私有逻辑
    bool setupClient();              // 建 rfbClient 并连本机回环端口
    void drainEvents();              // 把 m_evts 里的键鼠事件交给 libvncclient
    void onFramebufferUpdate(int x, int y, int w, int h); // 拷像素 → QImage → frameReady
    static quint32 vkToKeysym(int vk); // VK → X11 keysym（RFB 键码用 keysym）
    // VNC 标准认证（security type 2）密码回调：libvncclient 没有 rfbClientSetPassword，
    // 只能挂 client->GetPassword；返回的 char* 由库 free（实现里 strdup）。
    static char *passwordTrampoline(rfbClient *cl);
#endif
};
