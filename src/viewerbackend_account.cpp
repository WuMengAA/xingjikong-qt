// 星集控 · ViewerBackend 站点账号域（OAuth 一户通 + 云端接入票，2026-10-13 架构拆分，审计 2.1）
//
// 为什么拆出来：viewerbackend.cpp 曾 3573 行（C1060）。账号域（登录 / 换票 / 凭据
// 落盘 / 到期巡检）是内聚但**与连接模块相交**的一块：换到票后要 connectToCloud()、
// 退出登录要关 m_ws 并清设备表。做法：同 header 的 ViewerBackend 成员跨编译单元
// 实现（QObject 合法），私有成员和方法（connectToCloud / sendAction / setStatus /
// m_ws / m_devices…）在另一 TU 里照常可达 —— 行为零改动，只是源码分块。
//
// 依赖：OAuthLogin（oauthlogin.h）、QNetwork*（m_nam 已在构造创建，见审计 2.9）、
// accountFilePath()（%LOCALAPPDATA%/Stelarith Viewer/account.json）。

#include "viewerbackend.h"
#include "permission_manager.h"   // 2026-10-26：登出要复位权限镜像（m_perms->setFullAccess/reset），
                                  // viewerbackend.h 里只有前向声明，本 TU 必须自己引全类型
#include "oauthlogin.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
// 2026-10-13（WorkBuddy 代补，编译修复）：198 行 m_ws->close() 用到 QWebSocket 完整类型，
// 此前只前向声明（viewerbackend.h:81）—— C2027。不碰逻辑，仅补 include。
#include <QWebSocket>

