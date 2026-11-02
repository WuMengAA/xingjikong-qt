// 星集控被控端 · 星璃账号绑定（2026-10-06 设计，2026-10-07 重建）
//
// 流程（零手填接入）：点「用星璃账号绑定」
//   → 起本机临时端口 + 打开站点授权页（OAuth2 授权码，loopback 回拨）
//   → 用户星璃账号登录 + 点同意 → 回拨 127.0.0.1:<port>/oauth-callback?code&state
//   → 用 code 换网站会话令牌（公开客户端，不带 client_secret）
//   → 拿会话令牌访问站点 /api/device/bind?fp=… 换一张「设备接入票」
//   → emit succeeded(wsUrl, ticket, uid, owner) 交给 main.cpp 写进 agent.env 并立刻重连。
//
// 为什么需要 /api/device/bind 这一步：换会话令牌要登录态，而设备自己没有账号，
// 所以退一步用「人已经登录过的那次授权」换来的令牌去签设备票（RFC 8252 的 native app 套路）。
// 设备票格式 `v1.<exp>.<uid>.<fp>.<nonce>.<owner>.<sig>`（7 段），云端验票时还会拿
// register 里带的 fp 比对，并对号入座 uid —— 拿 A 机的票注册成 B 的 uid 会被直接拒。
//
// 与 viewer 端 oauthlogin.cpp 的关系：同一套 loopback OAuth 骨架，
// 差别只在最后一步——管理端拿会话令牌直接用，被控端拿它再去换一张设备票。

#ifndef STELARITH_OAUTHBIND_H
#define STELARITH_OAUTHBIND_H

#include <QObject>
#include <QString>

class QByteArray;
class QTcpServer;
class QTcpSocket;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * 设备指纹：绑定时算一次给站点（签进设备票），接入时 register 里也带这个字段。
 *
 * 两处**必须是同一个值**：站点按它签票、云端按它验票，对不上直接拒连接。
 * 所以统一由这里出，main.cpp 不再自己拼一遍（以前就是两套写法，改一处漏一处）。
 */
QString computeFingerprint();

/** 站点基址由 main.cpp 的 siteBaseFromWsUrl() 定（唯一真源，见 main.cpp）。
 *  ⚠️ 这里**故意不声明**那个函数：main.cpp 把它放在匿名命名空间里，
 *   若 oauthbind.h 再声明一个同签名全局函数，main.cpp 里的调用点会变成
 *   `C2668 对重载函数的调用不明确`。所以绑定流程改由调用方把站点地址传进来（见 begin()）。 */

class OAuthBind : public QObject
{
    Q_OBJECT
public:
    explicit OAuthBind(QObject *parent = nullptr);
    ~OAuthBind() override;

    /** 绑定流程是否进行中（main.cpp 用它拦「连点两次」）。 */
    bool busy() const { return m_busy; }

    /**
     * 开始绑定：开临时端口 → 开浏览器 → 等回拨。
     * 结果一律走信号：成功 succeeded(wsUrl, ticket, uid, owner)，失败 failed(原因)。
     * @param siteBase 站点基址（由 main.cpp 的 siteBaseFromWsUrl() 给）；
     *                 留空则由 OAuthBind 自己按环境变量兜一个默认值（只在不走 main.cpp 时用）。
     */
    void begin(const QString &siteBase = QString());

signals:
    // 绑定成功：wsUrl=云端地址，ticket=设备接入票，uid=站点签的设备码，owner=绑定者用户名
    void succeeded(const QString &wsUrl, const QString &ticket, const QString &uid, const QString &owner);
    // 绑定失败：reason 是**能照着做点什么**的原因（不是「失败」两个字）
    void failed(const QString &reason);

private:
    void onNewConnection();
    void onReadyRead();
    void onTokenFinished();
    void onBindFinished();

    void servePage(QTcpSocket *sock, int code, const QByteArray &body);
    void stopServer();
    /** 收尾：停端口 + 清连接 + 复位 busy（**所有**出口都必须走到这里，否则界面会卡在「正在打开浏览器」）。 */
    void release();

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_pending;              // 一次登录一块（⚠️ 绝不能用函数级 static，会串味）
    QNetworkAccessManager *m_nam = nullptr;
    int m_tokenStatus = 0;
    int m_bindStatus = 0;
    QString m_tokenDone;               // 换令牌回来的原文（出错时给人看）
    QString m_bindDone;                // 换设备票回来的原文
    QString m_state;
    QString m_redirectUri;
    QString m_clientId;
    QString m_siteUrl;
    bool m_busy = false;
};

#endif // STELARITH_OAUTHBIND_H
