// 星集控 · 管理端后端实现（从 main.cpp 原样搬过来，行为保持一致）

#include "viewerbackend.h"

#include "oauthlogin.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonValue>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebEngineView>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>

namespace {

/**
 * 一片多大（**二进制**字节）。base64 之后约再膨胀 1/3，也就是一条 ~87KB。
 *
 * 为什么是 64KB 而不是"整文件一次发完"：① 被控端 file_chunk 的 data 是 base64，
 * 大文件整发＝一条几 MB 的 JSON，WebSocket 和云端都要各自缓存；② 一片一回报，
 * 卡在第几片、卡在哪一条能直接看出来，整发的话失败了你不知道是哪一步断的。
 */
constexpr qint64 kFileChunkBytes = 64 * 1024;

/**
 * 协议 v1 信封：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
 * 文本消息统一走它；二进制帧（画面）另走 [1B 版本][2B 大端 headerLen][header][JPEG]。 */
QString makeEnvelope(const QString &type, const QJsonObject &payload)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), QString());
    o.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    o.insert(QStringLiteral("payload"), payload);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

constexpr int kFrameHeaderMax = 4096;
constexpr int kReconnectMs = 5000;

// 管理端默认连的生产云端（2026-10-06：从本机 dev 的 127.0.0.1:8788 改过来）。
// ⚠️ 只有"没设 STE_VIEWER_URL"时才用；本机联调请显式设 STE_VIEWER_URL=ws://127.0.0.1:8788/ws/viewer。
// 一律 wss（走 443 的公网域名），别抄 ws —— 明文 ws 连公网会被中间人直投。
static const QString kDefaultViewerUrl = QStringLiteral("wss://control.245959623.xyz/ws/viewer");

/**
 * 只有管理员才做得了的操作（2026-10-06 打磨：权限门控）。
 *
 * 判据不是"危险不危险"，是**影响面**：
 *   ① 不可逆（锁屏/重启/关机）、② 能把整台教室机交给外人（远控/终端开一条 shell）、
 *   ③ 一次动作打到所有设备（广播、定时）、④ 把程序推起来放到前台（launch）。
 * 只读的（截图/日志/探活）和改一台机器一点小状态的（音量/通知/录播）教师都能用，
 * 否则教师这个角色等于被锁在门外——那角色就是装饰。
 *
 * ⚠️ 这张表是**界面门控用**的，不是安全边界：真正的闸门在被控端和云端。
 * 界面置灰只是"别让人白点一下"，别拿它当权限系统用。
 */
const QStringList &adminOnlyActions()
{
    static const QStringList kAdmin = {
        QStringLiteral("lock"),
        QStringLiteral("reboot"),
        QStringLiteral("shutdown"),
        QStringLiteral("remote_control_start"),
        QStringLiteral("terminal_open"),
        QStringLiteral("schedule_shutdown"),
        QStringLiteral("schedule_reboot"),
        QStringLiteral("launch_app"),
        QStringLiteral("broadcast")   // 集控页「广播」：一发打所有在线设备
    };
    return kAdmin;
}

/* ── 静态区判定（UU 远程同款思路的轻量版）─────────────────────────────
 * 被控端画面没在动的时候，收到多少帧都是同一张图 —— 解码 + 重绘一遍纯属白烧 CPU。
 * 判定办法：给每帧算一个**稀疏指纹**（隔 7919 字节抽一个点，4096 个点，比整图遍历便宜得多，
 * 但鼠标一动、窗口一开立刻就变），连续 kStaticStreak 帧指纹一致就认定"进了静态区"，
 * 界面停止换 tick（QML 的 Image.source 不变 → 不再 requestImage → 不再解码重绘）。
 *
 * 为什么不用"JPEG 字节全等"来判：被控端每次抓屏都会重新编码一次，
 * 同一张画面编出来的字节不可能一模一样，字节比较会永远判不出静态。
 *
 * 为什么不用定时器降帧率：降帧率只是从 15fps 变 5fps，带宽和解码照样在跑；
 * 这里是"真不动就不画"，跟画面里有没有东西动无关。 */
constexpr int kStaticStreak = 8;          // 约 0.5 秒（被控端 15fps）——太短会误判、太长画面发僵

/** 画面指纹：宽高 + 整块缓冲里的伪随机采样和。同尺寸同画面 → 同指纹。 */
quint64 frameFingerprint(const QImage &img)
{
    if (img.isNull()) return 0;
    const qsizetype total = static_cast<qsizetype>(img.sizeInBytes());   // Qt 里没有 byteCount()，别照抄旧代码
    if (total <= 0) return 0;
    const uchar *p = img.constBits();
    quint64 h = (quint64)img.width() << 32 | (quint64)img.height();
    constexpr qsizetype stride = 7919;    // 质数：抽样点不会周期性落在同一片区域
    for (int k = 0; k < 4096; ++k) {
        h = h * 131 + p[(qsizetype)k * stride % total];
    }
    return h;
}

/**
 * 把 QString 转成 **带双引号的合法 JSON 字符串字面量**（即 "\"...\""，含外层引号）。
 *
 * 为什么不用手写转义：SDP 里全是 / 换行 引号，ICE candidate 是嵌套 JSON，
 * 自己拼一定会在某个特殊字符上炸，而且炸出来的是 JS 语法错、报在 runJavaScript 的字符串里，
 * 很难定位。这里借 Qt 自己的 QJsonObject/QJsonDocument 去转义 —— 转义规则只认一处，不会漂。
 *
 * 顺带把坑记下：QString 没有 toJsonEscapedString()，QJsonDocument 也没有接受 QJsonValue 的
 * 构造函数（QJsonDocument(QJsonValue) 编译不过），别再往那个方向试。
 */
QString jsonStringLiteral(const QString &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("_"), s);
    const QByteArray full = QJsonDocument(o).toJson(QJsonDocument::Compact);  // {"_":"..."}
    // lastIndexOf(':') 取的是最后一个冒号，即我们那个 "_" 对应的值起点；
    // mid 之后正好是带双引号的字面量本体
    return QString::fromUtf8(full.mid(full.lastIndexOf(':') + 1));
}

} // namespace

ViewerBackend::ViewerBackend(QObject *parent)
    : QObject(parent)
{
    // 身份与提醒开关先落下来（QSettings），后面所有门控判断都基于它。
    loadPrefs();

    // 文件推送的"发下一片"由回执驱动：收到一片 done 才发下一片。
    // 不这样写就只能靠定时器盲发，一旦被控端拒收一片，后面全乱序、越堆越多。
    QObject::connect(this, &ViewerBackend::resultReceived, this,
                     [this](const QString &, const QString &action, const QString &state,
                            const QString &result, const QString &error, const QString &,
                            const QJsonObject &data) {
                         if (action != QStringLiteral("file_push")
                                 && action != QStringLiteral("file_chunk")
                                 && action != QStringLiteral("file_done")) return;
                         if (m_fileState == QStringLiteral("idle")
                                 || m_fileState == QStringLiteral("done")
                                 || m_fileState == QStringLiteral("failed")) return;

                         // 分两档看回执：state=sent 只是"云端收下了、机器还没轮到"，
                         // 这时 result 还是空的，拿它判成败会把"正在推"误报成"推失败"。
                         // 只有 executed（机器真做了）之后，result != done 才算失败。
                         if (result != QStringLiteral("done")) {
                             if (state != QStringLiteral("executed")) return;   // 还在路上，继续等
                             // 失败原因原样照搬：被控端给的是"文件名缺少扩展名"这类人话，
                             // 我们不该自己再编一套说法（两套口径迟早对不上）
                             setFileFail(error.isEmpty()
                                             ? QStringLiteral("%1 被拒（没说原因）").arg(action)
                                             : error);
                             return;
                         }

                         if (action == QStringLiteral("file_push")) {
                             logf("[viewer] file_push 会话已开，开始分片（总 %lld 字节）",
                                  (long long)m_fileTotal);
                            // 本机文件在 pushFile() 里就开好了（setFileName + open 一起在那儿）。
                            // 这里只兜底：推的过程中文件被删了/被别人换走。
                            // errorString() 必须带上 —— 只说"打不开"等于没报，
                            // 日志里得能看见是"文件不在了"还是"被占用/被拒绝"
                            if (!m_pushFile.isOpen() && !m_pushFile.open(QIODevice::ReadOnly)) {
                                // 诊断行：这里栽过一次（漏 setFileName，errorString 只会给无用的
                                // "Unknown error"）。出问题时要能一次看清是"句柄被谁关了"
                                // 还是"文件真没了"，所以 fileName/exists/size/error 全打出来
                                logf("[viewer] 诊断 源文件句柄：isOpen=%d fileName=[%s] exists=%d size=%lld err=[%s]",
                                     (int)m_pushFile.isOpen(),
                                     m_pushFile.fileName().toUtf8().constData(),
                                     (int)m_pushFile.exists(),
                                     (long long)m_pushFile.size(),
                                     m_pushFile.errorString().toUtf8().constData());
                                setFileFail(QStringLiteral("本机文件读不开：%1（%2）")
                                                .arg(m_pushPath)
                                                .arg(m_pushFile.errorString()));
                                return;
                            }
                             m_fileState = QStringLiteral("sending");
                             emit fileProgressChanged();
                             sendNextChunk();
                         } else if (action == QStringLiteral("file_chunk")) {
                             sendNextChunk();
                         } else if (action == QStringLiteral("file_done")) {
                             m_fileState = QStringLiteral("done");
                             m_fileTarget = data.value(QStringLiteral("path")).toString();
                             m_fileError.clear();
                             if (m_pushFile.isOpen()) m_pushFile.close();
                             logf("[viewer] ✅ 文件已推到教室机：%s（%lld 字节）",
                                  m_fileTarget.toUtf8().constData(), (long long)m_fileBytes);
                             emit fileProgressChanged();
                         }
                     });
}

// QWebEngineView 必须在 event loop 停止**之后**销毁：主线程一停转，Chromium 就要求 view
// 已被释放，否则是 use-after-free 直接崩。这里用 deferDelete 把销毁推到事件循环之后。
ViewerBackend::~ViewerBackend()
{
    if (!m_rtcHtmlPath.isEmpty()) {
        QFile::remove(m_rtcHtmlPath);
        m_rtcHtmlPath.clear();
    }
    if (m_rtcView) {
        m_rtcView->deleteLater();
        m_rtcView = nullptr;
    }
}

// ───────────────────────────────────────────────────────────────────────────
// 版本 / 身份 / 角色 / 提醒偏好
// ───────────────────────────────────────────────────────────────────────────

/**
 * 编译期注入的版本号（Windows 上带三个部分才显示版本号属性，只给两个的话
 * Qt 会写 "8.0.0.0" 这种被当成文件的怪东西 —— 前面三段拼够三段就行）。
 */
#ifndef STELARITH_VIEWER_VERSION_STRING
#  define STELARITH_VIEWER_VERSION_STRING "0.0.0"
#endif
#define STELARITH_MAKE_VERSION(a, b, c) #a "." #b "." #c

QString ViewerBackend::version() const
{
    return QString::fromLatin1(STELARITH_MAKE_VERSION(
        STELARITH_VIEWER_VERSION_STRING));
}

/**
 * 是否处在"已经登录"的状态。
 *
 * 判据严格按**手上有没有能连云端的凭据**：有接入票，或者配了静态令牌，都算。
 * 只看 sessionToken 不够 —— 那种情况下界面会显示"已登录"、但云端连不上，
 * 用户对着一个登录着的顶栏纳闷为什么没设备（这就是以前那个坑）。
 */
