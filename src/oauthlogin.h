// 星集控 · 管理端「网站账号登录」（OAuth2 授权码 + loopback 回拨）
//
// 要解决的问题：管理端原先靠一个写死在 viewer.env 里的**静态令牌**连云端。
// 三处别扭：① 换人得改配置文件；② 令牌只知道"对不对"，不知道"是谁在连"；
// ③ 密钥一旦流出去就是永久泄露，只能换整根令牌。
//
// 现在：拉起本机默认浏览器打开站点的 /oauth/authorize（该站点当 IdP），
// 用户用网站账号登录、点同意 → 站点回拨到**本机临时端口**，这里接住 code，
// 拿它去 /oauth/token 换一张「网站会话令牌」（30 天）存本机。
// 之后所有云端接入都走这张会话令牌换来的接入票。
//
// 为什么不内嵌 WebEngineView：管理端已经有了一个离屏 QWebEngineView（WebRTC 收流页），
// 再开一个 Edid/WebEngineContext 只为登录，等于给打包清单多加一整套 Chromium 交互面，
// 而授权页本来就长在浏览器里 —— 直接开浏览器反而少一层。
//
// 回拨为什么用 loopback 端口而不是固定端口：站点对 xingjikong_native 这一档客户端
// 的回拨地址只要求 http://127.0.0.1 且路径以 /oauth-callback 结尾（见站点
// lib/server/oauth-clients.ts 的 validateRedirectUri），端口随便挑。
// 用系统分配的临时端口就不会和机器上别的软件抢，也不用在配置文件里写死一个端口号。

#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * 一次「网站账号登录」的完整往返。
 *
 * 只负责一件事：**把浏览器里登录完的结果拿回来**（网站会话令牌）。
 * 拿会话令牌之后去站点换云端接入票，是 ViewerBackend 的活 —— 那边才管云端凭据。
 *
 * 信号只发成功/失败两个，中间过程一律写日志（走全局 logf，落到 viewer-run.log），
 * 界面不需要知道"开了什么网址、回拨落在哪个端口"这种细节。
 */
class OAuthLogin : public QObject
{
    Q_OBJECT

public:
    explicit OAuthLogin(QObject *parent = nullptr);
    ~OAuthLogin() override;

    /**
     * 发起一次登录：起本机临时端口 → 开浏览器 → 在 onReadyRead 里接住回拨 → 换令牌。
     *
     * 配置从环境变量读（同源的 viewer.env 那套，双击即用）：
     *   STE_SITE_URL              站点首页地址（必填，如 https://www.245959623.xyz）
     *   STE_OAUTH_CLIENT_ID       默认 xingjikong_native
     *   STE_OAUTH_CLIENT_SECRET   该客户端的密钥（等价于原来 viewer.env 里的静态令牌，同样落在本地磁盘）
     *   STE_OAUTH_REDIRECT_PATH   默认 /oauth-callback
     */
    void begin();

    /** 登录流程进行中（界面可以据此把按钮置灰）。 */
    bool busy() const { return m_busy; }

signals:
    /** 换到网站会话令牌。expiresInSec 为 0 表示没给有效期（按 30 天处理）。 */
    void succeeded(const QString &sessionToken, qint64 expiresInSec);
    /** 任何一步失败：原因原文带出（已写日志，这里主要给界面提示）。 */
    void failed(const QString &reason);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onTokenFinished();

private:
    /** 回一句 HTTP 给浏览器（登录成功后那张"可以关掉这个标签页"的提示页）。 */
    void servePage(QTcpSocket *sock, int code, const QByteArray &body);
    /** 收尾：无论成败都要停端口、落状态，不许留个监听在那儿。 */
    void stopServer();

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTcpSocket *m_sock = nullptr;      // 只接第一路回拨就够，剩下的直接拒
    QString m_state;                   // CSRF：回拨回来对不上就判为无效
    QString m_redirectUri;
    QString m_siteUrl;
    QString m_clientId;
    QString m_clientSecret;
    bool m_busy = false;
};
