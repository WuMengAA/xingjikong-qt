// 星集控 · 管理端「网站账号登录」（OAuth2 授权码 + loopback 回拨）
//
// 要解决的问题：管理端原先靠一个写死在 viewer.env 里的**静态令牌**连云端。
// 三处别扭：① 换人得改配置文件；② 令牌只知道"对不对"，不知道"是谁在连"；
// ③ 密钥一旦流出去就是永久泄露，只能换整根令牌。
//
// 现在：打开站点的 /oauth/authorize（该站点当 IdP），用户用网站账号登录、点同意
// → 站点回拨到**本机临时端口**，这里接住 code，拿它去 /oauth/token 换一张
// 「网站会话令牌」（30 天）存本机。之后所有云端接入都走这张会话令牌换来的接入票。
//
// 打开授权页的三层兜底（2026-10-06 改，起因是用户真机上报「登录调用浏览器失败」）：
//   ① 内嵌窗口（QWebEngineView）—— **首选**。
//      "打不开浏览器"在教室机上是常见故障：没设默认浏览器 / 关联被安全软件改坏 /
//      程序以管理员身份跑而浏览器起不来 —— 这些情况下 QDesktopServices::openUrl
//      一律返回 false，用户就彻底登不进去，连个可复制的网址都没有。
//      管理端进程里**本来就在跑**一个离屏 QWebEngineView（WebRTC 收流页），
//      Chromium 上下文是现成的，内嵌不额外引入任何依赖。
//   ② QDesktopServices::openUrl（内嵌起不来才回落系统浏览器）。
//   ③ 两条都不通 → failed() 的原因里带上授权网址，让用户自己粘到地址栏。
//
// ⚠️ 顺带修的两个"失败一次就再也点不动"的坑（同一天）：
//   begin() 里 m_busy 置 true 之后，openUrl 失败与回拨校验失败这两条路径**都没复位**，
//   于是第二次点登录会被开头那句"上一次还没走完"直接挡掉，而且**不发任何信号** ——
//   界面上表现为按钮点了没反应、也不报错。现在所有退出路径一律复位。
//
// 回拨为什么用 loopback 端口而不是固定端口：站点对 xingjikong_native 这一档客户端
// 的回拨地址只要求 http://127.0.0.1 且路径以 /oauth-callback 结尾（见站点
// lib/server/oauth-clients.ts 的 validateRedirectUri），端口随便挑。
// 用系统分配的临时端口就不会和机器上别的软件抢，也不用在配置文件里写死一个端口号。
// 内嵌窗口访问 127.0.0.1:<临时端口> 与系统浏览器完全一样，回拨逻辑一行不用改。

#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QNetworkAccessManager;
class QNetworkReply;
class QWidget;      // 内嵌登录窗：只在 .cpp 里 include WebEngine，头文件保持干净
class QUrl;

/**
 * 一次「网站账号登录」的完整往返。
 *
 * 只负责一件事：**把登录完的结果拿回来**（网站会话令牌）。
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
     * 发起一次登录：起本机临时端口 → 打开授权页 → 在 onReadyRead 里接住回拨 → 换令牌。
     *
     * 配置从环境变量读（同源的 viewer.env 那套，双击即用）：
     *   STE_SITE_URL              站点首页地址（必填，如 https://www.245959623.xyz）
     *   STE_OAUTH_CLIENT_ID       默认 xingjikong_native
     *   STE_OAUTH_CLIENT_SECRET   该客户端的密钥（等价于原来 viewer.env 里的静态令牌，同样落在本地磁盘）
     *   STE_OAUTH_REDIRECT_PATH   默认 /oauth-callback
     *   STE_OAUTH_EMBEDDED        设为 0 则**不用**内嵌窗口、直接开系统浏览器（排障用）
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
    /** 收尾：关掉内嵌登录窗（成功 / 失败 / 用户自己关窗都走它）。 */
    void closeLoginWindow();

    /** 打开授权页：内嵌窗口优先，回落系统浏览器，都失败返回 false。 */
    bool openAuthUrl(const QUrl &authUrl);
    /** 开一个内嵌 WebEngine 窗口加载 url；起不来返回 false（调用方据此回落）。 */
    bool openEmbeddedWindow(const QUrl &url);

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTcpSocket *m_sock = nullptr;      // 只接第一路回拨就够，剩下的直接拒
    QByteArray m_pending;          // 本次回拨收到的 HTTP 请求原文（成员，不是 static）
    QWidget *m_loginWin = nullptr;     // 内嵌登录窗（QWebEngineView 的宿主）
    QString m_state;                   // CSRF：回拨回来对不上就判为无效
    QString m_redirectUri;
    QString m_siteUrl;
    QString m_clientId;
    QString m_clientSecret;
    bool m_busy = false;
};
