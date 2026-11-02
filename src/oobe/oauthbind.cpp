// 星璃账号绑定实现（骨架沿用管理端 oauthlogin.cpp 的 loopback OAuth，
// 尾部多一步「拿网站会话令牌换设备接入票」，接口定义见 oauthbind.h）。

#include "oauthbind.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>     // 绑定前的 TLS/网络自检（2026-10-13）
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <cstdlib>
#include <ctime>
#include <QRandomGenerator>   // state 随机串（密码学安全，替掉 std::rand）

namespace {

/** 站点里登记的「星集控被控端（账号绑定）」公开客户端（oauth-clients.ts 的 BUILTIN）。 */
constexpr const char *kDeviceClientId = "xjk_device_public";

/** 回拨路径。站点按 RFC 8252 只认 127.0.0.1 回拨，路径后缀任意，这里与管理端保持一致。 */
constexpr const char *kRedirectPath = "/oauth-callback";

/** 随机串（state 用）：state 能被猜就等于别人能伪造回拨把 code 骗走。
 *
 * ⚠️ 2026-10-13：原来用 std::rand()（非密码学安全，且 srand 种子含秒级 time），
 *    猜中 state 就等于能伪造 OAuth 回拨把授权码骗走。改走 QRandomGenerator::system()
 *    （Windows 上走 BCryptGenRandom，密码学安全）。同款 bug 在管理端 oauthlogin.cpp
 *    也有一份，见被控端代码质量报告 A7。
 */
QString randomHex(int bytes)
{
    QString out;
    out.reserve(bytes * 2);
    const char digits[] = "0123456789abcdef";
    for (int i = 0; i < bytes; ++i) {
        const quint32 v = QRandomGenerator::system()->generate();
        out += digits[v & 0xF];
        out += digits[(v >> 4) & 0xF];
    }
    return out;
}

QString fromEnv(const char *key)
{
    return QString::fromUtf8(qgetenv(key)).trimmed();
}

} // namespace

QString computeFingerprint()
{
    const QString raw = QStringLiteral("%1|%2|%3|%4")
        .arg(QHostInfo::localHostName(),
             QSysInfo::currentCpuArchitecture(),
             QSysInfo::kernelVersion(),
             QSysInfo::machineUniqueId().toHex());
    const QByteArray hash = QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256)
        .toHex().left(32);
    return QString::fromLatin1(hash).toLower();
}

OAuthBind::OAuthBind(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

OAuthBind::~OAuthBind()
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

void OAuthBind::begin(const QString &siteBase)
{
    if (m_busy) {
        qInfo().noquote() << QStringLiteral("[oauthbind] 上一次绑定还没走完，忽略本次（反复出现即为状态未复位，属 bug）");
        return;
    }

    // 站点地址由调用方给（生产站点是 www. 子域，从云端 ws:// 地址推导不出来）。
    // 没给就自己按环境变量兜一个 —— 走 main.cpp 的正常路径一定传了值。
    if (siteBase.trimmed().isEmpty())
        m_siteUrl = fromEnv("STE_QT_SITE_URL");
    else
        m_siteUrl = siteBase.trimmed();
    m_clientId = fromEnv("STE_OAUTH_CLIENT_ID").isEmpty()
        ? QString::fromUtf8(kDeviceClientId) : fromEnv("STE_OAUTH_CLIENT_ID");

    qInfo().noquote() << QStringLiteral("[oauthbind] 站点=%1 客户端=%2（公开客户端，不带密钥）")
                         .arg(m_siteUrl, m_clientId);

    // ⚠️ 2026-10-13：绑定前先自报**网络/TLS 能力**。
    //    教室里这类失败最常见的三种是「TLS 后端不可用」「系统代理拦掉」「DNS 不通」，
    //    而 QNetworkAccessManager 三者都只表现成一个 HTTP 0（状态码缺席），
    //    现场与远端都判不出是哪一种 —— 这行日志一次把三样都摊开。
    qInfo().noquote() << QStringLiteral("[oauthbind] 自检：TLS 可用=%1 运行时=%2 后端=%3 应用代理类型=%4")
                             .arg(QSslSocket::supportsSsl() ? QStringLiteral("是") : QStringLiteral("否"),
                                  QSslSocket::sslLibraryVersionString().isEmpty()
                                      ? QStringLiteral("(未加载)") : QSslSocket::sslLibraryVersionString(),
                                  QSslSocket::availableBackends().join(QLatin1Char(',')),
                                  QString::number(static_cast<int>(QNetworkProxy::applicationProxy().type())));

    if (!m_siteUrl.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
        const QString why = QStringLiteral("站点地址不是 http/https 开头（当前：%1）").arg(m_siteUrl);
        qWarning().noquote() << QStringLiteral("[oauthbind] 失败：%1").arg(why);
        m_busy = false;
        emit failed(why);
        return;
    }

    m_state = randomHex(16);

    // 端口 0 = 让系统分配空闲临时端口；站点只认 127.0.0.1，端口随便。
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        const QString why = QStringLiteral("本机临时端口开不出来（%1），可能是安全软件拦了回拨。"
                                           "请放行 127.0.0.1 的临时端口。").arg(m_server->errorString());
        qWarning().noquote() << QStringLiteral("[oauthbind] 失败：%1").arg(why);
        stopServer();
        m_busy = false;
        emit failed(why);
        return;
    }

    const int port = m_server->serverPort();
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(QString::fromUtf8(kRedirectPath));
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

    QObject::connect(m_server, &QTcpServer::newConnection, this, &OAuthBind::onNewConnection);

    qInfo().noquote() << QStringLiteral("[oauthbind] 正在打开授权页（回拨 %1）…").arg(m_redirectUri);
    // 被控端是常驻托盘程序：教室机上不一定有能用的默认浏览器（Edge 策略、装机精简），
    // 打不开时不能只是「点了个没反应的按钮」——把地址给用户，让人自己粘到浏览器里。
    if (!QDesktopServices::openUrl(authUrl)) {
        const QString why = QStringLiteral("打不开浏览器（请把下面这行地址复制到浏览器打开）：");
        qWarning().noquote() << QStringLiteral("[oauthbind] 失败：%1 %2").arg(why, authUrl.toString());
        m_busy = false;
        stopServer();
        emit failed(why + QLatin1Char('\n') + authUrl.toString());
        return;
    }
}