bool ViewerBackend::loggedIn() const
{
    return !m_cloudTicket.isEmpty() || !m_token.isEmpty();
}

QString ViewerBackend::accountName() const { return m_accountUser; }

QString ViewerBackend::role() const { return m_role; }

bool ViewerBackend::mayDo(const QString &perm) const
{
    if (m_role != QStringLiteral("admin")) return adminOnlyActions().contains(perm);
    return true;
}

bool ViewerBackend::setRole(const QString &role)
{
    const QString r = role.trimmed().toLower();
    const bool ok = (r == QStringLiteral("admin") || r == QStringLiteral("teacher"));
    if (!ok) return false;
    if (r == m_role) return true;
    // 没登录按最低权限看：这是"当前身份"的默认值，别让它显示成管理员。
    if (!loggedIn() && r == QStringLiteral("admin")) return false;
    m_role = r;
    QSettings s;
    s.setValue(QStringLiteral("account/role"), r);
    logf("[viewer] 身份切到 %s（顶栏徽标与按钮门控同步）",
         r.toUtf8().constData());
    refreshGateState();
    return true;
}

void ViewerBackend::openRegisterPage()
{
    if (m_siteUrl.isEmpty()) {
        setStatus(QStringLiteral("还没配站点地址（viewer.env 里加 STE_SITE_URL）"), true);
        return;
    }
    const QUrl u(QStringLiteral("%1/register").arg(m_siteUrl));
    logf("[viewer] 打开站点注册页 %s", u.toString().toUtf8().constData());
    QDesktopServices::openUrl(u);
}

void ViewerBackend::openExternal(const QString &url)
{
    if (url.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(url));
}

void ViewerBackend::setNotifyOnDone(bool on)
{
    if (m_notifyOnDone == on) return;
    m_notifyOnDone = on;
    QSettings s;
    s.setValue(QStringLiteral("prefs/notifyOnDone"), on);
    emit notifyPrefChanged();
}

void ViewerBackend::setNotifyOnOffline(bool on)
{
    if (m_notifyOnOffline == on) return;
    m_notifyOnOffline = on;
    QSettings s;
    s.setValue(QStringLiteral("prefs/notifyOnOffline"), on);
    emit notifyPrefChanged();
}

/**
 * 弹一条本机提示（状态行 + 可选桌面气泡）。
 *
 * 以前"提醒"那两张卡是画着玩的（两个空心的圆，点了没反应）—— 2026-10-06 打通：
 * 操作完成、设备离线这两件事在后端本来就是现成的时刻，直接接上就行。
 */
void ViewerBackend::notifyPref(const QString &text)
{
    setStatus(text, false);
    // 桌面气泡走 main.cpp 登记的那个常驻托盘（Qt 6.8 没有 find() 可拿，靠这个钩子），
    // 状态行那一行无论如何都写 —— 就算没有托盘（比如远程会话），提示也不该消失。
    stelarithNotifyTray(QStringLiteral("星集控"), text);
}

/**
 * 身份变了之后统一刷一遍：顶栏徽标、按钮置灰、连接区那行账号文案全挂着这些读数，
 * 一个一个 emit 容易漏（漏了就是"切了身份但按钮还是能点"）。
 */
void ViewerBackend::refreshGateState()
{
    emit roleChanged();
    emit accountChanged();
    refreshAccountText();
}

/** 提醒开关从 QSettings 读回来（构造时调用一次）。 */
void ViewerBackend::loadPrefs()
{
    QSettings s;
    const auto roleValue = s.value(QStringLiteral("account/role")).toString().toLower();
    // 默认值 = 管理员：这是单机自用场景（管理员在教室机上开这个程序），
    // 一上来就把按钮全灰掉比"默认宽松"更招骂。
    m_role = (roleValue == QStringLiteral("teacher")) ? QStringLiteral("teacher")
                                                      : QStringLiteral("admin");
    m_notifyOnDone = s.value(QStringLiteral("prefs/notifyOnDone"), true).toBool();
    m_notifyOnOffline = s.value(QStringLiteral("prefs/notifyOnOffline"), true).toBool();
}

// ───────────────────────────────────────────────────────────────────────────
// 站点账号（OAuth 一户通）
// ───────────────────────────────────────────────────────────────────────────
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
    if (s != m_accountText) {
        m_accountText = s;
        emit accountChanged();
    }
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
    logf("[account] 已忘记本机账号（凭据已删）");
    refreshAccountText();
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
        const QString why = QString::fromUtf8(raw).left(256);
        m_accountFatal = true;
        m_accountText = QStringLiteral("取云端接入票失败（HTTP %1%2）")
                            .arg(status)
                            .arg(why.isEmpty() ? QString() : QStringLiteral("：%1").arg(why));
        logf("[account] FAIL 换接入票失败（HTTP %d）：%s", status, raw.left(512).constData());
        emit accountChanged();
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    m_cloudTicket = o.value(QStringLiteral("ticket")).toString();
    m_ticketExp = (qint64)o.value(QStringLiteral("exp")).toDouble();
    m_accountUser = o.value(QStringLiteral("user")).toString();
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
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
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

void ViewerBackend::start()
{
    // ⚠️ 2026-10-06 改默认值：原来是 ws://127.0.0.1:8788/ws/viewer（本机 dev 云端）。
    // 包一发出去老师机拿到的就是这份默认值 → 指向"这台机器自己" = 永远连不上，
    // 界面上表现就是"连不上云端 + 设备列表空"（用户看到的"半成品"其实是根本没连上云端）。
    // 生产入口 = 云端监听 0.0.0.0:8788 的公网域名（见 Stelarith-cloud-ws/src/provision.js 的 CLOUD_PUBLIC_BASE）。
    // 本机联调照旧设 STE_VIEWER_URL 覆盖就行，不要为了联调把默认值改回去。
    m_url = qEnvironmentVariable("STE_VIEWER_URL", kDefaultViewerUrl).trimmed();
    if (m_url.isEmpty()) m_url = kDefaultViewerUrl;
    m_token = qEnvironmentVariable("STE_VIEWER_TOKEN", QString()).trimmed();
    if (m_token.isEmpty()) {
        logf("[viewer] WARN 未设环境变量 STE_VIEWER_TOKEN：云端 /ws/viewer 已加鉴权，"
             "既没静态令牌也没接入票的话会被拒绝（不是网络问题，是没带凭据）");
    }
    m_siteUrl = qEnvironmentVariable("STE_SITE_URL", QString()).trimmed();
    if (m_siteUrl.isEmpty()) m_siteUrl = QString::fromUtf8(kDefaultSiteUrl);
    logf("[viewer] 云端地址 %s / 站点 %s", m_url.toUtf8().constData(),
         m_siteUrl.toUtf8().constData());
    emit cloudUrlChanged();

    // ── 站点账号（OAuth 一户通，2026-10-06）──
    // 顺序：先读本机有没有存过的凭据 → 有就静默取票（取不到也不打扰）→ 没有就等用户点登录。
    // 没配 STE_SITE_URL 时完全按老路走（静态令牌），这条改动对老机器是零影响。
    m_oauth = new OAuthLogin(this);
    QObject::connect(m_oauth, &OAuthLogin::succeeded, this, &ViewerBackend::onOAuthSucceeded);
    QObject::connect(m_oauth, &OAuthLogin::failed, this, &ViewerBackend::onOAuthFailed);

    loadAccount();
    if (m_siteUrl.isEmpty()) {
        logf("[viewer] 没配 STE_SITE_URL：不走网站账号登录，仍用 viewer.env 里的静态令牌");
        m_accountText = QStringLiteral("未配置站点地址（只走静态令牌）");
        emit accountChanged();
    } else {
        m_accountTimer = new QTimer(this);
        m_accountTimer->setInterval(5 * 60 * 1000);   // 票到期巡检
        QObject::connect(m_accountTimer, &QTimer::timeout, this, &ViewerBackend::onAccountTimer);
        m_accountTimer->start();
        refreshAccountText();
        ensureCloudTicket(false);   // 静默：有票直接用，没票也不弹浏览器
    }

    // Qt6 的 QWebSocket 构造是三参 (origin, version, parent)，只给 parent 会重载歧义；
    // 它是 QObject 子类但不收 QObject*，用 setParent 挂上来
    m_ws = new QWebSocket();
    m_ws->setParent(this);
    QObject::connect(m_ws, &QWebSocket::connected, this, &ViewerBackend::onConnected);
    QObject::connect(m_ws, &QWebSocket::disconnected, this, &ViewerBackend::onDisconnected);
    QObject::connect(m_ws, &QWebSocket::textMessageReceived, this, &ViewerBackend::onTextMessage);
    QObject::connect(m_ws, &QWebSocket::binaryMessageReceived, this, &ViewerBackend::onBinaryMessage);
    QObject::connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        logf("[viewer] FAIL WS 错误 %d（%s）→ 会自己退避重连", (int)err,
             m_ws->errorString().toUtf8().constData());
        setStatus(QStringLiteral("连不上云端：") + m_ws->errorString(), true);
    });

    // fps 统计：每秒算一次（当前画面是 2 秒一帧，别把 0.5fps 说成 200fps）
    m_fpsTimer = new QTimer(this);
    QObject::connect(m_fpsTimer, &QTimer::timeout, this, &ViewerBackend::tickFps);
    m_fpsTimer->start(1000);

    m_lastFpsCheckMs = QDateTime::currentMSecsSinceEpoch();
    connectToCloud();
}

// ────────────────────────────────────────────────────────────────────────────
// WebRTC 收流（T-3，2026-10-04）
//
// 管理端是 **answer 侧**：
//   被控端 captureStream → createOffer →（云端中继 rtc-offer）→ 这里 setRemoteDescription
//   → createAnswer → rtc-answer 回云端 → ontrack 拿到 video track 塞进 <video>
//   → 定时器 drawImage 到 canvas → toDataURL 抽 JPEG → setRtcFrame 走现有 frame 通道。
//
// 为什么用离屏 QWebEngineView 而不是纯 C++ WebRTC 库：被控端（control-qt）已经用同样的
// canvas.captureStream 跑通了推流，这里用同套 Chromium 栈做 answer 侧是最短路径，
// 省掉整套 native WebRTC 依赖，而且 ontrack/ICE/codec 协商交给浏览器实现，不自己造。
//
// 为什么只收不发：被控端已经建了 RTCPeerConnection 并持有 track，管理端只需要接收；
// 双向视频（远控回环）不在这一批范围内。
// ────────────────────────────────────────────────────────────────────────────

/**
 * 取 WebRTC 信令里的字段（sdp / candidate），**两层都找**。
 *
 * 为什么必须往里再剥一层：被控端（control-qt 的 rtcSendSignal）发信封时把信令又包了一层 ——
 *   信封.payload = { payload: { kind, sdp, candidate }, from: "agent" }
 * 云端原样把这个 payload 转给管理端（registry.rtcRelayToViewers），所以到管理端手上就变成
 *   顶层 { payload: { kind, sdp, candidate }, from, uid }
 * 只看顶层 pay.value("sdp") 必然是空 → 日志上就一句"收到 rtc-offer 但没有 sdp 字段"，
 * 看不出真正的问题是协议包了两层。这里先取顶层、取不到再取内层，两种写法都吃。
 */
static QString rtcField(const QJsonObject &pay, const QLatin1String &key)
{
    QJsonValue v = pay.value(key);
    if (v.isUndefined() || v.isNull()) v = pay.value(QStringLiteral("payload")).toObject().value(key);
    return v.toString();
}

