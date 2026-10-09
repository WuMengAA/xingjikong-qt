// OAuth 登录流程实现（详见 oauthlogin.h 的头注释）。
//
// 时序：起临时端口 → 打开授权页（内嵌窗口 / 系统浏览器）→ 站点授权页（站内登录+同意）
//        → 回拨 127.0.0.1:<port>/oauth-callback?code&state
//        → 接住并回一张提示页 → 拿 code 换网站会话令牌 → 停端口、关窗口。

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
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

// 内嵌登录窗（2026-10-06 加）：QWebEngineView 是 QWidget。
// WebEngine 上下文在管理端进程里本来就有（收流页那个离屏 view），这里只是复用，没有新增依赖。
#include <QWidget>
#include <QVBoxLayout>
#include <QWebEngineView>

#include <cstdlib>
#include <ctime>

#include "viewerbackend.h"   // 只为 logf（全局日志，与界面共用一份落盘日志）

namespace {

/** 站点会话令牌的兜底有效期（秒）。站点给 30 天，这里只做兜底 display 用。 */
constexpr qint64 kDefaultSessionSec = 30 * 24 * 60 * 60;

/** 登录成功后，内嵌窗口停留多久再自动关（让人看一眼"登录成功"）。 */
constexpr int kCloseAfterSuccessMs = 1200;

/**
 * 一次登录的总超时（毫秒）。从打开授权页算起，到站点回拨为止。
 * 3 分钟：够用户走完"登录站点点同意"（含第一次注册/找回密码），又不至于让人干等。
 * 超时必须收口，否则就是"永远卡在正在登录"（详见 begin() 里的说明）。
 */
constexpr int kLoginTimeoutMs = 3 * 60 * 1000;

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
    // 窗口设了 WA_DeleteOnClose，这里只解引用不删，避免析构期二次 delete
    m_loginWin = nullptr;
    if (m_nam) m_nam->deleteLater();
    m_nam = nullptr;
}

