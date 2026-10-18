// 远控隧道 · 管理端 RFB 客户端（RFB 003.008 over WebSocket）—— 详见 rfb_client.h。
#include "rfb_client.h"
#include "rfb_des.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDateTime>

static quint16 rdU16(const QByteArray &b, int o) { return (quint16(quint8(b[o])) << 8) | quint8(b[o + 1]); }
static quint32 rdU32(const QByteArray &b, int o) {
    return (quint32(rdU16(b, o)) << 16) | rdU16(b, o + 2);
}
static void wrU16(QByteArray &b, int o, quint16 v) { b[o] = char((v >> 8) & 0xFF); b[o + 1] = char(v & 0xFF); }
static void wrU32(QByteArray &b, int o, quint32 v) { wrU16(b, o, (v >> 16) & 0xFFFF); wrU16(b, o + 2, v & 0xFFFF); }

static QString envEnvelope(const QString &type, const QJsonObject &payload)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

RfbClient::RfbClient(QObject *parent) : QObject(parent) {}

void RfbClient::start(const QString &tunnelUrl, const QString &token, const QString &uid, const QString &password)
{
    if (m_active) stop();
    m_token = token;
    m_uid = uid;
    m_password = password;
    m_active = true;
    m_open = false;
    m_phase = Phase::Idle;
    m_buf.clear();
    m_updRemaining = 0;
    m_fb = QSize();
    m_screen = QImage();

    qInfo("[rfb] 启动远控隧道 RFB 客户端：uid=%s tunnel=%s", qPrintable(uid), qPrintable(tunnelUrl));
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::connected, this, &RfbClient::onWsConnected);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &RfbClient::onWsBinaryReceived);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &RfbClient::onWsTextReceived);
    connect(m_ws, &QWebSocket::errorOccurred, this, &RfbClient::onWsError);
    connect(m_ws, &QWebSocket::disconnected, this, &RfbClient::onWsDisconnected);
    m_ws->open(QUrl(tunnelUrl));
}

void RfbClient::stop()
{
    if (!m_active) return;
    cleanup();
}

void RfbClient::cleanup()
{
    const bool was = m_active;
    m_active = false;
    m_open = false;
    m_phase = Phase::Idle;
    m_buf.clear();
    m_updRemaining = 0;
    if (m_ws) { m_ws->close(); m_ws->deleteLater(); m_ws = nullptr; }
    if (was) { emit status(QStringLiteral("远控隧道已关闭")); emit closed(); }
}

void RfbClient::onWsConnected()
{
    // 第一句：鉴权（复用管理端令牌）。
    QJsonObject p;
    p.insert(QStringLiteral("token"), m_token);
    m_ws->sendTextMessage(envEnvelope(QStringLiteral("auth"), p));
    qInfo("[rfb] 第二条 WS 已连，已发 auth");
}

void RfbClient::onWsTextReceived(const QString &msg)
{
    const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("tunnel.ready")) {
        // 鉴权通过 → 请求开这台设备的隧道。
        QJsonObject p;
        p.insert(QStringLiteral("uid"), m_uid);
        m_ws->sendTextMessage(envEnvelope(QStringLiteral("tunnel.open"), p));
        qInfo("[rfb] 鉴权通过，已发 tunnel.open(uid=%s)", qPrintable(m_uid));
    } else if (type == QStringLiteral("tunnel.opened")) {
        m_open = true;
        m_phase = Phase::WaitVersion;     // 等教室机 VNC 发来 ProtocolVersion
        qInfo("[rfb] 云端 tunnel.opened —— 隧道贯通，开始 RFB 握手");
        emit tunnelOpened();
        emit status(QStringLiteral("远控隧道已贯通"));
    } else if (type == QStringLiteral("error") || type == QStringLiteral("tunnel.close")) {
        qWarning("[rfb] 云端拒绝/关闭隧道：type=%s", qPrintable(type));
        cleanup();
    }
}

void RfbClient::onWsBinaryReceived(const QByteArray &data)
{
    if (!m_open) return;          // tunnel.opened 之前不会有 RFB 字节
    m_buf.append(data);
    process();
}

void RfbClient::onWsError()
{
    if (m_ws) qWarning("[rfb] 第二条 WS 出错：%s", qPrintable(m_ws->errorString()));
    cleanup();
}