/** 离屏渲染页：一个 <video>（远端视频轨）+ 一个 <canvas>（抽帧用），全黑底、无边框。 */
static const char *kRtcViewerHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>rtc</title>
<style>html,body{margin:0;background:#000;overflow:hidden;width:1280px;height:720px}</style>
</head><body>
<video id="v" autoplay muted playsinline style="width:1280px;height:720px;object-fit:contain"></video>
<canvas id="c" width="1280" height="720"></canvas>
<script>
// 页面内部报错必须能看见。QWebEnginePage 没给"接管 console"的公开入口
// （javaScriptConsoleMessage 在 6.8 是 protected，setConsoleMessageCallback 根本不存在），
// 所以在这儿自己把 console 兜住：QWebChannel 建不起来时它只往 console 打一行 error，
// 不拦下来的话 C++ 侧永远收不到，"为什么没画面"就彻底没线索。
window.__logs = [];
(function () {
  var push = function (k, a) {
    window.__logs.push(k + ': ' + Array.prototype.join.call(a, ' '));
    if (window.__logs.length > 30) window.__logs.shift();
  };
  ['error', 'warn', 'log'].forEach(function (k) {
    var orig = console[k] ? console[k].bind(console) : function () {};
    console[k] = function () { push(k, arguments); orig.apply(null, arguments); };
  });
})();
</script>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<script>
// ⚠️ 这里**绝对不能**把变量/函数起名叫 qt：
// 上面那句 new QWebChannel(...) 要用的是 Qt 注入的全局 qt（它身上挂着 webChannelTransport）。
// 而 var 声明会被提升（hoisting）：脚本一进执行上下文 qt 就已被覆盖成 undefined，
// new QWebChannel(qt.webChannelTransport) 直接拿到 undefined → QWebChannel 构造函数里面
// 一句 console.error('QWebChannel: invalid transport argument, ignored.') 就 return 了，
// 回调永远不触发 → window.__qt 永远是 undefined → 后面所有 qt().xxx 全报
// "Cannot read properties of undefined"。这个坑只出现在控制台一行 error，C++ 侧一点线索都没有。
var ch = new QWebChannel(qt.webChannelTransport, function () {
  window.__qt = ch.objects.qt;
  window.__qtOk = true;
});
var v = document.getElementById('v');
var c = document.getElementById('c');
var x = c.getContext('2d');
var pc = null;
var tracks = [];

// 注意：这里的 window.__qt.<name> 必须和 C++ 侧 Q_INVOKABLE 的**方法名逐字一致**
// （QWebChannel 按名字映射，名字对不上是静默失效，最难查的一种失败）
var qtc = function () { return window.__qt; };

function log(m) { try { if (qtc()) qtc().rtcDiag(String(m)); } catch (e) {} }

window.addEventListener('unhandledrejection', function (e) {
  log('REJECT ' + (e.reason && (e.reason.name + ' ' + e.reason.message) || e.reason));
});

// 建连：被控端的 offer 先到 → setRemoteDescription → createAnswer 回云端
window.__setOffer = function (b64) {
  if (!qtc()) { console.error('no qtc jobject'); return; }
  var sdpStr = atob(b64);
  log('setOffer len=' + sdpStr.length);
  if (!pc) {
    pc = new RTCPeerConnection();
    pc.onicecandidate = function (e) {
      if (!e.candidate) return;
      try { qtc().rtcGotIce(JSON.stringify(e.candidate)); } catch (err) { log('ice-send-fail ' + err); }
    };
    pc.ontrack = function (e) {
      var t = e.track;
      tracks.push(t);
      // 必须显式 muted + play()：Chromium 的自动播放策略对"带音轨的媒体"不放行，
      // 只写 autoplay 属性不够 —— 结果就是 ontrack 明明拿到了 live 的 video 轨，
      // video 元素 readyState 永远是 0、videoWidth/Height 是 0，抽帧全抓到空，
      // 日志上还看不出任何报错（这正是之前"tracks 有 live 但画面全黑"的来源）。
      v.muted = true;
      v.autoplay = true;
      try { v.srcObject = e.streams[0]; } catch (err) { log('src-fail ' + err); }
      try {
        var pr = v.play();
        if (pr && pr.catch) pr.catch(function (er) { log('play-fail ' + er.message); });
      } catch (err) { log('play-FAIL ' + err); }
      log('ontrack kind=' + t.kind + ' ready=' + t.readyState);
      try { qtc().rtcGotTrack(); } catch (err) { log('track-cb-fail ' + err); }
    };
    pc.onconnectionstatechange = function () { log('conn=' + pc.connectionState); };
  }
  pc.setRemoteDescription({ type: 'offer', sdp: sdpStr }).then(function () {
    return pc.createAnswer();
  }).then(function (ans) {
    return pc.setLocalDescription(ans);
  }).then(function () {
    log('answer len=' + pc.localDescription.sdp.length);
    qtc().rtcGotAnswer(pc.localDescription.sdp);
  }).catch(function (e) {
    log('setOffer-FAIL ' + e.name + ' ' + e.message);
    qtc().rtcDiag('setOffer failed: ' + (e.name + ' ' + e.message));
  });
};

window.__addIce = function (b64) {
  var candStr = atob(b64);
  if (!pc) { log('ice-before-pc ' + candStr.slice(0, 60)); return; }
  pc.addIceCandidate(JSON.parse(candStr)).then(function () {
    log('ice-added');
  }).catch(function (e) { log('ice-FAIL ' + e.name + ' ' + e.message); });
};

// 拆掉旧 pc，为一次**全新**的协商腾地方。
//
// 为什么必须有：被控端每次 rtc-start 都 new 一个全新的 RTCPeerConnection（见它的 __startStream），
// 每个 offer 背后都是一个新 peer。而我们这边原来只在 pc 为空时才建，于是第二台机器（或重连）
// 的 offer 是被 setRemoteDescription 塞进**上一个 pc** 的 —— 那个 pc 已经处在
// connected / have-local-offer，Chromium 直接抛 "called in wrong state"，
// 结果就是：第一台机器有画面，切到第二台永远黑屏，而且日志里只有一句 JS 异常。
// 对称地看，被控端自己也有 __reset 干同一件事。
window.__resetPc = function () {
  try { if (pc) pc.close(); } catch (e) {}
  pc = null;
  tracks = [];
  try { v.srcObject = null; } catch (e) {}
  log('pc-reset');
};

// 抽帧：远端 video 解码出的帧画到 canvas，转 JPEG 交回 C++（复用现有 frame 通道）
window.__grab = function () {
  try {
    if (!qtc()) return;                      // 桥还没建好，别刷屏 TypeError
    if (v.readyState < 2) { qtc().setRtcFrame('EMPTY'); return; }
    x.drawImage(v, 0, 0, 1280, 720);
    var s = c.toDataURL('image/jpeg', 0.72);
    qtc().setRtcFrame(s.indexOf(',') >= 0 ? s.slice(s.indexOf(',') + 1) : s);
  } catch (e) {
    qtc().setRtcFrame('EMPTY');
    log('grab-FAIL ' + e.name + ' ' + e.message);
  }
};

// 自检/诊断出口：被控端那边也留了一个 __diag，这里对称放一个
window.__diag = function () {
  return JSON.stringify({
    hasPc: !!pc,
    state: pc ? pc.connectionState : 'none',
    tracks: tracks.map(function (t) { return t.kind + ':' + t.readyState; }),
    ready: v.readyState,
    w: v.videoWidth,
    h: v.videoHeight
  });
};
</script>
</body></html>)HTML";

void ViewerBackend::setRtcState(const QString &s)
{
    if (m_rtcState == s) return;
    m_rtcState = s;
    m_rtcReady = (s == QStringLiteral("track"));   // 拿到远端视频轨才算真出画面
    emit rtcStateChanged();
}