void OAuthBind::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        if (m_sock) {   // 只接第一路（站点回拨的那一路），多余的关掉别让它挂着
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->flush();
            s->deleteLater();
            continue;
        }
        m_sock = s;
        QObject::connect(s, &QTcpSocket::readyRead, this, &OAuthBind::onReadyRead);
        QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
    }
}

void OAuthBind::onReadyRead()
{
    QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;

    m_pending += s->readAll();
    if (!m_pending.contains("\r\n\r\n")) return;   // 请求头没读全（GET 无 body）

    const QString head = QString::fromLatin1(m_pending.left(m_pending.indexOf("\r\n\r\n")));
    const QStringList lines = head.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    m_pending.clear();

    if (lines.isEmpty()) {
        s->deleteLater();
        m_sock = nullptr;
        stopServer();
        m_busy = false;
        return;
    }

    const QString requestLine = lines.first().trimmed();
    // ⚠️ 请求行形如 `GET /oauth-callback?code=…&state=… HTTP/1.1`。
    // 取中间那段（方法/目标/协议三段切）：老写法把行尾 " HTTP/1.1" 一起带进 query，
    // state 会被解析成 "<hex> HTTP/1.1" ⇒ 与发出的值永不相等 —— 症状是"回拨校验不过"。
    const QString target = requestLine.section(QLatin1Char(' '), 1, 1);
    const QString path = target.left(target.indexOf(QLatin1Char('?')));
    const QUrlQuery qq(target.mid(target.indexOf(QLatin1Char('?')) + 1));

    const bool pathOk = path.startsWith(QLatin1Char('/'))
                        && path.endsWith(QString::fromUtf8(kRedirectPath));
    const QString gotState = qq.queryItemValue(QStringLiteral("state"));
    const QString code = qq.queryItemValue(QStringLiteral("code"));

    if (!pathOk || gotState != m_state || code.isEmpty()) {
        // 回拨对不上 state 一律按失败：可能是别人点到我们端口（CSRF 面）
        qInfo().noquote() << QStringLiteral("[oauthbind] 回拨被拒：path=%1 state匹配=%2 带code=%3")
                             .arg(path, gotState == m_state ? QStringLiteral("是") : QStringLiteral("否"),
                                  code.isEmpty() ? QStringLiteral("否") : QStringLiteral("是"));
        servePage(s, 400, QStringLiteral("<h2>绑定未完成</h2><p>回拨的地址或状态对不上，已拒绝。"
                                         "请回到被控端的绑定窗口再点一次。</p>").toUtf8());
        s->disconnectFromHost();
        m_sock = nullptr;
        stopServer();
        m_busy = false;   // 用户在授权页点「取消」也走这里：不复位的话按钮就一直灰着
        emit failed(QStringLiteral("授权回拨校验没过（地址不对或状态不匹配），请重新绑定"));
        return;
    }

    // 先给人一个交代（浏览器还开在授权页上），再去换令牌
    servePage(s, 200, QStringLiteral("<h2>授权成功</h2><p>正在申请设备接入票，请稍候…</p>").toUtf8());
    s->disconnectFromHost();
    m_sock = nullptr;
    stopServer();

    // 从这里起就**不再监听浏览器窗口**：用户的授权流程已经走完（或者他在授权页点了取消，
    // 那也是他自己的事）。这里再挂任何"关窗＝取消"的回调用途都只会误伤——
    // 绑定早就该收尾了，收尾统一走 release()。成功/失败都从后续两步的网络回调里发。

    qInfo().noquote() << QStringLiteral("[oauthbind] 回拨接住，正在换网站会话令牌…");

    QUrl tokenUrl(QStringLiteral("%1/oauth/token").arg(m_siteUrl));
    // 公开客户端（xjk_device_public）**整个不带** client_secret：
    // 站点那条 PUBLIC 分支判的是「参数不存在」，带一个空串过去反而会被判成"带错 secret"。
    //
    // ⚠️ 2026-10-25（Win7 现场排障）：必须发 **application/json**，不能发
    //   x-www-form-urlencoded。站点（SvelteKit）的 csrf.checkOrigin 对 **form 类
    //   Content-Type** 的 POST 校验 Origin 头，而 QNAM 不发 Origin ⇒ 站点直接
    //   403 "Cross-site POST form submissions are forbidden"（Win7 现场日志同款；
    //   已对生产站点 curl 对照实证：form→403、JSON→直达业务逻辑）。
    //   SvelteKit 的 is_form_content_type 只认 form/multipart/text-plain，
    //   JSON 不触发该校验。站点 /oauth/token 的 toParams 同时支持两种形态
    //   （见 +server.ts），改 JSON 无需站点配合。
    QJsonObject body;
    body.insert(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.insert(QStringLiteral("client_id"), m_clientId);
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("redirect_uri"), m_redirectUri);

    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, &OAuthBind::onTokenFinished);
}

