#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 远控隧道 · 管理端 RFB 客户端（RFB 003.008 over WebSocket）
//
// 接在 viewer 的第二条 WS（/ws/tunnel，复用管理端令牌鉴权）之上。握手顺序：
//   auth{token} → tunnel.ready → tunnel.open{uid} → tunnel.opened
// 之后这条 WS 的**二进制通道**就是一条「直连教室机 5900 的 VNC」——本协议在此之上跑
// 标准 RFB：ProtocolVersion → SecurityTypes(选 2=VNC 标准 DES 认证) → SecurityResult
// → ClientInit → ServerInit → SetPixelFormat/SetEncodings(Raw) → FramebufferUpdateRequest
// → 收到 FramebufferUpdate(Raw 编码) 解码成 QImage 经 frameReady 抛出。
//
// 安全：VNC 密码只在「老师端 ↔ 本机 VNC」这一段用，云端只搬字节、看不到明文密码
// （老师端拿密码做 DES 挑战应答；云端全程是密文字节流）。
//
// 输入回灌：老师端的鼠标/键盘经 sendPointer/sendKey 编成 RFB PointerEvent/KeyEvent，
// 走同一条隧道发回教室机（由 VNC 服务注入）——**不**走旧的 instruction/input 通道，
// 避免和 VNC 服务双重注入。
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QWebSocket>
#include <QImage>
#include <QString>

class RfbClient : public QObject
{
    Q_OBJECT
public:
    explicit RfbClient(QObject *parent = nullptr);

    /// 启动：tunnelUrl 形如 ws://host:port/ws/tunnel；token=管理端令牌；uid=目标设备；
    /// password=教室机 VNC 密码（用于 DES 挑战应答）。
    void start(const QString &tunnelUrl, const QString &token, const QString &uid, const QString &password);
    void stop();
    bool isActive() const { return m_active; }
    bool isOpen() const { return m_open; }
    QSize frameSize() const { return m_fb; }

public slots:
    /// 鼠标：nx,ny ∈ [0,1]（画面内归一化绝对坐标）；mask 按钮位（1=左 2=右 4=中）。
    void sendPointer(double nx, double ny, quint8 mask);
    /// 键盘：keysym（X11 keysym）；down=true 按下。
    void sendKey(quint32 keysym, bool down);
    /// 键盘（便捷）：直接吃 Windows 虚拟键码 VK，内部转 X11 keysym 再发 RFB KeyEvent。
    void sendKeyVk(int vk, bool down);
    /// VK → X11 keysym（RFB 键码用 keysym，不是 VK；常见键覆盖，未见键返回 0）。
    static quint32 vkToKeysym(int vk);

signals:
    void frameReady(const QImage &img);   // 解码出的一帧完整画面
    void tunnelOpened();                   // 隧道贯通（可开始发输入）
    void status(const QString &msg);
    void closed();

private slots:
    void onWsConnected();
    void onWsBinaryReceived(const QByteArray &data);
    void onWsTextReceived(const QString &msg);
    void onWsError();
    void onWsDisconnected();

private:
    enum class Phase { Idle, WaitVersion, WaitSecurityTypes, WaitChallenge,
                       WaitSecurityResult, WaitServerInit, Connected };

    void sendVersion();
    void sendSecurityType(quint8 t);
    void doVncAuth(const QByteArray &challenge);
    void sendClientInit();
    void onServerInit(const QByteArray &msg);
    void sendSetPixelFormat();
    void sendSetEncodings();
    void requestUpdate(bool incremental);
    void process();                        // 驱动状态机 + 解码
    void processFramebufferUpdate();       // 解析并合成 rect（可能跨多个二进制包）
    void cleanup();                        // 收尾：关 WS/TCP、复位状态（stop() 与错误路径共用）
    QImage::Format m_fmt = QImage::Format_RGB32;

    QWebSocket *m_ws = nullptr;
    QString m_token, m_uid, m_password;
    Phase m_phase = Phase::Idle;
    QByteArray m_buf;                       // 收到的二进制缓冲（按消息长度切分）
    QSize m_fb;                             // 帧缓冲尺寸
    QImage m_screen;                        // 当前完整画面（RFB 直接合成到这里）
    bool m_active = false;
    bool m_open = false;                    // 是否收到 tunnel.opened
    int m_updRemaining = 0;                 // 当前 FramebufferUpdate 还有几个 rect 没收完
    bool m_bigEndian = false;               // ServerInit 报告的像素格式（我们随后强制 LE，仅作校验）
};
