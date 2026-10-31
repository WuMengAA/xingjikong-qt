// 星集控被控端 · Local API 网关实现（2026-10-31）
#include "localapi_server.h"
#include "localapi_auth.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

// 常量
static const quint16 kPort = 8080;
static const int kMaxHeaderBytes = 16 * 1024;      // 请求头上限，防滥用
static const int kIdleTimeoutMs = 10000;           // 单连接空闲超时

// ── 工具 ────────────────────────────────────────────────────────────────
static QByteArray statusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

LocalApiServer::LocalApiServer(QObject *parent)
    : QObject(parent)
{
    // m_auth 是纯类（非 QObject），在 start() 里拿到 configDir 才构造；析构手动释放。
}

LocalApiServer::~LocalApiServer()
{
    delete m_auth;
    m_auth = nullptr;
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
    }
}

void LocalApiServer::start()
{
    // ① 配置目录（与 main.cpp 数据目录同根，真源唯一）
    const QString configDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");

    // ② 鉴权器（token 签发/校验/限流/审计），拿到 configDir 才可构造
    m_auth = new LocalApiAuth(configDir);

    // ③ token（首次生成并落盘；空=异常，端点层 fail-closed）
    const QString token = m_auth->loadOrCreateToken();

    // ④ 本机身份：与 main.cpp 读同一文件（classcode.txt / devrole.txt）
    m_classCode = readFileTrimmed(configDir + QStringLiteral("/classcode.txt"));
    m_role = readFileTrimmed(configDir + QStringLiteral("/devrole.txt"));
    if (m_role != QStringLiteral("broadcast")) m_role = QStringLiteral("classroom");  // 默认教室机

    // ⑤ 仅回环绑定（安全边界写死）
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, kPort)) {
        m_lastError = m_server->errorString();
        qWarning().noquote() << QStringLiteral("[local-api] ⚠️ 无法监听 127.0.0.1:%1（%2）")
                                    .arg(kPort).arg(m_lastError);
        return;
    }
    m_listening = true;
    connect(m_server, &QTcpServer::newConnection, this, &LocalApiServer::onNewConnection);

    qInfo().noquote() << QStringLiteral("[local-api] 已监听 127.0.0.1:%1 ｜ token=%2 ｜ class=%3 ｜ role=%4")
                             .arg(kPort)
                             .arg(token.isEmpty() ? QStringLiteral("<无>") : QStringLiteral("<已签发>"))
                             .arg(m_classCode.isEmpty() ? QStringLiteral("<未归属>") : m_classCode)
                             .arg(m_role);
}

QString LocalApiServer::readFileTrimmed(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QString s = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    return s;
}

void LocalApiServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        if (!sock) continue;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock] { finishClient(sock); });
        // 空闲超时兜底：请求迟迟不发完 → 断开（定时器随 sock 析构自动取消）
        QTimer::singleShot(kIdleTimeoutMs, sock, [sock] {
            if (sock->state() == QAbstractSocket::ConnectedState) sock->abort();
        });
    }
}

void LocalApiServer::onReadyRead(QTcpSocket *sock)
{
    if (!m_buf.contains(sock)) m_buf[sock] = QByteArray();
    m_buf[sock] += sock->readAll();

    // 请求头超限 → 431
    if (m_buf[sock].size() > kMaxHeaderBytes) {
        respond(sock, 431, QByteArrayLiteral("{\"ok\":false,\"error\":\"header_too_large\"}"));
        finishClient(sock);
        return;
    }
    // 还没收完请求头
    if (!m_buf[sock].contains("\r\n\r\n")) return;

    const QByteArray req = m_buf[sock];
    m_buf.remove(sock);
    handleRequest(sock, req);
}