void OAuthBind::onTokenFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    // ⚠️ 2026-10-13：`HttpStatusCodeAttribute` 在**传输层失败**时是**无效 QVariant**，
    //    `toInt()` 得到的就是 0 —— 于是提示写成「站点返回 HTTP 0」，而真实原因
    //    （DNS 解析不了 / 连接被拒 / TLS 握手失败 / 被代理或安全软件拦掉）只在
    //    `error()` / `errorString()` 里，被旧代码整段丢掉，现场无从下手。
    //    现场反馈「远端提示：换网站登录态失败站点返回 http 0」走的正是这条路径。
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrStr = reply->errorString();
    m_tokenStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_tokenDone = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    if (m_tokenStatus != 200) {
        // status==0 ⇒ 一个字节的 HTTP 响应都没收到；否则是站点确实回了非 200。
        const bool noResponse = (m_tokenStatus == 0);
        qWarning().noquote()
            << QStringLiteral("[oauthbind] 换站点令牌失败（HTTP %1，networkError=%2：%3）站点=%4 响应=%5")
                   .arg(m_tokenStatus)
                   .arg(static_cast<int>(netErr))
                   .arg(netErrStr, m_siteUrl, m_tokenDone.left(512));
        release();
        emit failed(noResponse
                        ? QStringLiteral("连不上站点 %1（网络层就没通：%2）。"
                                         "请先在这台机器上打开 %1 —— 打不开就是 DNS / 代理 / 防火墙的问题，"
                                         "能打开再把这条报给管理员。")
                              .arg(m_siteUrl, netErrStr)
                        : QStringLiteral("换网站登录态失败（站点返回 HTTP %1）").arg(m_tokenStatus));
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(m_tokenDone.toUtf8()).object();
    const QString sessionToken = obj.value(QStringLiteral("token")).toString();
    if (sessionToken.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[oauthbind] 换回来的响应里没有 token：%1").arg(m_tokenDone.left(512));
        release();
        emit failed(QStringLiteral("换回来的登录态里没有 token（站点改了 /oauth/token？）"));
        return;
    }

    qInfo().noquote() << QStringLiteral("[oauthbind] 换到网站登录态，正在申请设备接入票…");

    // 设备票：站点 /api/device/bind?fp=…（登录态走 Bearer）
    //   · fp 必填，8-64 位小写字母数字，格式不对站点直接 400
    //   · uid 可选：带上这台机器现有的设备码（没配就落回电脑名），站点照它签票，
    //     否则站点自己生成 user<id>-dev<fp8>。带错了没关系——只要和写进 agent.env 的一致，
    //     云端 register 时 uid 必须与票里的一致，不一致会被 BAD_PAYLOAD 拒。
    QUrl bindUrl(QStringLiteral("%1/api/device/bind").arg(m_siteUrl));
    QUrlQuery bq;
    bq.addQueryItem(QStringLiteral("fp"), computeFingerprint());
    const QString uid = fromEnv("STE_QT_UID").isEmpty()
        ? QHostInfo::localHostName() : fromEnv("STE_QT_UID");
    bq.addQueryItem(QStringLiteral("uid"), uid);
    bindUrl.setQuery(bq);

    QNetworkRequest breq(bindUrl);
    breq.setRawHeader("Authorization",
                      QStringLiteral("Bearer %1").arg(sessionToken).toUtf8());
    QNetworkReply *reply2 = m_nam->get(breq);
    QObject::connect(reply2, &QNetworkReply::finished, this, &OAuthBind::onBindFinished);
}