void OAuthLogin::begin()
{
    // ⚠️ 开局先把回拨缓冲清空：它是**成员**（不是函数内 static，见 :257 那段修复），
    //    上一次登录半途取消/超时留下的半截请求行会污染这一次的回拨解析，
    //    症状是"回拨被拒：state 匹配=否"这种看不懂的失败。
    m_pending.clear();

    if (m_busy) {
        // ⚠️ 以前失败路径没复位 m_busy，第二次点登录就卡在这儿：既不继续、也不报错，
        // 用户只看到按钮没反应。现在所有退出路径都复位了，这句正常情况下不该再出现；
        // 真出现了就在日志里点名，别让它变成一句"莫名其妙没反应"。
        logf("[oauth] 上一次登录还没走完，忽略本次请求（如反复出现即为状态未复位，属 bug）");
        return;
    }

    // 站点地址：环境变量能覆盖（本机联调自有站点），没配就用生产站点 ——
    // 管理端是发下去给人装的东西，一上来就报"没配站点地址"等于逼用户去看文档。
    m_siteUrl = QString::fromLocal8Bit(qgetenv("STE_SITE_URL")).trimmed();
    if (m_siteUrl.isEmpty()) m_siteUrl = QString::fromUtf8(kDefaultSiteUrl);
    m_clientId =
        QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_ID")).trimmed().isEmpty()
            ? QStringLiteral("xingjikong_native")   // 站点里已登记的那台桌面端客户端
            : QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_ID")).trimmed();
    // 密钥**不是必需的**：xingjikong_native 是 loopback 原生客户端，站点按 RFC 8252
    // 把它当公开客户端（validateClient 里的 PUBLIC 哨兵分支：要求对方「明确不带」密钥）。
    // ⚠️ 这里一旦因为"密钥没配"直接失败，用户只会看到"绑定失败"，而真正的问题从来不在密钥上
    //    —— 所以留空是正常状态，不是错误状态。
    m_clientSecret = QString::fromLocal8Bit(qgetenv("STE_OAUTH_CLIENT_SECRET")).trimmed();
    const QString redirectPath =
        QString::fromLocal8Bit(qgetenv("STE_OAUTH_REDIRECT_PATH")).trimmed().isEmpty()
            ? QStringLiteral("/oauth-callback")
            : QString::fromLocal8Bit(qgetenv("STE_OAUTH_REDIRECT_PATH")).trimmed();

    logf("[oauth] 站点=%s 客户端=%s 密钥=%s", m_siteUrl.toUtf8().constData(),
         m_clientId.toUtf8().constData(),
         m_clientSecret.isEmpty() ? "不带（公开客户端）" : "带");
    // 站点只认 http:// 的 127.0.0.1 回拨，别把 https 或局域网地址塞进去 ——
    // 授权页会在 inspect() 那一步直接回 invalid_redirect_uri，用户只看得到"登录失败"。
    if (!m_siteUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !m_siteUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        const QString why = QStringLiteral("站点地址不是 http/https（当前：%1）").arg(m_siteUrl);
        logf("[oauth] FAIL %s", why.toUtf8().constData());
        m_busy = false;
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
        logf("[oauth] FAIL %s", why.toUtf8().constData());
        m_busy = false;
        emit failed(why);
        stopServer();
        return;
    }

    const int port = m_server->serverPort();
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(redirectPath);
    m_busy = true;

    // ⚠️ 登录总超时（2026-10-08 补）：授权页开出去之后，用户可能一直不点"同意"、
    //    浏览器被安全软件拦掉、或者站点回拨根本到不了本机（防火墙/端口被占）。
    //    没有这条收口，就会出现"卡死在登录中"：
    //      · m_busy 永远为 true ⇒ 再点登录只会命中上面那句"上一次还没走完"（=点了没反应）
    //      · 127.0.0.1 上的临时监听端口一直开着，谁都不收
    //      · 界面上既不报错、也没有重试入口
    //    到点必须把状态全部复位，并把原因说人话。
    if (!m_timeout) {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        QObject::connect(m_timeout, &QTimer::timeout, this, [this]() {
            if (!m_busy) return;                 // 已经正常收尾过了
            m_busy = false;
            stopServer();
            closeLoginWindow();
            logf("[oauth] FAIL 登录超时：等网站回拨 %d 秒没等到，已收口（可重新登录）",
                 kLoginTimeoutMs / 1000);
            emit failed(QStringLiteral("登录超时：没等到网站回拨。"
                                       "如果在浏览器/登录窗口里还没点「同意」，请重试一次；"
                                       "若反复超时，多半是回拨被安全软件拦了。"));
        });
    }
    m_timeout->start(kLoginTimeoutMs);

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

    QObject::connect(m_server, &QTcpServer::newConnection, this, &OAuthLogin::onNewConnection);

    logf("[oauth] 正在打开授权页（回拨 %s，state=%s…）",
         m_redirectUri.toUtf8().constData(), m_state.left(8).toUtf8().constData());
    if (!openAuthUrl(authUrl)) {
        const QString why = QStringLiteral("打不开登录窗口，系统浏览器也叫不起来。"
                                           "请把下面这行地址复制到浏览器地址栏手动打开：");
        logf("[oauth] FAIL %s：%s", why.toUtf8().constData(),
             authUrl.toString().toUtf8().constData());
        // ⚠️ 这两行顺序不能反：先落状态再发信号，否则界面收到 failed 时 busy 还是 true，
        // 登录按钮会一直灰着（"点了没反应"的老毛病）。
        m_busy = false;
        stopServer();
        emit failed(why + QStringLiteral("\n") + authUrl.toString());
        return;
    }
}

bool OAuthLogin::openAuthUrl(const QUrl &authUrl)
{
    // ① 内嵌窗口：不依赖系统里有没有默认浏览器，教室机上最稳的一条路
    if (openEmbeddedWindow(authUrl)) {
        logf("[oauth] 已用内嵌登录窗口打开授权页");
        return true;
    }
    // ② 回落系统浏览器
    if (QDesktopServices::openUrl(authUrl)) {
        logf("[oauth] 已用系统浏览器打开授权页");
        return true;
    }
    logf("[oauth] FAIL 内嵌窗口与系统浏览器都没能打开授权页");
    return false;
}

