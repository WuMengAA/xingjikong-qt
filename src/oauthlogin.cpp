// OAuth 登录流程实现（详见 oauthlogin.h 的头注释）。
//
// 时序：起临时端口 → 开浏览器 → 站点授权页（站内登录+同意）→ 回拨 127.0.0.1:<port>/oauth-callback?code&state
//        → 接住并回一张提示页给浏览器 → 拿 code 换网站会话令牌 → 停端口。

#include "oauthlogin.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHostAddress>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <cstdlib>
#include <ctime>

#include "viewerbackend.h"   // 只为 logf（全局日志，与界面共用一份落盘日志）

namespace {

/** 站点会话令牌的兜底有效期（秒）。站点给 30 天，这里只做兜底 display 用。 */
constexpr qint64 kDefaultSessionSec = 30 * 24 * 60 * 60;

/** 随机串（state 用）。state 必须不可猜，否则别人能伪造回拨把 code 骗走。 */
QString randomHex(int bytes)
{
    static bool seeded = false;
    if (!seeded) {
        // 时钟 + 函数地址：同一毫秒连开两次也不会撞上同一串 state
        std::srand(static_cast<unsigned>(std::time(nullptr) ^ (quintptr)(void *)&randomHex));
        seeded = true;
    }
    QString out;
    out.reserve(bytes * 2);
    for (int i = 0; i < bytes; ++i) {
        const char digits[] = "0123456789abcdef";
        out += digits[std::rand() % 16];
        out += digits[std::rand() % 16];
    }
    return out;
}

} // namespace

OAuthLogin::OAuthLogin(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

OAuthLogin::~OAuthLogin()
{
    stopServer();
    if (m_sock) {
        m_sock->disconnectFromHost();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    if (m_nam) m_nam->deleteLater();
    m_nam = nullptr;
}

void OAuthLogin::begin()
{
    if (m_busy) {
        logf("[oauth] 上一次登录还没走完，忽略本次请求");
        return;
    }

    m_siteUrl = QString::fromLocal8Bit(qgetenv("STE_SITE_URL")).trimmed();
    m_clientId =
        QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_ID")).trimmed().isEmpty()
            ? QStringLiteral("xingjikong_native")   // 站点里已登记的那台桌面端客户端
            : QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_ID")).trimmed();
    m_clientSecret = QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_SECRET")).trimmed();
    const QString redirectPath =
        QString::fromLocal8Bit(qgetenv("STE_OAUTH_REDIRECT_PATH")).trimmed().isEmpty()
            ? QStringLiteral("/oauth-callback")
            : QString::fromLocal8Bit(qgetenv("STE_OAUTH_REDIRECT_PATH")).trimmed();

    if (m_siteUrl.isEmpty()) {
        const QString why = QStringLiteral("没配站点地址（viewer.env 里加 STE_SITE_URL=https://<你的站点>）");
        logf("[oauth] FAIL %s", why.toLocal8Bit().constData());
        emit failed(why);
        return;
    }
    if (m_clientSecret.isEmpty()) {
        const QString why = QStringLiteral("没配 OAuth 客户端密钥（STE_OAUTH_CLIENT_SECRET）");
        logf("[oauth] FAIL %s", why.toLocal8Bit().constData());
        emit failed(why);
        return;
    }
    // 站点只认 http:// 的 127.0.0.1 回拨，别把 https 或局域网地址塞进去 ——
    // 授权页会在 inspect() 那一步直接回 invalid_redirect_uri，用户只看得到"登录失败"。
    if (!m_siteUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !m_siteUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        const QString why = QStringLiteral("站点地址不是 http/https（当前：%1）").arg(m_siteUrl);
        logf("[oauth] FAIL %s", why.toLocal8Bit().constData());
        emit failed(why);
        return;
    }

    m_state = randomHex(16);

    // 端口 0 = 让系统分配一个空闲临时端口。回拨校验只认 127.0.0.1 + 路径后缀，端口随便。
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        const QString why = QStringLiteral("本机临时端口开不出来（%1），可能是安全软件拦了回拨。"
                                           "请放行 127.0.0.1 的临时端口，或退出占用它的程序")
                                .arg(m_server->errorString());
        logf("[oauth] FAIL %s", why.toLocal8Bit().constData());
        emit failed(why);
        stopServer();
        return;
    }

    const int port = m_server->serverPort();
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(redirectPath);
    m_busy = true;

    QUrl authUrl(QStringLiteral("%1/oauth/authorize").arg(m_siteUrl));
    {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client_id"), m_clientId);
        q.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
        q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
        q.addQueryItem(QStringLiteral("state"), m_state);
        q.addQueryItem(QStringLiteral("scope"), QStringLiteral("cims profile"));
        authUrl.setQuery(q);
    }

    logf("[oauth] 正在浏览器里打开授权页（回拨 %s，state=%s…）",
         m_redirectUri.toLocal8Bit().constData(), m_state.left(8).toLocal8Bit().constData());
    if (!QDesktopServices::openUrl(authUrl)) {
        const QString why = QStringLiteral("打不开浏览器（没设成默认浏览器？）。"
                                           "请手动把下面地址粘到浏览器地址栏：");
        logf("[oauth] FAIL %s：%s", why.toLocal8Bit().constData(),
             authUrl.toString().toLocal8Bit().constData());
        emit failed(why + authUrl.toString());
        stopServer();
        return;
    }

    QObject::connect(m_server, &QTcpServer::newConnection, this, &OAuthLogin::onNewConnection);
}

void OAuthLogin::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        if (m_sock) {
            // 只接第一路（那才是站点回拨的这一路）；多余的关掉，别让它挂着
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->flush();
            s->deleteLater();
            continue;
        }
        m_sock = s;
        QObject::connect(s, &QTcpSocket::readyRead, this, &OAuthLogin::onReadyRead);
        QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
    }
}