void ViewerBackend::initRtcView()
{
    if (m_rtcView) return;
    cancelRtcViewReap();   // 上一轮倒计时还没到，别把刚要复用的页面收掉

    // WebEngine 是 Chromium：默认沙箱在没配 seccomp 的环境下会拒跑 RTCPeerConnection 的
    // ICE 传输（被控端已经踩过同一个坑，同样用 qputenv 关掉）。必须在创建 view 之前设。
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "-no-sandbox --disable-gpu-sandbox --disable-dev-shm-usage");

    // QWebEngineView 是 QWidget 子类、不是 QObject，没有 (QObject*) 构造 ——
    // 只能先不带 parent 建，再 setParent 挂到 backend 上（挂不上就直接泄漏在堆上）
    m_rtcView = new QWebEngineView();
    // 渲染层不参与界面布局：只负责跑 RTCPeerConnection，画完抽帧交回 C++。
    // 用 move 挪出屏幕而不是 hide —— hide 后 Chromium 会暂停媒体管线，ontrack 也拿不到帧。
    m_rtcView->setGeometry(-2000, -2000, 1280, 720);

    // 关键：页面必须以 **file://** origin 加载，才能访问 Qt 内建的
    // qrc:///qtwebchannel/qwebchannel.js。用 data: URL 加载会让 origin 变成 null，
    // 那个 <script src> 直接被同源策略拦掉，QWebChannel 建不起来 → JS 侧 __qt 是 undefined。
    // 文件写在临时目录、退出时清理；不放在工程目录里免得污染版本树。
    // ⚠️ 2026-10-07：文件名带上进程号，不再所有实例共用同一个固定名。
    // 真机上出现过 "收流页写不进临时文件（拒绝访问）" —— 上一个实例（或它残留的 Chromium
    // 子进程）还握着那个同名文件，新实例一开就写不进去，RTC 收流整条路直接废掉：
    // 画面看着还能动是因为有 JPEG 兜底，但那是 0.5–2 fps，不是实时流。
    // 带 PID 之后各写各的互不干扰；真写不进去时先把同名残留清掉再试一次。
    // ⚠️ 2026-10-07 第二次修：光带进程号还不够，真机日志里带 PID 的名字一样报
    // "拒绝访问"。根因不是名字撞车，是**目录**：Temp 是公共目录，系统清理、实时防护
    // 扫描、别的程序都在那儿落文件，谁先握住谁说了算，文件名再唯一也躲不开。
    // 这次两处一起改：① 挪到**本应用自己的缓存目录**（CacheLocation），不再跟 Temp 抢；
    // ② 文件名交给 QTemporaryFile 发 —— 系统保证唯一且原子创建，撞了会自己换名重试，
    // 而不是像 QFile::open 那样一次失败就完蛋。
    const QString rtcDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString baseDir = rtcDir.isEmpty() ? QDir::tempPath() : rtcDir;
    QDir().mkpath(baseDir);                 // 目录不给就自己建，mkdir 失败下面照样会报出来

    QTemporaryFile tmp(baseDir + QStringLiteral("/stelarith-viewer-rtc-XXXXXX.html"));
    tmp.setAutoRemove(false);               // 文件要留给 WebEngine 加载，析构时绝不能删
    if (!tmp.open()) {
        logf("[viewer] FAIL 收流页临时文件建不出来（目录 %s）：%s",
             baseDir.toUtf8().constData(), tmp.errorString().toUtf8().constData());
        setRtcState(QStringLiteral("failed"));
        return;
    }
    const QByteArray pageBytes(kRtcViewerHtml);
    if (tmp.write(pageBytes) != pageBytes.size()) {
        logf("[viewer] FAIL 收流页没写全（%s）", tmp.fileName().toUtf8().constData());
        setRtcState(QStringLiteral("failed"));
        return;
    }
    tmp.close();
    m_rtcHtmlPath = tmp.fileName();         // 退出时删掉，别在缓存目录里堆垃圾（见 ~ViewerBackend）
    const QString htmlPath = m_rtcHtmlPath;

    auto *page = new QWebEnginePage(m_rtcView);
    m_rtcView->setPage(page);
    // 刻意**不给 parent**：QWebEngineView 是 QWidget，只能挂 QWidget 做 parent，
    // 而 backend 是 QObject（挂不上）；再包一层 QWidget 纯属多余。
    // 生命周期统一由析构里的 deleteLater() 收口（见 ~ViewerBackend）。
    m_rtcView->setContextMenuPolicy(Qt::NoContextMenu);
    m_rtcView->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    // 这是纯渲染层，不该有 JS 弹新窗 / 下载的能力（被控端那边也没这需求）
    m_rtcView->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);

    // QWebChannel：JS ↔ C++ 的桥。构造函数只收 (QObject *parent)，页面侧的 transport
    // 由 page->setWebChannel(channel) 接管，不用自己传（QWebChannel(QWebChannelPrivate&,QObject*)
    // 那个是私有的，别去碰）
    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("qt"), this);
    page->setWebChannel(channel);

    // 收流页的 console 必须接到日志里。以前页面内部报什么错完全看不见
    // （QWebChannel 建不起来时 JS 只往 console 打一行 error，C++ 侧什么都收不到），
    // 于是"为什么没出画面"只能靠猜，这一猜就是几个小时。
    // Qt6 移除了 signal 关键字，不能用 page->loadFinished.connect(...) 这种风格访问信号。
    QObject::connect(page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        if (!ok) {
            logf("[viewer] FAIL 收流页加载失败");
            setRtcState(QStringLiteral("failed"));
            return;
        }
        logf("[viewer] 收流页就绪（离屏 1280x720）");
        setRtcState(QStringLiteral("waiting"));

        // 桥到底建没建起来，必须当场问清楚：typeof window.qt / transport 是关键，
        // "hasPc=false 且毫无报错"只说明 JS 提前 return 了，看不出原因
        m_rtcView->page()->runJavaScript(
            QStringLiteral("(typeof __qtOk) + '/' + (typeof window.qt) + '/' + (window.qt ? typeof window.qt.webChannelTransport : 'none') + '/logs=' + JSON.stringify(window.__logs.slice(0, 6))"),
            [](const QVariant &v) {
                logf("[viewer] 收流页桥状态 __qtOk/qt/transport = %s", qPrintable(v.toString()));
            });
        // 页面还没 load 完时 runJavaScript 是对着 about:blank 执行的，window.__setOffer 不存在，
        // 调用会**静默失败**（C++ 侧连个错都收不到）→ 表现为"收到 offer 了但毫无反应"。
        // ⚠️ 顺序陷阱（2026-10-05 修）：m_rtcPageReady 必须在 offer 补灌**之前**置位。
        // deliverOffer() 内部拿这个标志决定"直接灌 JS"还是"再存回 m_pendingOffer 等下一轮"，
        // 原先这行写在 offer 补灌之后 → 补灌那一下永远判成"页面没就绪" → offer 被原样塞回队列，
        // pc 从头到尾建不起来，后面攒的候选只能喂给 null pc，日志里只留两行 ice-before-pc。
        m_rtcPageReady = true;

        // 远端候选补灌必须排在 offer 之后：页面里的 pc 是 __setOffer 里同步 new 出来的，
        // 顺序反了 addIceCandidate 就会撞上 "pc 还不存在"，JS 侧只记一行 ice-before-pc 然后丢掉，
        // 候选就这么无声无息少了一批，ICE 死活连不上还查不出原因。
        if (!m_pendingOffer.isEmpty()) {
            const QString sdp = m_pendingOffer;
            m_pendingOffer.clear();
            deliverOffer(sdp);
        }

        if (!m_pendingIce.isEmpty()) {
            const QStringList queued = m_pendingIce;
            m_pendingIce.clear();
            for (const QString &c : queued)
                addRemoteIce(c);
            logf("[viewer] RTC 收流页就绪后补灌远端 ICE 候选 %d 个", queued.size());
        }

        // 抽帧节拍：25fps 上限，但真帧率受被控端推流 fps 限制。
        // 用成员 timer（2026-10-06 占用优化）：之前每次建页都 new 两个挂在本对象上的 timer，
        // 收流页回收时不停，一轮重连留两个空转孤儿，攒多了 tick 全是白检 nullptr。
        // 拿到页面所有权先复位，免得上一轮的旧 timer 还咬着已经 deleteLater 的 view。
        stopRtcTimers();
        if (!m_grabTimer) m_grabTimer = new QTimer(this);
        QObject::connect(m_grabTimer, &QTimer::timeout, this, [this] {
            if (m_rtcView) m_rtcView->page()->runJavaScript(QStringLiteral("window.__grab()"));
        });
        m_grabTimer->start(40);
        // 每 2 秒捞一次收流页内部状态：pc 建没建、ICE 走到哪、视频轨有没有、解码出多大画面。
        // 没有这个，画面不出来时你只能看到"没日志"，看不出卡在 offer/answer/ice 哪一步。
        if (!m_rtcDiagTimer) m_rtcDiagTimer = new QTimer(this);
        QObject::connect(m_rtcDiagTimer, &QTimer::timeout, this, [this] {
            if (!m_rtcView) return;
            m_rtcView->page()->runJavaScript(QStringLiteral("window.__diag()"),
                                             [](const QVariant &v) {
                                                 const QString s = v.toString();
                                                 if (!s.isEmpty() && s != QStringLiteral("undefined"))
                                                     logf("[viewer] 收流页状态 %s", qPrintable(s));
                                             });
        });
        m_rtcDiagTimer->start(2000);
    });

    page->load(QUrl::fromLocalFile(htmlPath));
}

/** 诊断/日志（JS → C++）。 */
void ViewerBackend::rtcDiag(const QString &s)
{
    logf("[viewer] RTC %s", s.toUtf8().constData());
    if (m_rtcState == QStringLiteral("waiting") &&
        !s.contains(QStringLiteral("REJECT")) && !s.contains(QStringLiteral("FAIL"))) {
        // 进入协商期
        setRtcState(QStringLiteral("negotiating"));
    }
}

/**
 * JS 侧 createAnswer 产出的 answer → **回云端给被控端**。
 *
 * 这里原本只有一行 "收到意外的 rtc-answer（管理端不产出 answer）" —— 那是照着"管理端是答案产出方"
 * 倒推的错判：真正的分工是**被控端 offer 侧、管理端 answer 侧**，被控端在等我们这份 answer 才能
 * 走完 setRemoteDescription → 触发 ontrack。answer 不回云端 = 协商永远停在 have-local-offer，
 * 被控端那边静默重连、管理端这边一眼看不出问题（只有"一直黑屏"）。
 */
void ViewerBackend::rtcGotAnswer(const QString &sdp)
{
    if (sdp.isEmpty()) {
        logf("[viewer] FAIL RTC answer 是空的（createAnswer 没产出 sdp），不回云端");
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("kind"), QStringLiteral("answer"));
    p.insert(QStringLiteral("sdp"), sdp);
    p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
    sendEnvelope(QStringLiteral("rtc-answer"), p);
    logf("[viewer] RTC answer 已回云端（sdp %d 字符 → %s）", sdp.size(),
         m_currentUid.toUtf8().constData());
}

/**
 * JS 侧**本端** ICE candidate 过来：回云端给被控端。
 *
 * 少发这一半是致命的：被控端只收到自己的候选，两端候选对不上，ICE 永远完不成，
 * ontrack 不来 → 画面全黑，而两端日志看起来都挺正常（offer 收了、answer 回了）。
 *
 * ⚠️ 这里**只发云端，不回灌自己的 pc**。以前这两件事挤在同一个函数里，于是：
 *   - 本端候选被 addIceCandidate 加给自己 → Chromium 静默 ice-FAIL；
 *   - 云端来的远端候选也被同一个函数处理 → 被原样 sendEnvelope 回发给被控端，
 *     被控端收到自己刚发出去的候选，形成回环放大。
 * 两个方向现在彻底分开：本端走这里，远端走 addRemoteIce()。
 */
void ViewerBackend::rtcGotIce(const QString &candJson)
{
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("kind"), QStringLiteral("ice"));
    p.insert(QStringLiteral("candidate"), candJson);
    p.insert(QStringLiteral("from"), QStringLiteral("viewer"));
    sendEnvelope(QStringLiteral("rtc-ice"), p);
}

void ViewerBackend::addRemoteIce(const QString &candJson)
{
    // 判断"页面能不能灌"必须看 m_rtcPageReady，不能看 m_rtcView 是否非空 ——
    // view 对象一 new 出来就非空，但 page->load() 是异步的，那段时间 runJavaScript
    // 对着 about:blank 执行，window.__addIce 不存在，调用**静默失败**，
    // 这就是 "window.__addIce is not a function" 的来源。
    if (!m_rtcView || !m_rtcPageReady) {
        if (m_pendingIce.size() >= kMaxPendingIce) {
            logf("[viewer] WARN 远端 ICE 队列已满（%d 个），丢弃后来的候选 —— 对端可能在反复重连",
                 m_pendingIce.size());
            return;
        }
        m_pendingIce.append(candJson);
        return;
    }
    const QByteArray b64 = candJson.toUtf8().toBase64();   // 同 __setOffer：base64 注入，杜绝字面量语法错
    m_rtcView->page()->runJavaScript(QStringLiteral("window.__addIce('%1');")
                                     .arg(QString::fromLatin1(b64)));
}

void ViewerBackend::deliverOffer(const QString &sdp)
{
    if (!m_rtcView || !m_rtcPageReady) {
        m_pendingOffer = sdp;   // 页面还没就绪，等 loadFinished 补灌
        return;
    }
    // 先拆旧 pc：被控端每个 offer 都是一个全新 peer（见 JS 里 __resetPc 的说明）。
    // 两条 runJavaScript 按调用顺序在页面里排队执行，所以 reset 一定先于 setOffer。
    m_rtcView->page()->runJavaScript(QStringLiteral("window.__resetPc()"));
    // 走 base64 而不是 JSON 字符串字面量：SDP 里全是 CRLF，任何手写/库转义出的
    // 字面量都可能被 Chromium 判成非法 token，一旦炸就是 "Uncaught SyntaxError"，
    // 而且报在 runJavaScript 的注入串上、页面里一点痕迹都没有（logs 全空）。
    // base64 只含 [A-Za-z0-9+/=]，物理上不可能产生语法错。
    const QByteArray b64 = sdp.toUtf8().toBase64();
    m_rtcView->page()->runJavaScript(
        QStringLiteral("window.__setOffer('%1');").arg(QString::fromLatin1(b64)));
    logf("[viewer] RTC offer 已灌进收流页（sdp %d 字符 → base64 %d）", sdp.size(), b64.size());
}

/**
 * 真删离屏收流页。
 * deleteLater 而不是 delete：page 上还排着 runJavaScript 回调（补灌候选、抽帧都走它），
 * 当场删会打在半路。析构里统一收口（见 ~ViewerBackend）。
 */