bool OAuthLogin::openEmbeddedWindow(const QUrl &url)
{
    // 排障开关：STE_OAUTH_EMBEDDED=0 强制走系统浏览器（比如要复现"只有浏览器才有的问题"）
    if (QString::fromLocal8Bit(qgetenv("STE_OAUTH_EMBEDDED")).trimmed() == QLatin1String("0")) {
        logf("[oauth] STE_OAUTH_EMBEDDED=0：本次跳过内嵌窗口");
        return false;
    }

    auto *win = new QWidget();
    win->setWindowTitle(QStringLiteral("星集控 · 用星璃账号登录"));
    win->setAttribute(Qt::WA_DeleteOnClose);
    auto *lay = new QVBoxLayout(win);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *view = new QWebEngineView(win);
    lay->addWidget(view);

    win->resize(520, 720);
    win->setMinimumSize(400, 520);

    m_loginWin = win;
    // 用户直接把窗口关了 = 放弃这次登录。必须复位状态，否则下一次点登录会被
    // begin() 开头那句"上一次还没走完"挡掉（同样是"点了没反应"）。
    QObject::connect(win, &QObject::destroyed, this, [this](QObject *) {
        m_loginWin = nullptr;
        if (!m_busy) return;          // 正常收尾（成功/失败）已经复位过了
        m_busy = false;
        stopServer();
        logf("[oauth] 用户关掉了登录窗口，本次登录取消");
        emit failed(QStringLiteral("登录窗口被关掉了，没完成登录"));
    });

    view->setUrl(url);
    win->show();
    win->raise();
    win->activateWindow();
    return true;
}