void RfbClient::onWsDisconnected()
{
    qInfo("[rfb] 第二条 WS 断开");
    cleanup();
}

// ── RFB 状态机 ────────────────────────────────────────────────────────────────
void RfbClient::process()
{
    while (true) {
        switch (m_phase) {
        case Phase::Idle:
            return;
        case Phase::WaitVersion: {
            if (m_buf.size() < 12) return;
            const QString ver = QString::fromLatin1(m_buf.constData(), 12);
            m_buf.remove(0, 12);
            qInfo("[rfb] VNC 版本 %s", qPrintable(ver.trimmed()));
            sendVersion();
            m_phase = Phase::WaitSecurityTypes;
            break;
        }
        case Phase::WaitSecurityTypes: {
            if (m_buf.size() < 1) return;
            const quint8 n = quint8(m_buf[0]);
            if (m_buf.size() < 1 + n) return;
            bool have2 = false;
            for (int i = 0; i < n; ++i) if (quint8(m_buf[1 + i]) == 2) have2 = true;
            m_buf.remove(0, 1 + n);
            if (!have2) { qWarning("[rfb] FAIL VNC 服务端不支持标准 VNC 认证(类型2)"); cleanup(); return; }
            sendSecurityType(2);
            m_phase = Phase::WaitChallenge;
            break;
        }
        case Phase::WaitChallenge: {
            if (m_buf.size() < 16) return;
            const QByteArray ch = m_buf.left(16);
            m_buf.remove(0, 16);
            doVncAuth(ch);
            m_phase = Phase::WaitSecurityResult;
            break;
        }
        case Phase::WaitSecurityResult: {
            if (m_buf.size() < 4) return;
            const quint32 res = rdU32(m_buf, 0);
            m_buf.remove(0, 4);
            if (res != 0) { qWarning("[rfb] FAIL VNC 认证被拒（密码不对？）"); cleanup(); return; }
            sendClientInit();
            m_phase = Phase::WaitServerInit;
            break;
        }
        case Phase::WaitServerInit: {
            if (m_buf.size() < 20) return;
            const quint32 nameLen = rdU32(m_buf, 20);
            const int total = 24 + int(nameLen);
            if (m_buf.size() < total) return;
            onServerInit(m_buf.left(total));
            m_buf.remove(0, total);
            m_phase = Phase::Connected;
            break;
        }
        case Phase::Connected:
            processFramebufferUpdate();
            return;
        }
    }
}

void RfbClient::sendVersion()
{
    m_ws->sendBinaryMessage(QByteArray("RFB 003.008\n"));
}

void RfbClient::sendSecurityType(quint8 t)
{
    m_ws->sendBinaryMessage(QByteArray(1, char(t)));
}

void RfbClient::doVncAuth(const QByteArray &challenge)
{
    quint8 resp[16];
    RfbDes::vncAuthResponse(m_password.toUtf8().constData(),
                             reinterpret_cast<const quint8 *>(challenge.constData()), resp);
    m_ws->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(resp), 16));
    qInfo("[rfb] 已发 VNC 认证应答（DES 挑战响应）");
}

void RfbClient::sendClientInit()
{
    m_ws->sendBinaryMessage(QByteArray(1, char(1)));   // shared-flag = 1
}

void RfbClient::onServerInit(const QByteArray &msg)
{
    const int w = rdU16(msg, 0);
    const int h = rdU16(msg, 2);
    // 像素格式（offset 4..19）：bitsPerPixel(4) depth(5) bigEndian(6) trueColor(7)
    //   redMax(8,9) greenMax(10,11) blueMax(12,13) redShift(14) greenShift(15) blueShift(16)
    m_bigEndian = (quint8(msg[6]) != 0);
    m_fb = QSize(w, h);
    m_screen = QImage(w, h, QImage::Format_RGB32);
    m_screen.fill(0xFF000000);
    const QString name = QString::fromUtf8(msg.mid(24));
    qInfo("[rfb] ServerInit：%dx%d  bigEndian=%d  桌面名=%s", w, h, (int)m_bigEndian, qPrintable(name));

    sendSetPixelFormat();    // 强制 32 位小端 trueColor（解码口径统一）
    sendSetEncodings();      // 只认 Raw（简单可靠，教室带宽够用）
    requestUpdate(false);    // 第一帧：非增量（全画面）
}