void OAuthBind::onBindFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    // 同 onTokenFinished：HTTP 0 ＝ 没拿到响应，真因只在 error()/errorString() 里（2026-10-13）。
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrStr = reply->errorString();
    m_bindStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_bindDone = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    if (m_bindStatus != 200) {
        const bool noResponse = (m_bindStatus == 0);
        qWarning().noquote()
            << QStringLiteral("[oauthbind] 申请设备票失败（HTTP %1，networkError=%2：%3）站点=%4 响应=%5")
                   .arg(m_bindStatus)
                   .arg(static_cast<int>(netErr))
                   .arg(netErrStr, m_siteUrl, m_bindDone.left(512));
        release();
        // 站点在 401/503 时给的是人话（"登录会话无效或已过期" / "云端票据密钥未配置"），
        // 别用 HTTP 码糊过去——运维看不懂 503 到底要改什么。
        const QJsonObject obj = QJsonDocument::fromJson(m_bindDone.toUtf8()).object();
        const QString why = obj.value(QStringLiteral("error")).toString();
        if (noResponse) {
            emit failed(QStringLiteral("连不上站点 %1（网络层就没通：%2）").arg(m_siteUrl, netErrStr));
        } else {
            emit failed(why.isEmpty()
                        ? QStringLiteral("申请设备接入票失败（站点返回 HTTP %1）").arg(m_bindStatus)
                        : why);
        }
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(m_bindDone.toUtf8()).object();
    if (!obj.value(QStringLiteral("ok")).toBool()) {
        const QString why = obj.value(QStringLiteral("error")).toString();
        qWarning().noquote() << QStringLiteral("[oauthbind] 设备票申请被拒：%1").arg(m_bindDone.left(512));
        release();
        emit failed(why.isEmpty() ? QStringLiteral("站点没有返回 ok（设备绑定通道关闭？）") : why);
        return;
    }

    const QString ticket = obj.value(QStringLiteral("ticket")).toString();
    const QString uid = obj.value(QStringLiteral("uid")).toString();
    const QString owner = obj.value(QStringLiteral("owner")).toString();
    const QString siteWs = obj.value(QStringLiteral("wsUrl")).toString();

    if (ticket.isEmpty() || uid.isEmpty()) {
        qWarning().noquote() << QStringLiteral("[oauthbind] 设备票响应缺字段：%1").arg(m_bindDone.left(512));
        release();
        emit failed(QStringLiteral("设备票响应里没有票或设备码（站点版本过旧？）"));
        return;
    }

    // 站点给了云端地址就用它的（生产部署以站点 PUBLIC_CLOUD_WS_URL 为准）；
    // 没给就退回本机已有配置——宁可用旧地址，也不要把用户配好的地址清空。
    const QString wsUrl = siteWs.trimmed().isEmpty()
        ? fromEnv("STE_QT_WS_URL") : siteWs.trimmed();
    if (wsUrl.isEmpty()) {
        release();
        emit failed(QStringLiteral("站点没给云端地址、本机也没配（wsUrl 缺失，无法写入配置）"));
        return;
    }

    qInfo().noquote() << QStringLiteral("[oauthbind] ✅ 绑定成功：设备 %1（%2）").arg(uid, owner.isEmpty() ? QStringLiteral("未署名") : owner);

    release();   // 先复位 busy 再发信号，否则界面收到成功时按钮还是灰的
    emit succeeded(wsUrl, ticket, uid, owner);
}

void OAuthBind::servePage(QTcpSocket *sock, int code, const QByteArray &body)
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

void OAuthBind::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void OAuthBind::release()
{
    stopServer();
    if (m_sock) {
        m_sock->disconnectFromHost();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_pending.clear();
    m_busy = false;
}