void ViewerBackend::releaseRtcView()
{
    if (!m_rtcView) return;
    // 节拍先停表：页面上已经排进 Chromium 主线程的 __grab()/__diag() 调用还挂着，
    // 不停的话 view 一销毁它们就打在已释放的 page 上。
    stopRtcTimers();
    m_rtcView->close();          // 不给 close 的话 Chromium 的渲染进程不退出
    m_rtcView->deleteLater();
    m_rtcView = nullptr;
    m_rtcPageReady = false;      // 必须一起清：下次靠它判"这页能不能灌 JS"
    m_pendingOffer.clear();
    m_pendingIce.clear();
    setRtcState(QStringLiteral("idle"));
    logf("[viewer] 收流页已回收（Chromium 渲染进程随之退出）");
}

void ViewerBackend::cancelRtcViewReap()
{
    if (m_rtcReap && m_rtcReap->isActive()) m_rtcReap->stop();
}

void ViewerBackend::stopRtcTimers()
{
    if (m_grabTimer) m_grabTimer->stop();
    if (m_rtcDiagTimer) m_rtcDiagTimer->stop();
}

// 延迟回收：给"刚断又马上重连"留窗口，避免断线抖动时反复重建 Chromium（重建一次几百毫秒）。
// 真正等多久由 STE_RTC_IDLE_MS（默认 20 秒）决定，从"跟云端断开"那一刻开始算。
void ViewerBackend::scheduleRtcViewReap()
{
    if (!m_rtcReap) {
        m_rtcReap = new QTimer(this);
        m_rtcReap->setSingleShot(true);
        m_rtcReap->setInterval(5000);
        QObject::connect(m_rtcReap, &QTimer::timeout, this, [this]() { releaseRtcView(); });
    }
    m_rtcReap->stop();
    if (!m_offlineSince) return;          // 还在线上，没什么可收的
    // 空 = 默认 20 秒；显式写 0 = 关掉（别让默认值把开关吃掉）
    const QByteArray envIdle = qgetenv("STE_RTC_IDLE_MS");
    const qint64 limit = envIdle.isEmpty() ? 20000 : QString::fromLocal8Bit(envIdle).toLongLong();
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_offlineSince;
    if (limit <= 0) return;
    if (elapsed >= limit) { releaseRtcView(); return; }   // 早就算超了，立刻收
    m_rtcReap->setInterval((int)(limit - elapsed));
    m_rtcReap->start();
}

/** JS 侧 ontrack 触发：真正拿到远端视频轨。 */
void ViewerBackend::rtcGotTrack()
{
    logf("[viewer] ✅ RTC ontrack：拿到远端视频轨");
    setRtcState(QStringLiteral("track"));
}

/**
 * JS → C++：抽到一帧 JPEG（base64，可能带 data: 前缀）。
 * 复用现有 frame 通道（走 applyFrameBytes → frameChanged），所以 QML 那边一行都不用改。
 */
void ViewerBackend::setRtcFrame(const QString &b64)
{
    if (b64 == QStringLiteral("EMPTY") || b64.isEmpty()) return;
    const QByteArray raw = QByteArray::fromBase64(
        b64.startsWith(QStringLiteral("data:")) ? b64.mid(b64.indexOf(QLatin1Char(',') ) + 1).toUtf8()
                                                 : b64.toUtf8());
    if (raw.isEmpty()) return;
    applyFrameBytes(raw, QJsonObject(), RtcGrab);
}

void ViewerBackend::setStatus(const QString &s, bool warn)
{
    if (m_statusText == s && m_statusWarn == warn) return;
    m_statusText = s;
    m_statusWarn = warn;
    emit statusTextChanged();
}

bool ViewerBackend::hasAnyCredential() const
{
    return !m_cloudTicket.isEmpty() || !m_token.isEmpty();
}

int ViewerBackend::retryDelayMs() const
{
    // 5s → 10s → 20s → 30s 封顶。断线时先快速补一次（网络抖一下就回来了），
    // 一直不通就退到分钟级 —— 常驻程序在后台空转连一个连不上的地址，
    // 除了把日志刷爆没有任何收益，还会把真正的错误挤出去。
    return qMin(30000, 5000 * (1 << qMin(m_retryCount, 2)));
}

void ViewerBackend::connectToCloud()
{
    if (!m_ws) return;
    // 没凭据就别连了（2026-10-06 打磨：这是重连风暴的真正根因）。
    // 以前"既没有 ticket 也没有 token"也要硬连，云端回一句鉴权失败，
    // 界面再 5 秒重试一次，日志里就是几十轮一模一样的 "已连上云端 → 正在鉴权 → FAIL"。
    // 站在用户那边看：屏幕上一句"没登录"就够了，不需要后面的循环。
    if (!hasAnyCredential()) {
        if (!m_retryStopped) {
            m_retryStopped = true;
            logf("[viewer] 手上没有云端凭据（没登录），已停在那儿等用户登录——不再空转重连");
            setStatus(QStringLiteral("没登录，连不上云端。点右上角「登录」用星璃账号进去"),
                      true);
            emit authFailed(QStringLiteral("未登录"));
        }
        return;
    }
    m_retryStopped = false;
    m_ws->close();
    m_ws->open(QUrl(m_url));
    logf("[viewer] 正在连云端 %s", m_url.toUtf8().constData());
}

void ViewerBackend::sendEnvelope(const QString &type, const QJsonObject &payload)
{
    if (!m_ws) return;
    m_ws->sendTextMessage(makeEnvelope(type, payload));
}

void ViewerBackend::onConnected()
{
    m_connected = true;
    m_offlineSince = 0;      // 离线看门狗解除（配合 onDisconnected 里的回收）
    if (m_rtcReap && m_rtcReap->isActive()) cancelRtcViewReap();
    emit connectedChanged();
    logf("[viewer] 已连上云端 %s，正在鉴权…", m_url.toUtf8().constData());
    setStatus(QStringLiteral("已连上云端，正在鉴权…"), false);
    m_authed = false;
    emit authedChanged();
    // 云端要求第一条消息必须是 auth（v1 信封），否则直接拒收（鉴权前不推任何设备表/画面）。
    // 手里有没过期的接入票就发票，一张都没有才退回静态令牌 ——
    // 两条通道云端一直都认（详见 cloud-ws/src/index.js 的 authorizeViewer）。
    QJsonObject p;
    pickTicketOrToken(p);
    sendEnvelope(QStringLiteral("auth"), p);
}