void RfbClient::sendSetPixelFormat()
{
    QByteArray m(18, char(0));
    m[0] = char(0);            // type = SetPixelFormat
    m[2] = char(32);           // bits-per-pixel
    m[3] = char(24);           // depth
    m[4] = char(0);            // big-endian = 0（小端）
    m[5] = char(1);            // true-colour = 1
    wrU16(m, 6, 255);          // red-max
    wrU16(m, 8, 255);          // green-max
    wrU16(m, 10, 255);         // blue-max
    m[12] = char(16);          // red-shift
    m[13] = char(8);           // green-shift
    m[14] = char(0);           // blue-shift
    m_ws->sendBinaryMessage(m);
}

void RfbClient::sendSetEncodings()
{
    QByteArray m(8, char(0));
    m[0] = char(2);            // type = SetEncodings
    wrU16(m, 2, 1);            // n = 1
    wrU32(m, 4, 0);            // encoding = 0 (Raw)
    m_ws->sendBinaryMessage(m);
}

void RfbClient::requestUpdate(bool incremental)
{
    if (m_fb.isEmpty()) return;
    QByteArray m(10, char(0));
    m[0] = char(3);            // type = FramebufferUpdateRequest
    m[1] = char(incremental ? 1 : 0);
    wrU16(m, 2, 0); wrU16(m, 4, 0);
    wrU16(m, 6, quint16(m_fb.width()));
    wrU16(m, 8, quint16(m_fb.height()));
    m_ws->sendBinaryMessage(m);
}

void RfbClient::processFramebufferUpdate()
{
    while (true) {
        if (m_updRemaining > 0) {
            // 收 rect 数据（type+header 已在上一步移除）
            if (m_buf.size() < 12) return;
            const int x = rdU16(m_buf, 0), y = rdU16(m_buf, 2);
            const int w = rdU16(m_buf, 4), h = rdU16(m_buf, 6);
            const quint32 enc = rdU32(m_buf, 8);
            if (enc != 0) {     // 只支持 Raw（已 SetEncodings 限定）
                qWarning("[rfb] FAIL 不支持的 RFB 编码 %u（应只收到 Raw）", enc);
                m_buf.clear(); m_updRemaining = 0; requestUpdate(false); return;
            }
            const int dataLen = w * h * 4;
            if (m_buf.size() < 12 + dataLen) return;
            // 解码合成到 m_screen
            if (w > 0 && h > 0) {
                const quint32 *src = reinterpret_cast<const quint32 *>(m_buf.constData() + 12);
                const int fw = m_screen.width(), fh = m_screen.height();
                const int cw = qMin(w, fw - x), ch = qMin(h, fh - y);
                for (int row = 0; row < ch; ++row) {
                    const quint32 *srow = src + row * w;
                    quint32 *dst = reinterpret_cast<quint32 *>(m_screen.scanLine(y + row)) + x;
                    for (int col = 0; col < cw; ++col) {
                        const quint32 p = srow[col];
                        // 服务端按我们 SetPixelFormat 的请求（redShift=16, bigEndian=0,
                        // trueColor=1）发像素：每个像素的 32 位值即 0x00RRGGBB，
                        // 与 QImage::Format_RGB32（0xFFRRGGBB，R 在高字节、B 在低字节）同布局，
                        // 只差最高字节的 alpha 位 —— 直接置 alpha 即可，不要按 BGR 再换字节
                        //（那种写法会把 R/B 对调，画面红蓝反色）。
                        dst[col] = 0xFF000000 | (p & 0x00FFFFFF);
                    }
                }
            }
            m_buf.remove(0, 12 + dataLen);
            m_updRemaining--;
            if (m_updRemaining == 0) {
                emit frameReady(m_screen);
                requestUpdate(true);     // 一发一收：请求下一帧（增量）
            }
            continue;
        }
        // 收 server→client 消息头
        if (m_buf.size() < 1) return;
        const quint8 type = quint8(m_buf[0]);
        if (type == 0) {                 // FramebufferUpdate
            if (m_buf.size() < 4) return;
            const int nrects = (int(quint8(m_buf[2])) << 8) | int(quint8(m_buf[3]));
            m_buf.remove(0, 4);
            m_updRemaining = nrects;
            if (nrects == 0) { requestUpdate(true); continue; }   // 空更新：继续等
            continue;
        } else if (type == 1) {          // SetColourMapEntries
            if (m_buf.size() < 6) return;
            const int count = (int(quint8(m_buf[4])) << 8) | int(quint8(m_buf[5]));
            const int total = 6 + count * 3;
            if (m_buf.size() < total) return;
            m_buf.remove(0, total); continue;
        } else if (type == 2) {         // Bell
            m_buf.remove(0, 1); continue;
        } else if (type == 3) {         // ServerCutText
            if (m_buf.size() < 8) return;
            const quint32 len = rdU32(m_buf, 4);
            const int total = 8 + int(len);
            if (m_buf.size() < total) return;
            m_buf.remove(0, total); continue;
        } else {
            qWarning("[rfb] FAIL 收到未知 RFB 消息类型 %u，重置缓冲防错位", type);
            m_buf.clear(); m_updRemaining = 0; requestUpdate(false); return;
        }
    }
}