void OAuthLogin::onReadyRead()
{
    QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;

    static QByteArray pending;
    pending += s->readAll();
    // 请求头到 \r\n\r\n 就完了，body 这里不会有（GET 无 body）
    if (!pending.contains("\r\n\r\n")) return;

    const QString head = QString::fromLatin1(pending.left(pending.indexOf("\r\n\r\n")));
    const QStringList lines = head.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        s->deleteLater();
        m_sock = nullptr;
        stopServer();
        return;
    }

    const QString requestLine = lines.first().trimmed();
    const QString target = requestLine.mid(requestLine.indexOf(QLatin1Char(' ')) + 1);
    const QString path = target.left(target.indexOf(QLatin1Char('?')));
    const QUrlQuery qq(target.mid(target.indexOf(QLatin1Char('?')) + 1));

    pending.clear();

    const bool pathOk = path.startsWith(QLatin1Char('/')) && path.endsWith(QStringLiteral("/oauth-callback"));
    const QString gotState = qq.queryItemValue(QStringLiteral("state"));
    const QString code = qq.queryItemValue(QStringLiteral("code"));

    if (!pathOk || gotState != m_state || code.isEmpty()) {
        // 回拨对不上 state 一律按失败处理：那可能是别人点到我们端口（CSRF 面）
        logf("[oauth] WARN 回拨被拒：path=%s state匹配=%s 带code=%s",
             path.toLocal8Bit().constData(), gotState == m_state ? "是" : "否",
             code.isEmpty() ? "否" : "是");
        servePage(s, 400, "<h2>登录未完成</h2><p>回拨的地址或状态对不上，已拒绝。请回到管理端再点一次「登录」。</p>");
        s->disconnectFromHost();
        m_sock = nullptr;
        stopServer();
        emit failed(QStringLiteral("授权回拨校验没过（地址不对或状态不匹配），请重新登录"));
        return;
    }

    // 先给浏览器一个交代（用户正盯着一个标签页），再去换令牌
    servePage(s, 200, "<h2>登录成功</h2><p>管理端已拿到网站账号，这个标签页可以关掉了。</p>");
    s->disconnectFromHost();
    m_sock = nullptr;
    stopServer();

    logf("[oauth] 回拨接住，正在换网站会话令牌…");

    QUrl tokenUrl(QStringLiteral("%1/oauth/token").arg(m_siteUrl));
    QByteArray body = QStringLiteral("grant_type=authorization_code&client_id=%1&client_secret=%2&code=%3&redirect_uri=%4")
                          .arg(QUrl::toPercentEncoding(m_clientId),
                               QUrl::toPercentEncoding(m_clientSecret),
                               QUrl::toPercentEncoding(code),
                               QUrl::toPercentEncoding(m_redirectUri))
                          .toUtf8();

    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_nam->post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, this, &OAuthLogin::onTokenFinished);
}

void OAuthLogin::onTokenFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (status != 200) {
        logf("[oauth] FAIL 换令牌失败（HTTP %d）：%s", status, raw.left(512).constData());
        m_busy = false;
        emit failed(QStringLiteral("换网站会话令牌失败（站点返回 HTTP %d）").arg(status));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QJsonObject obj = doc.object();
    // 站点故意同时给 access_token 与 token 两个键（同一张票，为兼容早已发出的桌面端），
    // 这里两个都认，谁有用谁。
    const QString sessionToken = obj.value(QStringLiteral("token")).toString();
    if (sessionToken.isEmpty()) {
        const QString why = QStringLiteral("换回来的响应里没有 token（站点改了 /oauth/token 的返回？）");
        logf("[oauth] FAIL %s：%s", why.toLocal8Bit().constData(), raw.left(512).constData());
        m_busy = false;
        emit failed(why);
        return;
    }

    const qint64 expiresIn = (qint64)obj.value(QStringLiteral("expires_in")).toDouble();
    logf("[oauth] 换到网站会话令牌（有效期 %lld 秒）", expiresIn > 0 ? expiresIn : kDefaultSessionSec);

    m_busy = false;
    emit succeeded(sessionToken, expiresIn > 0 ? expiresIn : kDefaultSessionSec);
}

void OAuthLogin::servePage(QTcpSocket *sock, int code, const QByteArray &body)
{
    if (!sock) return;
    const QByteArray html =
        "<!doctype html><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<body style=\"font:15px/1.7 system-ui,'Microsoft YaHei',sans-serif;padding:40px;"
        "max-width:520px;margin:0 auto;color:#222\">" + body +
        "</body>";
    const QByteArray head =
        QStringLiteral("HTTP/1.1 %1 %2\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: %3\r\nConnection: close\r\n\r\n")
            .arg(code)
            .arg(code == 200 ? QStringLiteral("OK") : QStringLiteral("Bad Request"))
            .arg(html.size())
            .toUtf8();
    sock->write(head + html);
    sock->flush();
}

void OAuthLogin::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}