void ViewerBackend::onDisconnected()
{
    m_connected = false;
    // 离线看门狗：跟云端断了这么久还没连回来，收流页里那个 Chromium 渲染进程（~137MB）
    // 已经送不出任何画面，留着纯占内存。判据用"跟云端断多久"而不是"多久没收到信令"——
    // 画面稳定后对端本来就不发东西，按后者数会误杀正在看的画面。
    // 阈值可用 STE_RTC_IDLE_MS 覆盖（毫秒，默认 20000），排障时调小值能快验。
    // 只认**第一次**断连：下面每次重连失败都会再走到 onDisconnected，
    // 起点刷新一次倒计时就往后推一次，看门狗永远走不到头（实测断线 26 秒、重连失败 3 次没触发）。
    if (!m_offlineSince) m_offlineSince = QDateTime::currentMSecsSinceEpoch();
    scheduleRtcViewReap();
    emit connectedChanged();
    // 「设备离线时提醒」：断线是常驻程序最该让用户知道的一件事，
    // 以前只在日志里留一行，用户得盯着屏幕才知道"怎么没画面了"。
    if (m_notifyOnOffline && !m_retryStopped && !m_authFailed) {
        notifyPref(QStringLiteral("与云端断开了，正在自动重连…"));
    }

    if (m_authFailed || m_retryStopped) {
        // 鉴权失败/已停机重连再连只会一遍遍失败：把原因留在屏幕上，别循环
        logf("[viewer] FAIL 鉴权没通过，已停止重连：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端鉴权失败，已停止重连：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        return;
    }
    ++m_retryCount;
    const int delay = retryDelayMs();
    logf("[viewer] FAIL 与云端断开，%d 秒后重连（第 %d 次）", delay / 1000, m_retryCount);
    setStatus(QStringLiteral("与云端断开，%1 秒后重连…").arg(delay / 1000), true);
    QTimer::singleShot(delay, this, [this] { connectToCloud(); });
}

void ViewerBackend::requestDevices()
{
    sendEnvelope(QStringLiteral("devices"), QJsonObject());
}

/**
 * 自检用：订 → 退 → 再订，逼被控端重建连接重新 offer。
 *
 * 顺序不能省也不能乱：退订必须先发生在"已经订上了"之后，否则云端那条 rtc-stop
 * 落到"本来就没订阅"上直接空转，被控端的 g_rtcOn 一直是 true，
 * 后面再收到 rtc-start 时 rtcStart() 一句 `if (g_rtcOn && g_rtcView) return` 就原地返回，
 * 一个 offer 都不发 —— 表现还是"订阅成功但永远不来画面"。
 * 退订让云端发 rtc-stop（被控端 g_rtcOn=false 且页面 __reset），
 * 再订让云端发 rtc-start（被控端重建 RTCPeerConnection → 新 offer）。
 */
void ViewerBackend::rtcRenegotiate(const QString &uid)
{
    QJsonObject sp;
    sp.insert(QStringLiteral("uid"), uid);
    logf("[viewer] 自检：订 %s →（1.5s 后）退订 →（0.8s 后）重订，逼它重新 offer",
         uid.toUtf8().constData());
    // 必须走 setCurrentUid 而不是直接发 subscribe：它同时会把 m_currentUid 改掉，
    // 之后 answer / 本端 ICE 回云端时才知道该投给谁（直接发信封的话会投到上一台设备上）
    m_currentUid = uid;
    emit currentUidChanged();
    sendEnvelope(QStringLiteral("subscribe"), sp);
    QTimer::singleShot(1500, this, [this, uid] {
        QJsonObject s;
        s.insert(QStringLiteral("uid"), uid);
        sendEnvelope(QStringLiteral("unsubscribe"), s);
        logf("[viewer] 自检：退订 %s（云端应下发 rtc-stop）", uid.toUtf8().constData());
    });
    QTimer::singleShot(2300, this, [this, uid] {
        QJsonObject s;
        s.insert(QStringLiteral("uid"), uid);
        sendEnvelope(QStringLiteral("subscribe"), s);
        logf("[viewer] 自检：重订 %s（云端应下发 rtc-start）", uid.toUtf8().constData());
    });
}

void ViewerBackend::setCurrentUid(const QString &uid)
{
    if (uid.isEmpty() || uid == m_currentUid) return;
    m_currentUid = uid;
    refreshCapActions();          // 换机器 = 换一套能力，门控要立刻跟着变
    emit currentUidChanged();
    logf("[viewer] 切到 %s，向云端订阅它的画面", uid.toUtf8().constData());
    QJsonObject sp;
    sp.insert(QStringLiteral("uid"), uid);
    sendEnvelope(QStringLiteral("subscribe"), sp);
}

void ViewerBackend::onTextMessage(const QString &text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        logf("[viewer] FAIL 云端消息解析不出来：%s", text.left(160).toUtf8().constData());
        return;
    }
    const QJsonObject o = doc.object();
    // v1 信封与旧扁平格式都认：有 v 字段就取 payload，否则整条就是旧扁平结构
    const bool isV1 = o.contains(QStringLiteral("v"));
    const QJsonObject pay = isV1 ? o.value(QStringLiteral("payload")).toObject() : o;
    const QString type = o.value(QStringLiteral("type")).toString();

    // RTC 报文级流水：offer 到没到、ice 到没到、answer 回没回，靠的就是这几行。
    // 以前只有"成功/失败"两种日志，缺中间态，排障只能靠猜。
    if (type.startsWith(QStringLiteral("rtc-"))) {
        logf("[viewer] 云端 RTC 报文到了 type=%s len=%d uid=%s sdp=%d cand=%d payload=%d",
             type.toUtf8().constData(), text.size(),
             pay.value(QStringLiteral("uid")).toString().toUtf8().constData(),
             pay.value(QStringLiteral("sdp")).toString().size(),
             pay.value(QStringLiteral("candidate")).toString().size(),
             pay.value(QStringLiteral("payload")).toObject().size());
    }

    if (type == QStringLiteral("authed") || type == QStringLiteral("auth-ok")) {
        m_authed = true;
        m_retryCount = 0;           // 通了就把退避计清零，下一次断线还是 5 秒起步
        m_retryStopped = false;
        emit authedChanged();
        logf("[viewer] ✅ 云端鉴权通过（mode=%s）",
             pay.value(QStringLiteral("mode")).toString().toUtf8().constData());
        setStatus(QStringLiteral("已连上云端，等教室机推画面…"), false);
        requestDevices();   // 鉴权过后再拉设备表（之前是连上就拉，现在会被云端拒）
    } else if (type == QStringLiteral("auth-fail")) {
        m_authFailReason = pay.value(QStringLiteral("reason")).toString();
        // 拿票连的、却被拒 —— 票可能是过期的、也可能是站点那边换了密钥（旧票仍签得出但验不过）。
        // 这种情况下"停起重连"没意义（票还是那张票），正确做法是把票废掉、逼一次重新登录，
        // 由用户在浏览器里重新同意一次拿到新票。别把人卡在一条死路上。
        if (!m_cloudTicket.isEmpty()) {
            logf("[viewer] FAIL 接入票被云端拒（%s）→ 废掉这张票，需要重新用网站账号登录",
                 m_authFailReason.toUtf8().constData());
            m_cloudTicket.clear();
            m_ticketExp = 0;
            m_accountFatal = true;
            m_accountText = QStringLiteral("接入票失效，请重新登录");
            emit accountChanged();
            saveAccount();
            // ⚠️ 2026-10-06：这里必须停机重连。旧代码只 close()，紧接着被 onDisconnected
            // 重新排了一次 5 秒重连 → 那张（已清掉的）票换了个姿势再来一遍 →
            // 日志里就是几十轮 "已连上云端 → 正在鉴权 → auth-fail → 关闭 → 5 秒后重连"，
            // 界面上看着像"程序自己在抽风"。票废了就该停在这儿等用户重新授权。
            m_retryStopped = true;
            m_authFailReason = QStringLiteral("接入票失效（%1）").arg(m_authFailReason);
            setStatus(QStringLiteral("接入票失效，请点右上角重新登录"), true);
            emit authFailed(m_authFailReason);
            if (m_ws) m_ws->close();
            return;
        }
        m_authFailed = true;
        m_retryStopped = true;      // 同 onDisconnected：拒绝一次就是拒绝一路，别硬碰
        logf("[viewer] FAIL 云端拒绝鉴权：%s", m_authFailReason.toUtf8().constData());
        setStatus(QStringLiteral("云端拒绝鉴权：") + m_authFailReason, true);
        emit authFailed(m_authFailReason);
        if (m_ws) m_ws->close();
    } else if (type == QStringLiteral("error")) {
        // 统一错误通道（v1）：云端拒绝必须看得见，不许静默
        // 错误码体系（2026-10-06）：ecode = 数字码（AU1001/CM4001…），code = 语义码兼容，
        // message = 人话。按 ecode 决定 UI 行为（认证类强制重登），其余展示 message。
        const QString ecode = pay.value(QStringLiteral("ecode")).toString();
        const QString code = pay.value(QStringLiteral("code")).toString();
        const QString message = pay.value(QStringLiteral("message")).toString();
        logf("[viewer] FAIL 云端拒绝 → %s/%s：%s",
             ecode.toUtf8().constData(), code.toUtf8().constData(), message.toUtf8().constData());
        // 认证失效（AU1002 过期 / AU1003 无效）：旧令牌已不可用，直接走重登流程，
        // 避免"看起来还连着，但每条指令都被拒"的假在线状态。
        if (ecode == QLatin1String("AU1002") || ecode == QLatin1String("AU1003")) {
            m_authFailReason = message;
            m_authFailed = true;
            m_retryStopped = true;
            setStatus(QStringLiteral("登录已过期，请重新登录"), true);
            emit authFailed(message);
            if (m_ws) m_ws->close();
            return;
        }
        setStatus(QStringLiteral("云端拒绝：") + message, true);
    } else if (type == QStringLiteral("viewer-ready")) {
        logf("[viewer] 云端回话：管理端在线（云端在线管理端 %d 个）",
             pay.value(QStringLiteral("viewers")).toInt(-1));
    } else if (type == QStringLiteral("devices")) {
        refreshDevices(pay.value(QStringLiteral("devices")).toArray());
    } else if (type == QStringLiteral("subscribed")) {
        logf("[viewer] 云端确认订阅 %s", pay.value(QStringLiteral("uid")).toString().toUtf8().constData());
    } else if (type == QStringLiteral("rtc-offer")) {
        // 被控端发起了建连：把它带过来的 offer 灌进离屏 QWebEngineView，
        // setRemoteDescription 之后 createAnswer 回云端（__setOffer 里做完了全流程）
        //
        // ⚠️ 必须先把 offer 里的 uid 同步到 m_currentUid（2026-10-05 修）。
        // offer 携带的 uid 是"这条协商属于哪台机器"的**权威来源**，比本地的设备表可靠：
        // rtcGotAnswer() / rtcGotIce() 回云端时都只认 m_currentUid，而它可能被
        // refreshDevices() 在"设备表短暂为空"（云端重启/被控端掉线重连）时清空，
        // 或者 viewer 刚起来设备表还没到就是空的。那样 answer 会带着空 uid 回去，
        // 云端 devices.get("") 找不到设备 → answer 静默丢在云端，
        // 被控端 offeredAt 一直不变、answeredAt 永远是 0，卡在 pc-failed 无限重发 offer。
        // 现象是"两端都连上了、JPEG 兜底画面在动，就是 RTC 永远建不起来"。
        const QString offerUid = rtcField(pay, QLatin1String("uid"));
        if (!offerUid.isEmpty() && offerUid != m_currentUid) {
            logf("[viewer] RTC offer 带的 uid=%s 与本地当前 uid=%s 不一致，以 offer 为准修正",
                 offerUid.toUtf8().constData(), m_currentUid.toUtf8().constData());
            m_currentUid = offerUid;
            emit currentUidChanged();
        }
        initRtcView();
        const QString sdp = rtcField(pay, QLatin1String("sdp"));
        if (sdp.isEmpty()) {
            // 失败也要把来路摊开：哪台机器、整条消息长啥样（截断），否则"没有 sdp"就没法往下查
            logf("[viewer] FAIL 收到 rtc-offer 但没有 sdp 字段（uid=%s 原始=%s）",
                 pay.value(QStringLiteral("uid")).toString().toUtf8().constData(),
                 text.left(220).toUtf8().constData());
            setRtcState(QStringLiteral("failed"));
        } else {
            // 新 offer = 新的一轮协商，上一轮攒下的候选已经作废，先清干净再灌。
            // 不清的话，上次那台机器的候选会被喂给新建的 pc，ICE 往一个不存在的对端上撞。
            if (!m_pendingIce.isEmpty()) {
                logf("[viewer] RTC 收到新 offer，丢弃上一轮残留的 %d 个候选", m_pendingIce.size());
                m_pendingIce.clear();
            }
            deliverOffer(sdp);   // 页面就绪就直接灌，没就绪就暂存等补灌
        }
    } else if (type == QStringLiteral("rtc-ice")) {
        // 被控端的 ICE candidate → 转给离屏 view 里的 RTCPeerConnection
        //
        // 同样以报文里的 uid 为准（见上面 rtc-offer 分支的长注释）：ICE 可能**先于 offer** 到达，
        // 那一刻 m_currentUid 很可能还是空的，不补的话本端候选回给云端时 uid 也是空 → 丢。
        const QString iceUid = rtcField(pay, QLatin1String("uid"));
        if (!iceUid.isEmpty() && iceUid != m_currentUid) {
            m_currentUid = iceUid;
            emit currentUidChanged();
        }
        const QString cand = rtcField(pay, QLatin1String("candidate"));
        if (cand.isEmpty()) {
            // 同样把原始报文摊开：candidate 空有三种来路（被控端发了空串、包了两层没剥到、
            // 云端转发时被砍了），只一句"是空的"分不清是哪一种
            logf("[viewer] WARN 收到 rtc-ice 但 candidate 是空的（原始=%s）",
                 text.left(200).toUtf8().constData());
        } else {
            // 和 offer 一样懒建：云端转发顺序不保证 offer 一定先到，ICE 抢先是可能的。
            // 以前这种情况直接丢弃 → "候选平白少一半、ICE 怎么都连不上"，现在建页并排队。
            if (!m_rtcView) {
                logf("[viewer] RTC 收到 ice 但收流页还没建（offer 未先到），先建页再排队");
                initRtcView();
            }
            addRemoteIce(cand);
        }
    } else if (type == QStringLiteral("rtc-answer")) {
        logf("[viewer] 收到意外的 rtc-answer（管理端是 answer 侧，不该收到）");
    } else if (type == QStringLiteral("rtc-start") || type == QStringLiteral("rtc-stop")) {
        // rtc-stop 就是"没人看了"的确切信号：收流页留 5 秒没人来就回收（省那 137MB）。
        // rtc-start 不用管回收 —— 下一份 offer 到达时 initRtcView() 自己会建回来。
        if (type == QStringLiteral("rtc-stop")) scheduleRtcViewReap();
        logf("[viewer] 云端广播 %s（推流开关，管理端据此%s收流页）",
             type.toUtf8().constData(),
             type == QStringLiteral("rtc-stop") ? "回收" : "保持");
    } else if (type == QStringLiteral("rtc-relayed")) {
        // 云端回给我们自己的信令投递结果——排查"发出去到底到没到"就靠这条
        logf("[viewer] RTC 信令已中继 type=%s ok=%s to=%s %s",
             pay.value(QStringLiteral("type")).toString().toUtf8().constData(),
             (pay.value(QStringLiteral("ok")).toBool() ? "true" : "false"),
             pay.value(QStringLiteral("to")).toString().toUtf8().constData(),
             pay.value(QStringLiteral("detail")).toString().toUtf8().constData());
    } else if (type == QStringLiteral("frame")) {
        // 兼容旧文本 frame（base64）；v1 客户端走的是二进制帧分支
        const QByteArray b64 = QByteArray::fromBase64(pay.value(QStringLiteral("data")).toString().toUtf8());
        if (b64.isEmpty()) logf("[viewer] FAIL 收到一帧但内容是空的（云端推了空 base64）");
        else applyFrameBytes(b64, QJsonObject());
    } else if (type == QStringLiteral("terminal_opened")) {
        // 会话真开起来了。被控端这侧已经在往外吐 terminal_data，这里只负责转给界面。
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        // 只认当前会话的帧：迟到/串台的帧直接丢，别糊进窗口里
        if (m_termState != TermOpen || sid != m_termSid) {
            logf("[viewer] WARN 收到 terminal_opened 但本机没在等（state=%d sid=%s our=%s）",
                 (int)m_termState, sid.toUtf8().constData(), m_termSid.toUtf8().constData());
            return;
        }
        setTermState(TermOpen, sid, QString());
    } else if (type == QStringLiteral("terminal_data")) {
        if (m_termState != TermOpen) return;
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        if (sid != m_termSid) return;
        const QString data = pay.value(QStringLiteral("data")).toString();
        if (data.isEmpty()) return;
        // 被控端拿本地代码页编的字节，云端原样转；这里按本地八位还原即可，
        // 换行符留原样（被控端发的是 \r\n），不然回显会挤成一行。
        emit terminalData(sid, data);
    } else if (type == QStringLiteral("terminal_exit")) {
        if (m_termState != TermOpen) return;
        emit terminalExit(m_termSid, pay.value(QStringLiteral("code")).toInt(-1),
                          pay.value(QStringLiteral("ms")).toInt(0));
    } else if (type == QStringLiteral("terminal_closed")) {
        // 没开过会话时也照转：本机拒绝走的就是这条（状态从 Pending 直接回 Idle），
        // 界面要靠它把"点了没反应"变成一句"被本机拒绝了"。
        const QString sid = pay.value(QStringLiteral("sid")).toString();
        const QString reason = pay.value(QStringLiteral("reason")).toString();
        setTermState(TermIdle, sid, reason);
        emit terminalClosed(sid, reason);
    } else if (type == QStringLiteral("instruction-result")) {
        const QString state = pay.value(QStringLiteral("state")).toString();
        const QString action = pay.value(QStringLiteral("action")).toString();
        const QString uid = pay.value(QStringLiteral("uid")).toString();
        const QString detail = pay.value(QStringLiteral("detail")).toString();
        const QString result = pay.value(QStringLiteral("result")).toString();
        const QString err = pay.value(QStringLiteral("error")).toString();
        QJsonObject data = pay.value(QStringLiteral("data")).toObject();
        logf("[viewer] 指令 %s → %s（%s）", action.toUtf8().constData(),
             state.toUtf8().constData(), detail.toUtf8().constData());
        // 探活的往返时间只有 backend 知道（发出时刻记在这儿），算好塞进 data 给界面用
        if (action == QStringLiteral("ping") && m_pingSentMs > 0) {
            data.insert(QStringLiteral("rttMs"),
                        (double)(QDateTime::currentMSecsSinceEpoch() - m_pingSentMs));
        }
        // 「操作完成时提示」这个开关接在这儿：机器真做了（executed）才响，
        // 云端"收下了"不算 —— 收下了但机器没做，用户等到的却是"完成了"，那 worse than 不提示。
        if (state == QStringLiteral("executed") && m_notifyOnDone) {
            notifyPref(err.isEmpty()
                           ? QStringLiteral("%1 在 %2 上完成了").arg(action, uid)
                           : QStringLiteral("%1 在 %2 上失败：%3").arg(action, uid, err));
        }
        emit resultReceived(uid, action, state, result, err, detail, data);
    } else {
        logf("[viewer] 收到未处理的云端消息 type=%s", type.toUtf8().constData());
    }
}