// 凭据落盘位置：%LOCALAPPDATA%/Stelarith Viewer/account.json（程序名由 main.cpp 钉死，
// 所以这个路径是可预期的；不是 exe 同目录 —— 绿色包会被覆盖、重装也会丢，
// 把令牌跟程序文件放一起还容易被整文件夹拷走）。
// JSON 里只有四个字段：会话令牌、接入票、票到期时刻、登录账号。
// 强度与原来 viewer.env 里的静态令牌**同档**（都是本机明文），但多一层"是谁在连"的账号。
QString ViewerBackend::accountText() const { return m_accountText; }
bool ViewerBackend::accountBusy() const { return m_accountBusy; }
static QByteArray accountFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::currentPath();
    return (dir + QStringLiteral("/account.json")).toLocal8Bit();
}
void ViewerBackend::loadAccount()
{
    const QByteArray path = accountFilePath();
    QFile f(QString::fromLocal8Bit(path));
    // 没存过是最常见的情况（第一次在这台机器登录），不算错，但要留一行 ——
    // 否则"读不到凭据"和"压根没登录"在日志里长得一模一样，排障时只能靠猜。
    if (!f.open(QIODevice::ReadOnly)) {
        logf("[account] 没读到本机凭据（首次登录属正常）：%s", path.constData());
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject o = doc.object();
    m_sessionToken = o.value(QStringLiteral("session")).toString();
    m_cloudTicket = o.value(QStringLiteral("ticket")).toString();
    m_ticketExp = (qint64)o.value(QStringLiteral("exp")).toDouble();
    m_accountUser = o.value(QStringLiteral("user")).toString();
    const QString expTxt = m_ticketExp > 0
        ? QStringLiteral("，到期 %1")
              .arg(QDateTime::fromMSecsSinceEpoch(m_ticketExp).toString(QStringLiteral("MM-dd HH:mm")))
        : QStringLiteral("，到期时间没记");
    logf("[account] 读到本机凭据：账号=%s 接入票 %d 个字符%ls",
         m_accountUser.isEmpty() ? "(没记)" : m_accountUser.toUtf8().constData(),
         m_cloudTicket.size(), expTxt.constData());
}
void ViewerBackend::saveAccount()
{
    // 凭据目录不存在就建一个：AppLocalDataLocation 在干净机器上可能还没生成过。
    QDir().mkpath(QFileInfo(QString::fromLocal8Bit(accountFilePath())).path());
    QJsonObject o;
    o.insert(QStringLiteral("session"), m_sessionToken);
    o.insert(QStringLiteral("ticket"), m_cloudTicket);
    o.insert(QStringLiteral("exp"), (double)m_ticketExp);
    o.insert(QStringLiteral("user"), m_accountUser);
    QFile f(QString::fromLocal8Bit(accountFilePath()));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logf("[account] WARN 凭据写不进 %s（登录状态会每次重启都要重登）",
             accountFilePath().constData());
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}
void ViewerBackend::clearAccount()
{
    m_sessionToken.clear();
    m_cloudTicket.clear();
    m_ticketExp = 0;
    m_accountUser.clear();
    m_accountFatal = false;
    m_verified = true;   // 退出后重置为默认（已认证），下次登录以站点回传为准
    QFile::remove(QString::fromLocal8Bit(accountFilePath()));
}
void ViewerBackend::refreshAccountText()
{
    QString s;
    if (m_accountBusy) {
        s = QStringLiteral("正在登录网站账号…");
    } else if (m_siteUrl.isEmpty()) {
        s = QStringLiteral("未配置站点地址（viewer.env 里加 STE_SITE_URL）");
    } else if (!m_cloudTicket.isEmpty() && ticketUsable()) {
        s = QStringLiteral("已登录：%1（云端接入票 %2 内有效）")
                .arg(m_accountUser.isEmpty() ? QStringLiteral("网站账号") : m_accountUser,
                     QDateTime::fromMSecsSinceEpoch(m_ticketExp).toString(QStringLiteral("MM-dd HH:mm")));
    } else if (m_accountFatal) {
        s = m_accountText;                 // 失败文案自己已经把原因写全了
    } else if (!m_sessionToken.isEmpty()) {
        s = QStringLiteral("网站会话过期，正在重新取票…");
    } else {
        s = QStringLiteral("未登录：点这里用网站账号登录");
    }
    // ⚠️ 2026-10-08：**无条件发信号**，不再"文案没变就不发"。
    //    accountChanged 不只驱动这一行文案：loggedIn / accountName / 顶栏胶囊、
    //    账户菜单里显示哪一组（登录组 / 退出组）全都挂在它身上。
    //    以前那句 `if (s != m_accountText)` 会漏掉这种情形：**退出登录**后
    //    文案恰好还是"未登录…"（没变），于是信号不发 ⇒ 菜单里的「退出登录」那组
    //    不消失 —— 用户看到的就是"点了退出登录，还是登录着"。
    m_accountText = s;
    emit accountChanged();
}
bool ViewerBackend::ticketUsable(quint64 soonMs) const
{
    if (m_cloudTicket.isEmpty() || m_ticketExp <= 0) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return now < m_ticketExp - (qint64)soonMs;
}
void ViewerBackend::pickTicketOrToken(QJsonObject &p) const
{
    // 手里有一张没过期的票就发票（云端 authorizeViewer 认 ticket）；
    // 一张都没有才退回原来那条静态令牌的路 —— 老机器/没配站点地址照样能连。
    if (!m_cloudTicket.isEmpty() && m_ticketExp > 0
        && QDateTime::currentMSecsSinceEpoch() < m_ticketExp) {
        p.insert(QStringLiteral("ticket"), m_cloudTicket);
        return;
    }
    p.insert(QStringLiteral("token"), m_token);
}
void ViewerBackend::loginWithSite()
{
    if (m_accountBusy) return;
    // 站点地址在 start() 里已经有默认值，这里兜的是"万一有人把它清掉"。
    // 清掉就退回老路（静态令牌）并如实说明，不要弹一句"没配站点地址"让用户去翻配置 ——
    // 站点地址是公开的，本来就不该让用户配。
    if (m_siteUrl.isEmpty()) {
        logf("[account] FAIL 站点地址为空，退回静态令牌（老机器未配 STE_SITE_URL）");
        m_accountFatal = true;
        refreshAccountText();
        return;
    }
    if (!m_oauth) {
        m_oauth = new OAuthLogin(this);
        QObject::connect(m_oauth, &OAuthLogin::succeeded,
                         this, &ViewerBackend::onOAuthSucceeded);
        QObject::connect(m_oauth, &OAuthLogin::failed, this, &ViewerBackend::onOAuthFailed);
    }
    m_accountBusy = true;
    m_accountFatal = false;
    refreshAccountText();
    m_oauth->begin();
}
void ViewerBackend::forgetAccount()
{
    clearAccount();
    // ⚠️ 2026-10-08：退出登录**不能只删凭据**。以前只做 clearAccount()，剩下两件事没人收：
    //   ① 云端连接还挂着（那张票虽然从本地删了，socket 还在）⇒ 界面继续显示设备表、
    //      继续能下指令，用户看到的是"点了退出登录，可它还在用" —— 这是"无法退出登录"最常见的形态；
    //   ② 重连逻辑还在转 ⇒ 过几秒又去敲门，日志里刷"鉴权失败"。
    //   两件都收掉，并把状态如实写清（"退出了但还连着"最容易让人以为按钮坏了）。
    m_retryStopped = true;
    m_authFailed = false;
    m_authFailReason.clear();
    // 设备表清空 + 当前画面作废：这些都是"上一个账号看到的东西"，不该留给下一个人。
    if (!m_devices.isEmpty()) {
        m_devices = QJsonArray();
        emit devicesChanged();
    }
    m_devLedger.clear();   // 离线台账也一起清（换账号后看到的应该是新账号的机器）
    if (!m_currentUid.isEmpty() || !m_frameUid.isEmpty()) {
        const QString prevUid = m_currentUid;
        m_currentUid.clear();
        m_frameUid.clear();
        m_frame = QImage();
        emit currentUidChanged();
        emit frameChanged();
        // 2026-10-11：选中即远控之后，这里（换账号/断连）也必须收掉远控，
        // 否则被控端一直挂着"被接管"状态、提示永不消失。
        if (m_remoteControlling && !prevUid.isEmpty()) {
            sendAction(QStringLiteral("remote_control_stop"), QJsonObject());
            logf("[viewer] 清空当前设备，收掉对 %s 的远控", prevUid.toUtf8().constData());
            m_remoteControlling = false;
            m_remoteControlUid.clear();
        }
    }
    // 静态令牌（viewer.env 里的 STE_VIEWER_TOKEN）也一并作废 —— 否则"退出登录"之后
    // hasAnyCredential() 仍为真、loggedIn() 仍为真，界面和菜单回到"已登录"那一组，
    // 用户会觉得这个按钮压根没生效。重启程序会重新从 viewer.env 读回来，不会真的丢配置。
    // ⚠️ 只在**配了站点地址**时才作废：老机器只靠静态令牌连云端，没有站点可登录；
    //    把令牌清掉等于把程序弄成永久的"没登录"，那才是真事故。
    const bool hadStaticToken = !m_token.isEmpty();
    if (!m_siteUrl.isEmpty()) m_token.clear();
    m_authed = false;
    emit authedChanged();
    if (m_ws) m_ws->close();

    // ── 2026-10-26：登出要把「上一个账号看到的班级」也收掉 ─────────────
    //   与设备表同一口径：上面清了 m_devices，但 m_classes 一直没清 ⇒ 退出后
    //   班级下拉还挂着上一个账号的班，点进去订阅的是别人的班（云端会拒，但界面
    //   先骗了一轮）。顺带把班级筛选复位到「全部班级」。
    if (!m_classes.isEmpty() || m_classesFiltered) {
        m_classes = QJsonArray();
        m_classesFiltered = false;
        emit classesChanged();
    }
    if (!m_currentClass.isEmpty()) m_currentClass.clear();

    // ── 2026-10-26：权限镜像与服务端身份锁，登出即解除 ────────────────
    //   · m_perms 只在 auth-ok 里被 setIdentity / setFullAccess，**登出没人复位**
    //     ⇒ 设备表会一直按**上一个账号**的 scope 裁剪（换人登录前永远看不到别人的班，
    //       换人登录后也要等下一次 auth-ok 才纠正）。
    //   · m_roleLocked 由 applyServerRole 置真，用来防「本地切回 admin 绕门控」；
    //     登出后连接已断、凭据已删，这把锁不该继续生效 —— 否则本机永久停在
    //     teacher（集控页永远隐藏，连切身份都切不动）。
    if (m_perms) {
        if (!m_token.isEmpty()) {
            m_perms->setFullAccess();   // 还有静态令牌 ⇒ 系统身份（运维视角，全量）
        } else {
            m_perms->reset();           // 什么凭据都没有 ⇒ 最低权限（不暴露任何数据）
        }
    }
    m_roleLocked = false;

    setStatus(QStringLiteral("已退出登录。点右上角账户 → 登录，可换一个网站账号"), false);
    logf("[account] 已忘记本机账号（凭据已删、云端连接已断开%s）",
         hadStaticToken ? "，静态令牌本次会话内一并作废（重启程序会从 viewer.env 读回）" : "");
    refreshAccountText();
    refreshGateState();   // 角色门控也跟着变（没凭据就按最低权限看）
}
void ViewerBackend::onOAuthSucceeded(const QString &sessionToken, qint64 expiresInSec)
{
    // 站点说这张会话 30 天；没给就按 30 天兜底，别签出无限期的会话。
    m_sessionToken = sessionToken;
    m_accountBusy = false;
    m_accountFatal = false;
    logf("[account] 网站账号登录成功（会话 %lld 天）", expiresInSec / 86400);
    ensureCloudTicket(true);
}
void ViewerBackend::onOAuthFailed(const QString &reason)
{
    m_accountBusy = false;
    m_accountFatal = true;
    m_accountText = QStringLiteral("登录失败：%1").arg(reason);
    logf("[account] FAIL %s", reason.toUtf8().constData());
    emit accountChanged();
}
/**
 * 把站点返回的错误正文变成**人话**。
 *
 * 站点（SvelteKit）出错时回的是**整张 HTML 错误页**，真正的原因在 <title> 里，
 * 例如「该账号没有查看集控的权限」「网站会话已失效，请在管理端重新登录」。
 * 原来这里直接把 raw.left(256) 拼进界面 —— 用户看到的是一屏 <!doctype html>… 标签，
 * 等于"登录失败，但不知道为什么、也不知道该找谁"。
 *
 * 取值顺序：HTML 的 <title> → JSON 的 message/error → 截断原文兜底。
 */
static QString humanizeSiteError(const QByteArray &raw)
{
    const QString txt = QString::fromUtf8(raw);
    const int t1 = txt.indexOf(QLatin1String("<title>"));
    if (t1 >= 0) {
        const int t2 = txt.indexOf(QLatin1String("</title>"), t1);
        if (t2 > t1) {
            const QString t = txt.mid(t1 + 7, t2 - t1 - 7).trimmed();
            if (!t.isEmpty()) return t;
        }
    }
    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    for (const char *k : {"message", "error", "detail"}) {
        const QString v = o.value(QLatin1String(k)).toString();
        if (!v.isEmpty()) return v;
    }
    return txt.left(200).simplified();
}

void ViewerBackend::onSessionTicketFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();
    m_sessionReply = nullptr;
    m_accountBusy = false;
    if (status != 200) {
        const QString why = humanizeSiteError(raw);
        // 把"接下来该做什么"直接写在界面上：这三条是运维/老师现场唯一能自己解决的分叉，
        // 否则拿到"HTTP 403"这类字样只能来问开发。
        QString hint;
        if (status == 403) {
            hint = QStringLiteral("（该网站账号没有集控权限，请到站点后台给它授权，或改用有权限的账号）");
        } else if (status == 401) {
            hint = QStringLiteral("（网站会话已失效，请重新登录）");
        } else if (status == 500) {
            hint = QStringLiteral("（站点侧没配好云端接入票密钥，需运维处理）");
        }
        m_accountFatal = true;
        m_accountText = QStringLiteral("取云端接入票失败（HTTP %1%2）%3")
                            .arg(status)
                            .arg(why.isEmpty() ? QString() : QStringLiteral("：%1").arg(why))
                            .arg(hint);
        logf("[account] FAIL 换接入票失败（HTTP %d）：%s", status, why.toUtf8().constData());
        emit accountChanged();
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    m_cloudTicket = o.value(QStringLiteral("ticket")).toString();
    m_ticketExp = (qint64)o.value(QStringLiteral("exp")).toDouble();
    m_accountUser = o.value(QStringLiteral("user")).toString();
    // 邮箱认证状态：站点明确回传 verified=false 才置 false，其余（缺省/true）按已认证，
    // 避免老静态令牌路径（拿不到该字段）被误标为「未认证用户」。
    const QJsonValue vv = o.value(QStringLiteral("verified"));
    const bool newVerified = !(vv.isBool() && vv.toBool() == false);
    if (newVerified != m_verified) {
        m_verified = newVerified;
        emit accountChanged();
    }
    m_accountFatal = false;
    if (m_cloudTicket.isEmpty()) {
        m_accountText = QStringLiteral("取云端接入票失败：站点没返回 ticket");
        m_accountFatal = true;
        logf("[account] FAIL 站点换票响应里没有 ticket：%s", raw.left(512).constData());
    } else {
        logf("[account] 云端接入票到手（%s，%s 到期），正在连云端…",
             m_accountUser.toUtf8().constData(),
             QDateTime::fromMSecsSinceEpoch(m_ticketExp).toString(QStringLiteral("MM-dd HH:mm")).toUtf8().constData());
    }
    saveAccount();
    refreshAccountText();
    if (!m_cloudTicket.isEmpty()) connectToCloud();
}
void ViewerBackend::ensureCloudTicket(bool interactive)
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    if (m_sessionReply) return;   // 换票请求在飞，别叠一个
    refreshAccountText();
    if (!m_cloudTicket.isEmpty() && ticketUsable()) {
        if (interactive) connectToCloud();
        return;
    }
    if (m_sessionToken.isEmpty()) {
        // 没会话也没票：只有用户主动点登录才去开浏览器，否则每次开机弹浏览器是骚扰
        if (!interactive) {
            logf("[account] 本机没有网站会话，跳过静默取票（用户可点托盘/界面里的「登录」）");
            return;
        }
        if (!m_accountFatal) setStatus(QStringLiteral("正在用网站账号登录…"), false);
        loginWithSite();
        return;
    }
    // 有会话令牌 → 去站点换一张云端接入票
    m_accountBusy = true;
    m_accountFatal = false;
    refreshAccountText();
    QUrl u(QStringLiteral("%1/api/console/desktop/session").arg(m_siteUrl));
    QNetworkRequest req(u);
    req.setRawHeader("Authorization", ("Bearer " + m_sessionToken).toUtf8());
    m_sessionReply = m_nam->post(req, QByteArray());
    QObject::connect(m_sessionReply, &QNetworkReply::finished,
                     this, &ViewerBackend::onSessionTicketFinished);
    logf("[account] 正在向站点取云端接入票…");
}
void ViewerBackend::onAccountTimer()
{
    // 票快到期（<6 小时）就提前换一张；换了成功则顺手重连一次，免得卡在"票刚过期"的空窗
    if ((m_cloudTicket.isEmpty() || !ticketUsable(6 * 60 * 60 * 1000)) && !m_accountBusy) {
        ensureCloudTicket(false);
    }
}