void OAuthLogin::closeLoginWindow()
{
    if (!m_loginWin) return;
    // 直接 delete：destroyed 回调里会看到 m_busy 已经复位过（false），不会再发一次 failed
    m_loginWin->close();
    m_loginWin = nullptr;
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

    // ⚠️ 2026-10-07 修：以前这里是函数级 `static QByteArray pending;` ——
    // 静态＝**所有实例共用一份**，上一次登录/上一个连接的残留会污染这一次的解析。
    // 改成成员缓冲，一次登录一块。
    m_pending += s->readAll();
    // 请求头到 \r\n\r\n 就完了，body 这里不会有（GET 无 body）
    if (!m_pending.contains("\r\n\r\n")) return;

    const QString head = QString::fromLatin1(m_pending.left(m_pending.indexOf("\r\n\r\n")));
    const QStringList lines = head.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        s->deleteLater();
        m_sock = nullptr;
        stopServer();
        m_busy = false;
        return;
    }

    const QString requestLine = lines.first().trimmed();
    // ⚠️ 2026-10-07 修（真机报「授权回拨校验没过」的根因）：
    // 请求行形如 `GET /oauth-callback?code=…&state=… HTTP/1.1`。
    // 老写法 mid(第一个空格之后) **把行尾的 " HTTP/1.1" 一起带进了 query 串**，
    // state 被解析成 "<hex> HTTP/1.1" ⇒ 与发出去的值永不相等。
    // 症状恰好是 `state匹配=否 带code=是`：code 落在第一个 & 之前所以干净，
    // 只有排在最后、被行尾直接污染的那个参数（state）会错。
    // 正确做法：请求行按空格切三段（方法/目标/协议），**只取中间那段**。
    const QString target = requestLine.section(QLatin1Char(' '), 1, 1);
    const QString path = target.left(target.indexOf(QLatin1Char('?')));
    const QUrlQuery qq(target.mid(target.indexOf(QLatin1Char('?')) + 1));

    m_pending.clear();

    const bool pathOk = path.startsWith(QLatin1Char('/')) && path.endsWith(QStringLiteral("/oauth-callback"));
    const QString gotState = qq.queryItemValue(QStringLiteral("state"));
    const QString code = qq.queryItemValue(QStringLiteral("code"));

    if (!pathOk || gotState != m_state || code.isEmpty()) {
        // 回拨对不上 state 一律按失败处理：那可能是别人点到我们端口（CSRF 面）
        logf("[oauth] WARN 回拨被拒：path=%s state匹配=%s（收到 %.8s… / 期望 %.8s…）带code=%s",
             path.toUtf8().constData(), gotState == m_state ? "是" : "否",
             gotState.left(8).toUtf8().constData(),
             m_state.left(8).toUtf8().constData(),
             code.isEmpty() ? "否" : "是");
        servePage(s, 400, "<h2>登录未完成</h2><p>回拨的地址或状态对不上，已拒绝。请回到管理端再点一次「登录」。</p>");
        s->disconnectFromHost();
        m_sock = nullptr;
        stopServer();
        // ⚠️ 原来这里漏了复位：授权页上点"取消"也会走到这条分支，
        // 之后 m_busy 一直 true，用户再点登录就是"没反应"。
        m_busy = false;
        closeLoginWindow();
        emit failed(QStringLiteral("授权回拨校验没过（地址不对或状态不匹配），请重新登录"));
        return;
    }

    // 先给一个交代（用户正盯着那个窗口），再去换令牌
    servePage(s, 200, "<h2>登录成功</h2><p>管理端已拿到网站账号，可以关掉这个页面了。</p>");
    s->disconnectFromHost();
    m_sock = nullptr;
    stopServer();

    // 让人看一眼成功页再关，别在"正在换令牌"的时候窗口突然没了
    QTimer::singleShot(kCloseAfterSuccessMs, this, [this] { closeLoginWindow(); });

    logf("[oauth] 回拨接住，正在换网站会话令牌…");

    QUrl tokenUrl(QStringLiteral("%1/oauth/token").arg(m_siteUrl));
    // 公开客户端（没配密钥）就**整个不带** client_secret —— 站点那条 PUBLIC 分支判的是
    // "参数不存在"，不是 "参数为空字符串"。带着一个空的 client_secret 发过去反而会被判成
    // 「带错 secret」，授权白走一遍还给一句看不懂的错。
    QStringList form;
    form << QStringLiteral("grant_type=authorization_code");
    form << QStringLiteral("client_id=%1").arg(QUrl::toPercentEncoding(m_clientId));
    if (!m_clientSecret.isEmpty()) {
        form << QStringLiteral("client_secret=%1").arg(QUrl::toPercentEncoding(m_clientSecret));
    }
    form << QStringLiteral("code=%1").arg(QUrl::toPercentEncoding(code));
    form << QStringLiteral("redirect_uri=%1").arg(QUrl::toPercentEncoding(m_redirectUri));
    const QByteArray body = form.join(QLatin1Char('&')).toUtf8();

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
        // 站点出错回的是整张 HTML 错误页，原因在 <title> 里（见 viewerbackend.cpp 的同名处理）。
        // 日志留原文便于排查，但给用户的必须是那句人话 —— 否则界面只有"HTTP 401"。
        QString why = QString::fromUtf8(raw);
        const int t1 = why.indexOf(QLatin1String("<title>"));
        if (t1 >= 0) {
            const int t2 = why.indexOf(QLatin1String("</title>"), t1);
            if (t2 > t1) why = why.mid(t1 + 7, t2 - t1 - 7).trimmed();
        } else {
            why = QString::fromUtf8(raw).left(200).simplified();
        }
        logf("[oauth] FAIL 换令牌失败（HTTP %d）：%s", status, raw.left(512).constData());
        m_busy = false;
        closeLoginWindow();
        emit failed(QStringLiteral("换网站会话令牌失败（站点返回 HTTP %1%2）")
                        .arg(status)
                        .arg(why.isEmpty() ? QString() : QStringLiteral("：%1").arg(why)));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QJsonObject obj = doc.object();
    // 站点故意同时给 access_token 与 token 两个键（同一张票，为兼容早已发出的桌面端）。
    // ⚠️ 两个都认，且**以标准键优先**：以前只读 token，站点哪天回归标准 OAuth2
    //    只发 access_token，管理端就会当场判"没拿到令牌"——登录全流程走完却登不进去。
    QString sessionToken = obj.value(QStringLiteral("token")).toString();
    if (sessionToken.isEmpty()) sessionToken = obj.value(QStringLiteral("access_token")).toString();
    if (sessionToken.isEmpty()) {
        const QString why = QStringLiteral("换回来的响应里没有 token（站点改了 /oauth/token 的返回？）");
        logf("[oauth] FAIL %s：%s", why.toUtf8().constData(), raw.left(512).constData());
        m_busy = false;
        closeLoginWindow();
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
    // 收口一律连带停超时器：stopServer() 是所有成功/失败路径的必经之地，
    // 挂在这儿就不会出现"已经登录完了、超时器还在跑，3 分钟后突然报一句超时"。
    if (m_timeout) m_timeout->stop();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}