void ViewerBackend::onBinaryMessage(const QByteArray &buf)
{
    // v1 帧：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG]
    if (buf.size() >= 4 && (quint8)buf.at(0) == 1) {
        const int hLen = ((unsigned char)buf.at(1) << 8) | (unsigned char)buf.at(2);
        if (hLen > 0 && hLen <= kFrameHeaderMax && buf.size() >= 3 + hLen) {
            const QJsonDocument hd = QJsonDocument::fromJson(buf.mid(3, hLen));
            if (!hd.isObject()) { logf("[viewer] FAIL 帧头解析失败（非 JSON）"); return; }
            const QJsonObject h = hd.object();
            if (h.value(QStringLiteral("type")).toString() != QStringLiteral("frame")) {
                logf("[viewer] FAIL 帧头 type 不是 frame");
                return;
            }
            applyFrameBytes(buf.mid(3 + hLen), h);
            return;
        }
    }
    // 兜底：旧裸帧（无头），整块即 JPEG
    applyFrameBytes(buf, QJsonObject());
}

/**
 * 把当前选中设备的能力清单抽出来给 QML 用（2026-10-06 契合度改造）。
 * 设备的 actions 是云端 devices 广播里带下来的（云端原样转发被控端 register 的 caps.actions）。
 * 老版本被控端/老云端都不带这个字段 → m_capActions 保持空，见 deviceSupports 的兜底。
 */
void ViewerBackend::refreshCapActions()
{
    const QStringList before = m_capActions;
    m_capActions.clear();
    if (!m_currentUid.isEmpty()) {
        for (const QJsonValue &v : m_devices) {
            const QJsonObject d = v.toObject();
            if (d.value(QStringLiteral("uid")).toString() != m_currentUid) continue;
            const QJsonValue acts = d.value(QStringLiteral("actions"));
            if (acts.isArray()) {
                for (const QJsonValue &a : acts.toArray()) {
                    const QString s = a.toString();
                    if (!s.isEmpty()) m_capActions.append(s);
                }
            }
            break;
        }
    }
    if (m_capActions != before) emit capActionsChanged();
}

void ViewerBackend::reportUnsupported(const QString &label, const QString &action)
{
    logf("[viewer] 拦下「%s」：这台被控端的能力清单里没有 %s",
         label.toUtf8().constData(), action.toUtf8().constData());
    setStatus(QStringLiteral("这台被控端不支持「%1」（它没上报 %2 这个能力）").arg(label).arg(action), true);
}

/**
 * 权限被拦下的反馈（和 reportUnsupported 成对：那边是"机器不支持"，
 *  这边是"人没这个身份"—— 两种灰按钮的解释必须一样直白）。
 * 不静默：拦了就得说一句，否则用户只会觉得"点了没反应"。
 */
void ViewerBackend::reportDenied(const QString &label)
{
    logf("[viewer] 拦下「%s」：当前身份（%s）做不了这件事",
         label.toUtf8().constData(), m_role.toUtf8().constData());
    setStatus(QStringLiteral("「%1」要管理员身份才做得到（点右上角账户就能切）").arg(label), true);
}

bool ViewerBackend::deviceSupports(const QString &action) const
{
    if (action.isEmpty()) return false;
    // 没能力表就一律放开：老版本被控端没上报 actions（或云端还没升级），
    // 这时候门控不能反过来把整排按钮锁死 —— 那比不门控更糟，老师会以为软件坏了。
    // 门控只用来"少让人白点一下"，不是用来拦人的。
    if (m_capActions.isEmpty()) return true;
    return m_capActions.contains(action);
}

void ViewerBackend::refreshDevices(const QJsonArray &arr)
{
    const bool countChanged = (arr.size() != m_devices.size());
    m_devices = arr;
    if (countChanged) logf("[viewer] 设备表更新：在线 %d 台", (int)arr.size());
    refreshCapActions();          // 设备一变（新增/掉线/换 uid），能力表就要重算
    emit devicesChanged();

    if (arr.isEmpty()) {
        if (!m_currentUid.isEmpty()) {
            m_currentUid.clear();
            emit currentUidChanged();
        }
        return;
    }
    // 第一台上线就默认看它（和云端 autoSubscribe 一个口径）
    if (m_currentUid.isEmpty()) {
        setCurrentUid(arr.first().toObject().value(QStringLiteral("uid")).toString());
    }
}

void ViewerBackend::applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header,
                                    FrameSource src)
{
    if (jpeg.isEmpty()) {
        logf("[viewer] FAIL 收到一帧但内容是空的");
        return;
    }

    // ── 两路取其一：RTC 活着的时候，JPEG 轮询帧一律不上屏 ──
    // 被控端现在**同时**在推旧 JPEG 通道和 WebRTC 流（迁移期的现实状况），
    // 两边都往同一个 m_frame 写，画面就在两张不同毫秒级的截图之间来回跳、
    // fps 和字节数虚高一倍，用户看到的是"画面有点飘"，日志却一切正常。
    // 这里定死优先级：RTC 优先；RTC 断流超过 kRtcStaleMs 自动降级回 JPEG
    // （教室网络抖一下就黑屏是不可接受的，宁可降画质也不能没画面）。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (src == RtcGrab) {
        m_rtcLastFrameMs = now;
    } else if (m_rtcLastFrameMs > 0 && now - m_rtcLastFrameMs < kRtcStaleMs) {
        static int swallow = 0;
        if (++swallow == 1 || swallow % 100 == 0) {
            logf("[viewer] RTC 正在出帧，旧 JPEG 通道的帧不上屏（已让行 %d 帧）", swallow);
        }
        return;
    }

    const QString wantSource = (src == RtcGrab) ? QStringLiteral("rtc") : QStringLiteral("jpeg");
    if (m_frameSource != wantSource) {
        m_frameSource = wantSource;
        emit frameSourceChanged();
    }

    m_frameCount++;
    m_framesSinceCheck++;
    m_lastFrameBytes = jpeg.size();
    QImage img;
    if (!img.loadFromData(jpeg)) {
        logf("[viewer] FAIL 这一帧解码不出来（%lld 字节），画面保留上一帧", (long long)jpeg.size());
        emit frameDropped(QStringLiteral("这一帧解不出来，画面还是上一张"));
        emit statsChanged();
        return;
    }
    // 不在这里缩放：缩放是界面的事（QML 有自己的 Image 缩放与填充策略）
    m_frame = img;

    // ── 静态区：画面连续 kStaticStreak 帧一模一样 → 不再 emit frameChanged，
    //    界面就不换 tick，等于"这张已经画出过了，别再画一遍"。
    //    （画面内容本身没变，所以停刷显示的仍是最新内容，不是旧图。）
    const quint64 fp = frameFingerprint(img);
    m_sameFrameStreak = (fp == m_frameHash) ? (m_sameFrameStreak + 1) : 0;
    m_frameHash = fp;
    setStatic(m_sameFrameStreak >= kStaticStreak);
    if (!m_screenStatic) emit frameChanged();

    if (m_frameCount % 30 == 1) {   // 每 30 帧报一次，别把日志刷爆
        // header 空 = 旧帧（无 v1 帧头）；现在 RTC 帧不再混在这条路里，
        // 所以 "(旧帧)" 这三个字不会再误导人以为 RTC 帧走了裸帧通道
        const QString seq = header.isEmpty()
                                ? QStringLiteral("(旧帧)")
                                : QString::number(header.value(QStringLiteral("seq")).toInt());
        logf("[viewer] 画面在动[%s]：第 %d 帧（seq=%s，%d 字节，%.1f fps）",
             (src == RtcGrab ? "RTC" : "JPEG"), m_frameCount, seq.toUtf8().constData(),
             m_lastFrameBytes, m_fps);
    }
    emit statsChanged();
}

void ViewerBackend::setStatic(bool s)
{
    if (m_screenStatic == s) return;
    m_screenStatic = s;
    if (s) logf("[viewer] 画面进入静态区（连续 %d 帧一致）→ 停止重绘", kStaticStreak);
    else   logf("[viewer] 画面恢复动态 → 恢复全帧率重绘");
    emit statsChanged();
}

void ViewerBackend::tickFps()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double secs = (now - m_lastFpsCheckMs) / 1000.0;
    if (secs < 1.0) return;

    // 基准时间戳**必须无条件推进**。原来的写法把它写在 if (有帧) 里面：
    // 一旦有某个 1 秒窗口里一帧都没有（切设备、RTC 建连、被控端 momentarily 断流），
    // 基准就冻在那儿不动，下一次算出 secs = 好几秒，真实帧率被除以这段空窗 →
    // 明明 21.5fps 的流显示成 "1.0 fps"。窗口空着就报 0，别拿旧分母糊。
    m_fps = (m_framesSinceCheck > 0) ? m_framesSinceCheck / secs : 0.0;
    m_framesSinceCheck = 0;
    m_lastFpsCheckMs = now;
    emit statsChanged();
}