// ── 输入回灌（走隧道，RFB 事件）───────────────────────────────────────────────
void RfbClient::sendPointer(double nx, double ny, quint8 mask)
{
    if (!m_open || m_fb.isEmpty()) return;
    const quint16 X = quint16(qBound(0.0, nx, 1.0) * qMax(1, m_fb.width() - 1));
    const quint16 Y = quint16(qBound(0.0, ny, 1.0) * qMax(1, m_fb.height() - 1));
    QByteArray m(6, char(0));
    m[0] = char(5);            // type = PointerEvent
    m[1] = char(mask);
    wrU16(m, 2, X);
    wrU16(m, 4, Y);
    m_ws->sendBinaryMessage(m);
}

void RfbClient::sendKey(quint32 keysym, bool down)
{
    if (!m_open) return;
    QByteArray m(8, char(0));
    m[0] = char(4);            // type = KeyEvent
    m[1] = char(down ? 1 : 0);
    wrU32(m, 2, keysym);
    // 末尾 2 字节 pad 为 0
    m_ws->sendBinaryMessage(m);
}

quint32 RfbClient::vkToKeysym(int vk)
{
    // 字母/数字与 VK 同值（VK_A..Z=0x41..5A，VK_0..9=0x30..39）
    if ((vk >= 0x41 && vk <= 0x5A) || (vk >= 0x30 && vk <= 0x39)) return quint32(vk);
    // 功能键 F1..F24 = 0xFFBE..0xFFD5
    if (vk >= 0x70 && vk <= 0x87) return quint32(0xFFBE + (vk - 0x70));
    switch (vk) {
    case 0x25: return 0xFF51; // Left
    case 0x26: return 0xFF52; // Up
    case 0x27: return 0xFF53; // Right
    case 0x28: return 0xFF54; // Down
    case 0x09: return 0xFF09; // Tab
    case 0x08: return 0xFF08; // Backspace
    case 0x2E: return 0xFFFF; // Delete
    case 0x2D: return 0xFF63; // Insert
    case 0x24: return 0xFF50; // Home
    case 0x23: return 0xFF57; // End
    case 0x21: return 0xFF55; // PageUp
    case 0x22: return 0xFF56; // PageDown
    case 0x0D: return 0xFF0D; // Return/Enter
    case 0x1B: return 0xFF1B; // Escape
    case 0x20: return 0x20;   // Space
    case 0x14: return 0xFFE5; // CapsLock
    case 0x90: return 0xFF7F; // NumLock
    case 0x91: return 0xFF14; // ScrollLock
    case 0x13: return 0xFF13; // Pause
    case 0x2C: return 0xFF61; // PrintScreen
    case 0x10: return 0xFFE1; // Shift (Left Shift)
    case 0x11: return 0xFFE3; // Control (Left Ctrl)
    case 0x12: return 0xFFE9; // Alt (Left Alt)
    case 0x5B: return 0xFFEB; // Meta (Left Win)
    case 0x5C: return 0xFFEC; // Meta (Right Win)
    case 0x5D: return 0xFF67; // Menu
    default: return 0;
    }
}

void RfbClient::sendKeyVk(int vk, bool down)
{
    const quint32 ks = vkToKeysym(vk);
    if (ks == 0) return;
    sendKey(ks, down);
}