void LocalApiServer::handleRequest(QTcpSocket *sock, const QByteArray &req)
{
    // 解析请求行 + 头（仅取需要的字段，不做完整 HTTP 状态机）
    const int headerEnd = req.indexOf("\r\n\r\n");
    const QByteArray headerBlock = req.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');

    // 请求行：METHOD SP TARGET SP HTTP/x.y
    QByteArray method, target;
    {
        const QByteArray requestLine = lines.value(0).trimmed();
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2) {
            respond(sock, 400, QByteArrayLiteral("{\"ok\":false,\"error\":\"bad_request\"}"));
            finishClient(sock);
            return;
        }
        method = parts.at(0).trimmed().toUpper();
        target = parts.at(1).trimmed();
    }

    // 客户端 IP（回环下恒 127.0.0.1，但口径通用）
    const QString clientIp = sock->peerAddress().toString();

    // 去掉查询串（/v1/status?x=1 → /v1/status）——鉴权/路由都按纯路径判
    QByteArray path = target;
    const int q = path.indexOf('?');
    if (q >= 0) path = path.left(q);

    // ① 限流（超限 → 429 + 审计）
    if (!m_auth->allow(clientIp)) {
        m_auth->audit(clientIp, method + " " + target, QStringLiteral("rate_limited"));
        respond(sock, 429, QByteArrayLiteral("{\"ok\":false,\"error\":\"rate_limited\"}"));
        finishClient(sock);
        return;
    }

    // ② 公开路径：仅 GET /v1/ping（存活探测）。方法不对 → 405（路径公开，不藏方法）。
    //    其余一律先鉴权 —— 未知路径不先鉴权会泄露路由信息（对应云端「前端隐藏≠权限」）。
    if (path == "/v1/ping") {
        if (method != "GET") {
            respond(sock, 405, QByteArrayLiteral("{\"ok\":false,\"error\":\"method_not_allowed\"}"));
            finishClient(sock);
            return;
        }
        const QByteArray body = QByteArrayLiteral("{\"ok\":true,\"service\":\"stelarith-local-api\",\"proto\":1}");
        respond(sock, 200, body);
        finishClient(sock);
        return;
    }

    // ③ 鉴权：请求头 X-Local-Api-Token 必须等于本机签发的 token（大小写不敏感）
    {
        QByteArray provided;
        for (const QByteArray &line : lines) {
            const QByteArray t = line.trimmed();
            const int colon = t.indexOf(':');
            if (colon <= 0) continue;
            if (t.left(colon).trimmed().compare("X-Local-Api-Token", Qt::CaseInsensitive) == 0) {
                provided = t.mid(colon + 1).trimmed();
                break;
            }
        }
        if (!m_auth->validate(QString::fromLatin1(provided))) {
            m_auth->audit(clientIp, method + " " + target, QStringLiteral("unauthorized"));
            respond(sock, 401, QByteArrayLiteral("{\"ok\":false,\"error\":\"unauthorized\"}"));
            finishClient(sock);
            return;
        }
    }

    // ④ 路由（已过鉴权/限流）
    route(sock, method, path);
    finishClient(sock);
}

void LocalApiServer::route(QTcpSocket *sock, const QByteArray &method, const QByteArray &path)
{
    // 仅支持 GET（v1 最小集）；其它方法 → 405（已鉴权路径）
    if (method != "GET") {
        respond(sock, 405, QByteArrayLiteral("{\"ok\":false,\"error\":\"method_not_allowed\"}"));
        return;
    }

    if (path == "/v1/status") {
        // 已鉴权：返回本机身份（per-device 口径），供联动方确认"我在跟这台设备说话"
        QJsonObject o;
        o["ok"] = true;
        o["auth"] = "token";
        o["classCode"] = m_classCode.isEmpty() ? QJsonValue(QString()) : QJsonValue(m_classCode);
        o["role"] = m_role;
        respond(sock, 200, QJsonDocument(o).toJson(QJsonDocument::Compact));
        return;
    }

    respond(sock, 404, QByteArrayLiteral("{\"ok\":false,\"error\":\"not_found\"}"));
}

void LocalApiServer::respond(QTcpSocket *sock, int status, const QByteArray &body)
{
    if (!sock) return;
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "\r\n";
    out += body;
    sock->write(out);
    sock->flush();
    // Connection: close → 写完即断
    sock->disconnectFromHost();
}

void LocalApiServer::finishClient(QTcpSocket *sock)
{
    if (!sock) return;
    m_buf.remove(sock);
    // 只清一次：断开本服务与 sock 的全部连接（readyRead/disconnected 回调），
    // 避免 disconnected 信号再触发一轮 finishClient 造成二次 deleteLater。
    sock->disconnect(this);
    if (sock->state() != QAbstractSocket::UnconnectedState)
        sock->disconnectFromHost();
    sock->deleteLater();
}

// ── 装配入口：main.cpp 只在启动路径调一次（见 main.cpp localApiInit 调用点）──
void localApiInit()
{
    static LocalApiServer *s = nullptr;
    if (s) return;                    // 幂等：重复调用不重启
    s = new LocalApiServer(qApp);
    s->start();
}