void ViewerBackend::sendAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，指令没发（不报错就是骗人）"), true);
        logf("[viewer] FAIL 没选设备就点 %s，不发出去", action.toUtf8().constData());
        return;
    }
    setStatus(QStringLiteral("已把「%1」发给 %2，等云端回话…").arg(action).arg(m_currentUid), false);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
}

// ──────────────────────────────────────────────────────────────────────
// 远程终端
// ──────────────────────────────────────────────────────────────────────

void ViewerBackend::setTermState(TermState s, const QString &sid, const QString &note)
{
    if (m_termState == s && m_termSid == sid && m_termNote == note) return;
    m_termState = s;
    m_termSid = sid;
    m_termNote = note;
    emit termStateChanged();
}

void ViewerBackend::sendTermAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，终端动作没发（不报错就是骗人）"), true);
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] → %s 终端动作 %s", m_currentUid.toUtf8().constData(), action.toUtf8().constData());
}

void ViewerBackend::termOpen(const QString &shell)
{
    const QString sh = shell.trimmed().toLower();
    // 只放行 cmd / powershell：和被控端一致（那边也只认这两个），别让"开了个空壳终端"
    if (sh != QStringLiteral("cmd") && sh != QStringLiteral("powershell")) {
        setStatus(QStringLiteral("不支持的 shell：%1（只认 cmd / powershell）").arg(shell), true);
        return;
    }
    if (m_termState != TermIdle) {
        setStatus(QStringLiteral("这台已经有终端会话了，先关掉再开"), true);
        return;
    }
    const QString sid = QStringLiteral("v%1").arg(++m_termSidSeq);
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("shell"), sh);
    params.insert(QStringLiteral("cols"), 120);
    params.insert(QStringLiteral("rows"), 40);
    // 空闲 10 分钟没人理就自己关：这条链路上没人看着的会话开着等于给个后门
    params.insert(QStringLiteral("idleMs"), 600000);
    params.insert(QStringLiteral("cmdMs"), 30000);
    setTermState(TermPending, sid, QStringLiteral("等本机点头…"));
    sendTermAction(QStringLiteral("terminal_open"), params);
}

void ViewerBackend::termInput(const QString &sid, const QString &keys)
{
    if (m_termState != TermOpen || sid != m_termSid) {
        setStatus(QStringLiteral("终端会话不在，命令没发（先开终端）"), true);
        return;
    }
    // 不排队：REPL 是按"一条跑完再来下一条"的口子做的，排队的命令会错序
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("keys"), keys);
    sendTermAction(QStringLiteral("terminal_input"), params);
}

void ViewerBackend::termClose()
{
    if (m_termState == TermIdle) return;
    const QString sid = m_termSid;
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    sendTermAction(QStringLiteral("terminal_close"), params);
    // 本地先收摊：等被控端回 terminal_closed 可能要几百毫秒，
    // 这期间输入框留着会让人以为还能敲（敲了也是发给一个已经关掉的会话）
    setTermState(TermPending, sid, QStringLiteral("正在关…"));
}

void ViewerBackend::sendPing()
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，探活没发"), true);
        return;
    }
    m_pingSentMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("ping"));
    sendEnvelope(QStringLiteral("instruction"), p);
    setStatus(QStringLiteral("已向 %1 发探活，等它回话…").arg(m_currentUid), false);
}

void ViewerBackend::sendPointer(const QString &kind, double nx, double ny)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发操控"); return; }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), kind);
    params.insert(QStringLiteral("x"), nx);
    params.insert(QStringLiteral("y"), ny);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 操控→ %s 在 (%d%%, %d%%)", kind.toUtf8().constData(),
         (int)(nx * 100), (int)(ny * 100));
}

// ──────────────────────────────────────────────────────────────────────
// 文件推送
// ──────────────────────────────────────────────────────────────────────

// 7 个读数（fileState/fileName/fileBytes/fileTotal/filePercent/fileError/fileTarget）
// 在头文件里内联定义了 —— 那里是唯一一处，别在这儿再写一遍（C2084 重定义）。

void ViewerBackend::clearFilePush()
{
    if (m_pushFile.isOpen()) m_pushFile.close();
    m_pushPath.clear();
    m_fileState = QStringLiteral("idle");
    m_fileName.clear();
    m_fileBytes = 0;
    m_fileTotal = 0;
    m_filePercent = 0;
    m_fileError.clear();
    m_fileTarget.clear();
    m_pushNext = 0;
    emit fileProgressChanged();
}

void ViewerBackend::setFileFail(const QString &why)
{
    m_fileState = QStringLiteral("failed");
    m_fileError = why;
    if (m_pushFile.isOpen()) m_pushFile.close();
    logf("[viewer] FAIL 文件推送中断：%s", why.toUtf8().constData());
    emit fileProgressChanged();
}

QString ViewerBackend::pickFile()
{
    const QString path = QFileDialog::getOpenFileName(
                nullptr, QStringLiteral("选要推给这台教室机的文件"), QString(),
                QStringLiteral("所有文件 (*.*)"));
    if (path.isEmpty()) return QString();
    // 统一给 file:// URL，让 pushFile 那边的 QUrl::fromUserInput 有统一入口可解
    const QString url = QUrl::fromLocalFile(path).toString();
    logf("[viewer] 选文件 → %s", url.toUtf8().constData());
    return url;
}

void ViewerBackend::pushFile(const QString &urlText)
{
    // 没选设备就直接判失败：sendAction 会自己拦下来发不出去，但状态已经被设成 pushing 了，
    // 那条回执永远不来 → 按钮锁死在"等待"，界面看着像卡住
    if (m_currentUid.isEmpty()) {
        setFileFail(QStringLiteral("还没选设备，文件发不出去"));
        return;
    }
    clearFilePush();

    // 界面拿到的是 file:// URL，这里统一解一次，顺便把"到底在推哪个文件"印进日志 ——
    // 出错时日志里能直接对上，不用再去问人"你选的是哪个文件"
    const QString path = QUrl::fromUserInput(urlText).toLocalFile();
    if (path.isEmpty()) {
        setFileFail(QStringLiteral("这个地址不是本机文件：%1").arg(urlText));
        return;
    }
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        setFileFail(QStringLiteral("读不到这个文件：%1").arg(path));
        return;
    }
    if (fi.size() <= 0) {
        // 空文件：被控端的 file_done 会拿 total_bytes 和实收字节比对，空文件那边还没法收口
        setFileFail(QStringLiteral("这是个空文件（0 字节），推过去对不上账"));
        return;
    }

    m_fileName = fi.fileName();
    m_fileTotal = fi.size();
    m_pushPath = path;

    // QFile 必须先 setFileName 才能 open —— 2026-10-04 踩过：漏了这行，open() 直接 false，
    // 而且 errorString() 只会给你一句无用的 "Unknown error"（没文件名时 Qt 连错误类型都设不出来）。
    // 那时候会在"开本机文件"这一步卡死，一片 file_chunk 都发不出去，日志上看不出根因。
    m_pushFile.setFileName(path);
    if (!m_pushFile.open(QIODevice::ReadOnly)) {
        // 本机都读不了就不开被控端会话：少一条无谓指令，也少一台机器挂着一个空会话。
        // 打开失败一律先把 fileName/exists/size/error 摊开 —— errorString() 在这类失败上
        // 只会给一句无用的 "Unknown error"，不摊开根本不知道是文件没了还是句柄问题
        logf("[viewer] 诊断 pushFile 打开失败：fileName=[%s] exists=%d size=%lld err=[%s]",
             m_pushFile.fileName().toUtf8().constData(),
             (int)m_pushFile.exists(),
             (long long)m_pushFile.size(),
             m_pushFile.errorString().toUtf8().constData());
        setFileFail(QStringLiteral("本机文件读不开：%1（%2）")
                        .arg(path).arg(m_pushFile.errorString()));
        return;
    }
    logf("[viewer] 本机源文件已打开（%lld 字节），开始推给教室机", (long long)m_pushFile.size());

    m_fileState = QStringLiteral("pushing");
    emit fileProgressChanged();
    logf("[viewer] 开会话 → file_push %s（%lld 字节）→ %s",
         m_fileName.toUtf8().constData(), (long long)m_fileTotal,
         m_currentUid.toUtf8().constData());

    QJsonObject p;
    p.insert(QStringLiteral("name"), m_fileName);
    p.insert(QStringLiteral("total_bytes"), (double)m_fileTotal);
    sendAction(QStringLiteral("file_push"), p);
}

void ViewerBackend::sendNextChunk()
{
    if (m_fileState != QStringLiteral("sending")) return;

    const qint64 left = m_fileTotal - m_fileBytes;
    if (left <= 0) {
        // 收口：把声明的总字节数交给被控端，它自己会比对、对不上就把半截文件删掉
        QJsonObject p;
        p.insert(QStringLiteral("total_bytes"), (double)m_fileTotal);
        sendAction(QStringLiteral("file_done"), p);
        return;
    }

    const QByteArray blob = m_pushFile.read(qMin(left, kFileChunkBytes));
    if (blob.isEmpty()) {
        // 注意：格式化参数只能给 QString::arg，不能塞进 QStringLiteral —— 那是宏，
        // 多一个整数字面量就连参数个数都算不对（C4002）
        setFileFail(QStringLiteral("文件读不出来了（本机文件被改小了？已推 %1 / 原 %2）")
                        .arg((long long)m_fileBytes).arg((long long)m_fileTotal));
        return;
    }

    QJsonObject p;
    p.insert(QStringLiteral("seq"), (double)m_pushNext);
    p.insert(QStringLiteral("data"), QString::fromLatin1(blob.toBase64()));
    m_fileBytes += blob.size();
    m_pushNext++;
    m_filePercent = (int)(m_fileBytes * 100 / m_fileTotal);
    emit fileProgressChanged();
    sendAction(QStringLiteral("file_chunk"), p);
}

void ViewerBackend::cancelPush()
{
    if (m_fileState == QStringLiteral("idle")) return;
    if (m_fileState == QStringLiteral("done") || m_fileState == QStringLiteral("failed")) {
        clearFilePush();
        return;
    }
    // 真推到一半：仍要发一次 file_done 让被控端**关掉接收会话 + 删掉半截文件**
    // （否则它那边的 g_fileRecvTarget 一直挂着，后面再推任何文件都会先落到同一个目标上，
    // 而且残缺文件留在教室机上是打不开的）。
    // total_bytes 传 0 —— 被控端认这是"取消"：走删半截那条分支、不做字节数比对。
    QJsonObject p;
    p.insert(QStringLiteral("total_bytes"), QJsonValue(0));
    sendAction(QStringLiteral("file_done"), p);
    if (m_pushFile.isOpen()) m_pushFile.close();
    m_fileState = QStringLiteral("idle");
    m_fileError = QStringLiteral("已取消");
    logf("[viewer] 文件推送已取消（被控端已关会话并删掉半截文件，收件目录不留残骸）");
    emit fileProgressChanged();
}

void ViewerBackend::sendType(const QString &text)
{
    if (m_currentUid.isEmpty()) { logf("[viewer] FAIL 没选设备不发打字"); return; }
    QJsonObject params;
    params.insert(QStringLiteral("kind"), QStringLiteral("type"));
    params.insert(QStringLiteral("text"), text);
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), QStringLiteral("input"));
    p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] 操控→ 打字 %d 字符", text.size());
}
