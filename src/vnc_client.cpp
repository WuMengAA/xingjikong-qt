// 星集控 · 管理端 VNC 客户端实现（libvncclient 封装）
// 见 vnc_client.h 顶部长注释：libvncclient 只连**本进程回环端口**，字节由 Qt socket
// 与主 WS 的 vnc-data 帧互搬 —— 不直连 5900、不碰公网、不依赖私有 hook API。
#include "vnc_client.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QMutexLocker>
#include <QMetaObject>
#include <QDateTime>
#include <cstring>
#include <cstdint>

// MSVC 把 POSIX 名标成弃用（C4996），直接写 strdup 会给每个调用点刷一条警告；
// 本工程口径是 0 warning，所以这里显式走安全名。
#ifdef _MSC_VER
#  define steStrDup _strdup
#else
#  define steStrDup strdup
#endif

// logf 由 viewerbackend.cpp 提供（落到 viewer-run.log），此处前向声明避免引入庞大 header
void logf(const char *fmt, ...);

#ifdef STE_USE_LIBVNCCLIENT
#include <rfb/rfbclient.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────
VncClient::VncClient(QObject *parent) : QObject(parent)
{
}

VncClient::~VncClient()
{
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// 启动 / 停止
// ─────────────────────────────────────────────────────────────────────────────
void VncClient::start(const QString &password)
{
    if (m_active.loadRelaxed()) return;
    m_password = password;
    m_stopReq.storeRelaxed(0);
    m_lastKeepaliveMs = 0;   // 修复③：连上后立即发第一个保活，尽早建立"管理端还在"的证据
    {
        QMutexLocker l(&m_inMutex);
        m_pendingIn.clear();
    }
    {
        QMutexLocker l(&m_outMutex);
        m_evts.clear();
    }

    // ① 本机回环监听：端口由系统分配（127.0.0.1:0），外部不可达。
    //    这就是「禁止直连 5900」的落地方式：libvncclient 连的是本进程自己。
    if (!m_srv) {
        m_srv = new QTcpServer(this);
        connect(m_srv, &QTcpServer::newConnection, this, &VncClient::onLocalNewConnection);
    }
    if (!m_srv->isListening()) {
        if (!m_srv->listen(QHostAddress::LocalHost, 0)) {
            logf("[viewer-vnc] FAIL 回环监听失败：%s",
                 qPrintable(m_srv->errorString()));
            emit stateChanged(Error);
            return;
        }
    }
    m_localPort = m_srv->serverPort();

    m_active.storeRelaxed(1);
    emit stateChanged(Connecting);

    // ② 解码线程：连回环端口 + 跑 rfbProcessEvents
    m_thread = QThread::create([this]() { runLoop(); });
    m_thread->setObjectName(QStringLiteral("vnc-client"));
    m_thread->start();
}

void VncClient::stop()
{
    if (!m_active.loadRelaxed() && !m_thread) return;
    m_stopReq.storeRelaxed(1);
    // ⚠️ 顺序不能反：工作线程此刻可能正阻塞在 libvncclient 的 read()（等对端字节）上，
    //    `quit()`/`wait()` 根本唤不醒它 —— 必须先 abort 回环 socket，read 立刻返回错误，
    //    rfbInitClient/HandleRFBServerMessage 才会退出。反序就只能 terminate 线程，
    //    而 terminate 在 C 库里打断 malloc 会留下坏堆。
    if (m_sock) { m_sock->disconnect(this); m_sock->abort(); m_sock->deleteLater(); m_sock = nullptr; }
    if (m_thread) {
        if (!m_thread->wait(3000)) {
            logf("[viewer-vnc] ⚠️ 解码线程 3s 未退出，强制 terminate");
            m_thread->terminate();
            m_thread->wait(1000);
        }
        delete m_thread;
        m_thread = nullptr;
    }
    if (m_srv && m_srv->isListening()) m_srv->close();
    m_active.storeRelaxed(0);
    emit stateChanged(Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// 回环 socket（主线程）：入站写、出站读
// ─────────────────────────────────────────────────────────────────────────────
void VncClient::onLocalNewConnection()
{
    while (m_srv && m_srv->hasPendingConnections()) {
        QTcpSocket *s = m_srv->nextPendingConnection();
        if (m_sock) { s->abort(); s->deleteLater(); continue; }   // 只认一条（本进程自连）
        m_sock = s;
        connect(m_sock, &QTcpSocket::readyRead, this, &VncClient::onLocalReadyRead);
        connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
            logf("[viewer-vnc] 回环链路断开");
            if (m_active.loadRelaxed()) emit stateChanged(Error);
        });
        logf("[viewer-vnc] libvncclient 已连上本机回环端口 %u", (unsigned)m_localPort);
        flushPendingInput();
    }
}

void VncClient::onLocalReadyRead()
{
    if (!m_sock) return;
    const QByteArray data = m_sock->readAll();
    if (!data.isEmpty()) {
        // 诊断（2026-10-18 深夜排障）：出站方向此前完全黑盒 —— libvncclient 到底
        // 有没有把 RFB 字节（客户端 ProtocolVersion 等）写出来，只能靠猜。
        if (m_dbgOutLogged < 4)
            logf("[viewer-vnc] 回环出站 #%d：%d 字节", ++m_dbgOutLogged, data.size());
        emit vncOut(data);   // → viewerbackend 包 vnc-data 帧发云端
    }
}

void VncClient::flushPendingInput()
{
    if (!m_sock) return;
    QByteArray pending;
    {
        QMutexLocker l(&m_inMutex);
        pending.swap(m_pendingIn);
    }
    if (!pending.isEmpty()) m_sock->write(pending);
}

// ─────────────────────────────────────────────────────────────────────────────
// 背压保护（2026-10-18 · 按 vnc-data 架构规格）：WS 面向消息、RFB 面向流，
// 两端之间所有"缓冲"必须有上限——否则对端解码慢时内存无限涨、直接 OOM。
//  · 入站（云端 → libvncclient）：QTcpSocket 写缓冲阈值，超限主动断开（宁可
//    重连也不占内存）；socket 未就绪前的早到字节也有硬上限。
//  ─────────────────────────────────────────────────────────────────────────────
static constexpr qint64 kMaxSocketBacklog = 4 * 1024 * 1024;  // 回环 socket 写积压 4MB
static constexpr int    kMaxPendingIn   = 8 * 1024 * 1024;    // socket 未就绪缓冲 8MB

void VncClient::feedIncoming(const QByteArray &data)
{
    if (data.isEmpty()) return;
    if (!m_sock) {
        // socket 还没建（握手第一字节可能比 accept 先到）：先攒着，连上即补写。
        QMutexLocker l(&m_inMutex);
        if (m_pendingIn.size() + data.size() > kMaxPendingIn) {
            logf("[viewer-vnc] ⚠️ 入站缓冲超上限（%d 字节），丢弃早到数据防止内存膨胀",
                 m_pendingIn.size());
            m_pendingIn.clear();
        }
        m_pendingIn.append(data);
        return;
    }
    // 对端（libvncclient）读得慢时，Qt 写缓冲会无限涨 —— 超阈值主动断开，
    // 触发重连/用户重试，绝不无限制占内存。
    if (m_sock->bytesToWrite() > kMaxSocketBacklog) {
        logf("[viewer-vnc] ⚠️ 回环写积压超 %d MB，判定解码跟不上，断开连接",
             (int)(kMaxSocketBacklog / 1024 / 1024));
        emit stateChanged(Error);
        m_sock->abort();
        return;
    }
    m_sock->write(data);
    if (m_dbgInLogged < 4)
        logf("[viewer-vnc] 回环入站 #%d：%d 字节（云端→本机）", ++m_dbgInLogged, data.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// 出站（老师鼠标/键盘 → RFB 事件）。主线程入队，工作线程抽空发送（线程安全）。
// ─────────────────────────────────────────────────────────────────────────────
void VncClient::sendPointer(double nx, double ny, quint8 mask)
{
    if (!m_active.loadRelaxed()) return;
    QMutexLocker l(&m_outMutex);
    VncOutEvt e;
    e.kind = VncOutEvt::Ptr;
    e.nx = nx; e.ny = ny; e.mask = mask;
    m_evts.append(e);
}

void VncClient::sendKeyVk(int vk, bool down)
{
    if (!m_active.loadRelaxed()) return;
    QMutexLocker l(&m_outMutex);
    VncOutEvt e;
    e.kind = VncOutEvt::Key;
    e.vk = vk; e.down = down;
    m_evts.append(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// 工作线程体
// ─────────────────────────────────────────────────────────────────────────────
void VncClient::runLoop()
{
#ifdef STE_USE_LIBVNCCLIENT
    if (!setupClient()) return;
    // ⚠️ 客户端主循环用 WaitForMessage + HandleRFBServerMessage：
    //    `rfbProcessEvents` 是**服务端** API（rfb.h，签名收 rfbScreenInfoPtr），
    //    客户端根本没有这个符号 —— 照抄服务端写法会链接失败。
    //    WaitForMessage 内部 select 在 client->sock（= 本机回环）上，最多等 usecs 微秒，
    //    超时返回 0 且不报错；只有 HandleRFBServerMessage 返回 FALSE 才是真断链。
    //    数据由主线程 feedIncoming 写入回环 socket，因此这里既不阻塞主线程也不需要 sleep。
    while (!m_stopReq.loadRelaxed()) {
        drainEvents();
        // 修复③（2026-10-19）：管理端保活 —— 每 5s 经 RFB 发一个 1x1 **增量**
        // FramebufferUpdateRequest。语义无害（增量=只有该像素真变了服务端才回），
        // 但字节会经 vnc-data down 到达被控端，让它的泄漏看门狗知道"管理端还在"。
        // 没有它，老师纯观看（不碰键鼠）时被控端侧零 down 流量，"在看"与
        // "已离开"无法区分 —— 管理端直接离开不发 stop 就成了无声泄漏
        // （灵动岛冻结 + VNC server 空转，只能重启被控端收口）。
        if (m_cl) {
            const qint64 nowKl = QDateTime::currentMSecsSinceEpoch();
            if (nowKl - m_lastKeepaliveMs >= 5000) {
                m_lastKeepaliveMs = nowKl;
                // ⚠️ 签名：(client, x, y, w, h, incremental)——矩形参数在前、
                //    incremental 在末位（rfbclient.h:573）。顺序写反不会编译报错
                //    （全是整型），但会把 TRUE 当 x、0 当 y、请求一个 0 宽矩形。
                SendFramebufferUpdateRequest(m_cl, 0, 0, 1, 1, TRUE);
            }
        }
        const int ready = WaitForMessage(m_cl, 100000);   // 100ms
        if (ready < 0) {
            if (!m_stopReq.loadRelaxed()) {
                logf("[viewer-vnc] WaitForMessage 出错，停止解码");
                emit stateChanged(Error);
            }
            break;
        }
        if (ready == 0) continue;                          // 超时：无新数据，继续等
        if (!HandleRFBServerMessage(m_cl)) {
            if (!m_stopReq.loadRelaxed()) {
                logf("[viewer-vnc] HandleRFBServerMessage 失败（对端断开或协议错误）");
                emit stateChanged(Error);
            }
            break;
        }
    }
    // rfbClientCleanup 会释放 client 内部所有资源**并 free(client) 本身**，
    // 之后指针即失效，必须立刻置空（本类不持有它）。
    if (m_cl) {
        rfbClientCleanup(m_cl);
        m_cl = nullptr;
    }
#else
    // 未编译 libvncclient：安全桩，仅占位（工程仍能干净编译、出包）。
    logf("[viewer-vnc] libvncclient 未编译（STE_USE_LIBVNCCLIENT=OFF）—— 远控画面不可用");
    emit stateChanged(Error);
#endif
}

#ifdef STE_USE_LIBVNCCLIENT

// rfbClientSetClientData/GetClientData 的 tag：只要求是稳定地址，不必有内容。
static char kSelfTag = 0;

bool VncClient::setupClient()
{
    // 8/3/4 只是库内部初始缓冲参数；最终像素格式由下面的 format 决定。
    m_cl = rfbGetClient(8, 3, 4);
    if (!m_cl) {
        logf("[viewer-vnc] rfbGetClient 失败");
        emit stateChanged(Error);
        return false;
    }

    // ⚠️ clientData 是 `rfbClientData*` 链表（库内部扩展用），**不能**直接塞 this；
    //    必须走 rfbClientSetClientData(client, tag, data)。tag 只要求是稳定地址，
    //    不必是字符串内容 —— 用本文件内一个静态字节的地址即可。
    rfbClientSetClientData(m_cl, &kSelfTag, this);

    // 只连本进程回环端口 —— 没有任何对公网 / 5900 的连接。
    // 注意 serverHost 必须 malloc 出来：rfbClientCleanup 会 free() 它。
    m_cl->serverHost = steStrDup("127.0.0.1");
    m_cl->serverPort = m_localPort;

    // 像素格式：32bpp，小端，真彩，redShift=16 / green=8 / blue=0
    // ⇒ frameBuffer 内存布局为 [B,G,R,X]，与 QImage::Format_RGBX8888 逐字节一致。
    // （画面花屏时先查这里与对端 SetPixelFormat 是否真的按这个来。）
    m_cl->format.bitsPerPixel = 32;
    m_cl->format.depth = 24;
    m_cl->format.bigEndian = 0;
    m_cl->format.trueColour = 1;
    m_cl->format.redMax = 255;
    m_cl->format.greenMax = 255;
    m_cl->format.blueMax = 255;
    m_cl->format.redShift = 16;
    m_cl->format.greenShift = 8;
    m_cl->format.blueShift = 0;

    // VNC 标准认证（security type 2）：libvncclient **没有** rfbClientSetPassword()，
    // 正确做法是挂 GetPassword 回调 —— 回调返回的 char* 由库负责 free（要 strdup）。
    // 被控端当前默认无密码（None 认证，WS 层已有 token 鉴权）；仅当环境变量
    // STE_VNC_PASSWORD 非空时才需要这条链路。
    if (!m_password.isEmpty())
        m_cl->GetPassword = &VncClient::passwordTrampoline;

    // 编码策略（2026-10-24 第二次重构：Tight 优先）：
    //   · tight：**有损** JPEG 分块 + zlib 差分。带宽是硬约束（CF 隧道 + 教室上行
    //           合计只有几 Mbps），ZRLE 无损压缩对高熵画面只能压 3-4 倍，撑不起帧率；
    //           Tight 能把单帧再降一个数量级。代价是文字轻微发软（q≈6/9）。
    //   · copyrect：块移动（滚动/拖窗）零字节；
    //   · zrle：Tight 不可用时的无损回退（zlib）；
    //   · hextile：再回退；
    //   · raw：兜底（库内部恒支持）。
    //   ⚠️ 服务端取"客户端 SetEncodings 里第一个它支持的编码"为 preferredEncoding
    //     （见 rfbserver.c 的 SetEncodings 分支），所以 tight 必须排第一。
    //   ⚠️ Tight 依赖两端库带 libjpeg 构建（LIBVNCSERVER_HAVE_LIBJPEG）；换库后才有，
    //     旧的 no-jpeg 预编译库下此串会静默退化到 zrle/hextile。
    m_cl->appData.encodingsString = "tight copyrect zrle hextile raw";
    // Tight 质量等级（0..9，库默认 5）：6 在"文字可读 / 带宽"之间取平衡。
    // 走 SetEncodings 的 QualityLevel 伪编码下发给服务端。
    m_cl->appData.qualityLevel = 6;

    // 帧缓冲更新回调：解出一帧 → 拷成 QImage → frameReady
    m_cl->GotFrameBufferUpdate = &VncClient::updateScreenTrampoline;
    // ⚠️ 渲染三件套必须设（文档「all or none」）：位图数据经 GotBitmap 交给我们，
    //    库不会自动写 frameBuffer。缺了它，Raw 帧的像素永远不落缓冲、画面恒黑。
    m_cl->GotBitmap   = &VncClient::bitmapTrampoline;
    m_cl->GotFillRect = &VncClient::fillRectTrampoline;
    m_cl->GotCopyRect = &VncClient::copyRectTrampoline;

    // 连接 + 握手都在本线程完成；字节从回环 socket 来（主线程泵入）。
    if (!rfbInitClient(m_cl, nullptr, nullptr)) {
        // ⚠️ rfbInitClient 失败时**它自己已经调过 rfbClientCleanup**（内部 free(client)），
        //    这里再 cleanup 就是 double free，只能把指针丢掉。
        logf("[viewer-vnc] rfbInitClient 失败（连接或握手未成）");
        m_cl = nullptr;
        emit stateChanged(Error);
        return false;
    }

    m_fb = QSize(m_cl->width, m_cl->height);
    emit stateChanged(Connected);
    logf("[viewer-vnc] 握手完成：%dx%d，像素格式 32bpp RGBX",
         m_cl->width, m_cl->height);
    return true;
}

void VncClient::drainEvents()
{
    QList<VncOutEvt> evts;
    {
        QMutexLocker l(&m_outMutex);
        if (m_evts.isEmpty()) return;
        evts = m_evts;
        m_evts.clear();
    }
    if (!m_cl) return;
    for (const VncOutEvt &e : evts) {
        if (e.kind == VncOutEvt::Ptr) {
            const int x = m_fb.width() > 0 ? (int)(e.nx * (m_fb.width() - 1)) : 0;
            const int y = m_fb.height() > 0 ? (int)(e.ny * (m_fb.height() - 1)) : 0;
            SendPointerEvent(m_cl, x, y, e.mask);
        } else {
            const quint32 ks = vkToKeysym(e.vk);
            // ⚠️ 签名是 SendKeyEvent(client, key, down) —— 键码在前、按下标志在后，
            //    顺序写反编译不过（uint32_t 与 rfbBool 都是整型，别指望编译器提醒）。
            if (ks) SendKeyEvent(m_cl, ks, e.down ? TRUE : FALSE);
        }
    }
}

void VncClient::updateScreenTrampoline(rfbClient *cl, int x, int y, int w, int h)
{
    auto *self = static_cast<VncClient *>(rfbClientGetClientData(cl, &kSelfTag));
    if (self) self->onFramebufferUpdate(x, y, w, h);
}

// ── 渲染三件套实现（与库默认 CopyRectangle 同语义：把位图写进 frameBuffer）──
void VncClient::bitmapTrampoline(rfbClient *cl, const uint8_t *buf, int x, int y, int w, int h)
{
    if (!cl->frameBuffer || !buf || w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > cl->width || y + h > cl->height) return;
    const size_t stride = (size_t)cl->width * 4;
    for (int row = 0; row < h; row++)
        memcpy(cl->frameBuffer + (size_t)(y + row) * stride + (size_t)x * 4,
               buf + (size_t)row * (size_t)w * 4, (size_t)w * 4);
}

void VncClient::fillRectTrampoline(rfbClient *cl, int x, int y, int w, int h, uint32_t colour)
{
    if (!cl->frameBuffer || w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > cl->width || y + h > cl->height) return;
    /* 32bpp little-endian 下 colour 即 0x00RRGGBB，内存布局 = B,G,R,X —— 直接整字填 */
    uint32_t *dst = (uint32_t *)cl->frameBuffer + (size_t)y * cl->width + x;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) dst[col] = colour;
        dst += cl->width;
    }
}

void VncClient::copyRectTrampoline(rfbClient *cl, int sx, int sy, int w, int h, int dx, int dy)
{
    if (!cl->frameBuffer || w <= 0 || h <= 0) return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0) return;
    if (sx + w > cl->width || sy + h > cl->height) return;
    if (dx + w > cl->width || dy + h > cl->height) return;
    const size_t stride = (size_t)cl->width * 4;
    for (int row = 0; row < h; row++)
        memmove(cl->frameBuffer + (size_t)(dy + row) * stride + (size_t)dx * 4,
                cl->frameBuffer + (size_t)(sy + row) * stride + (size_t)sx * 4,
                (size_t)w * 4);
}

// VNC 标准认证回调：库在收到 challenge 后调它要密码，返回的串由库 free。
char *VncClient::passwordTrampoline(rfbClient *cl)
{
    auto *self = static_cast<VncClient *>(rfbClientGetClientData(cl, &kSelfTag));
    if (!self || self->m_password.isEmpty()) return nullptr;
    return steStrDup(self->m_password.toUtf8().constData());
}

void VncClient::onFramebufferUpdate(int x, int y, int w, int h)
{
    Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(w); Q_UNUSED(h);
    if (!m_cl || !m_cl->frameBuffer) return;
    const int fw = m_cl->width, fh = m_cl->height;
    if (fw <= 0 || fh <= 0) return;
    m_fb = QSize(fw, fh);
    // frameBuffer 为 fw*fh*4 连续内存，布局 [B,G,R,X]，与 Format_RGBX8888 逐字节对齐
    // ⇒ 直接 wrap + copy 上屏（零逐像素转换）。
    QImage img(reinterpret_cast<uchar *>(m_cl->frameBuffer), fw, fh,
               fw * 4, QImage::Format_RGBX8888);
    emit frameReady(img.copy());
}

// VK → X11 keysym（RFB 键码用 keysym，不是 Windows VK）。未见键返回 0。
quint32 VncClient::vkToKeysym(int vk)
{
    // 字母 A-Z（VK 0x41..0x5A → keysym 0x61..0x7A，小写）
    if (vk >= 0x41 && vk <= 0x5A) return (quint32)(vk - 0x41 + 0x61);
    // 数字 0-9（VK 0x30..0x39 → keysym 0x30..0x39）
    if (vk >= 0x30 && vk <= 0x39) return (quint32)vk;
    switch (vk) {
    case 0x0D: return 0xFF0D; // Enter
    case 0x08: return 0xFF08; // Backspace
    case 0x09: return 0xFF09; // Tab
    case 0x1B: return 0xFF1B; // Esc
    case 0x20: return 0x20;   // Space
    case 0x25: return 0xFF51; // Left
    case 0x26: return 0xFF52; // Up
    case 0x27: return 0xFF53; // Right
    case 0x28: return 0xFF54; // Down
    case 0x2E: return 0xFFFF; // Delete（XK_Delete = 0xFFFF）
    case 0x24: return 0xFF50; // Home
    case 0x23: return 0xFF57; // End
    case 0x21: return 0xFF55; // PageUp
    case 0x22: return 0xFF56; // PageDown
    case 0x2D: return 0xFF63; // Insert
    case 0x70: return 0xFFBE; // F1
    case 0x71: return 0xFFBF; // F2
    case 0x72: return 0xFFC0; // F3
    case 0x73: return 0xFFC1; // F4
    case 0x74: return 0xFFC2; // F5
    case 0x75: return 0xFFC3; // F6
    case 0x76: return 0xFFC4; // F7
    case 0x77: return 0xFFC5; // F8
    case 0x78: return 0xFFC6; // F9
    case 0x79: return 0xFFC7; // F10
    case 0x7A: return 0xFFC8; // F11
    case 0x7B: return 0xFFC9; // F12
    case 0xBD: return 0x2D;   // -
    case 0xBB: return 0x3D;   // =
    case 0xDB: return 0x5B;   // [
    case 0xDD: return 0x5D;   // ]
    case 0xDC: return 0x5C;   // backslash
    case 0xBA: return 0x3B;   // ;
    case 0xDE: return 0x27;   // '
    case 0xBC: return 0x2C;   // ,
    case 0xBE: return 0x2E;   // .
    case 0xBF: return 0x2F;   // /
    case 0xC0: return 0x60;   // `
    case 0x10: return 0xFFE1; // Shift_L
    case 0x11: return 0xFFE3; // Control_L
    case 0x12: return 0xFFE9; // Alt_L
    case 0x5B: return 0xFFEB; // Super_L
    case 0x14: return 0xFFE5; // Caps_Lock
    default: return 0;
    }
}

#endif // STE_USE_LIBVNCCLIENT
