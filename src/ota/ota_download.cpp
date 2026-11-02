#include "ota_download.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "ota_state.h"              // 路径 / 健康上报 / 退避账
#include "ota_update.h"             // applyUpdateInfo / otaBackupExe / otaWritePending
#include "ota_channel_state.h"      // 渠道自动晋升：remember latest.channel/promotion
#include "../system/winlauncher.h"         // stelarithStartHiddenDetached（重启助手静默拉起）

// ── main.cpp 文件作用域外部链接符号（按既有 extern 模式复用）──
extern const char *kAppVersion;
extern QString g_channel;
void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                       const QString &error = QString(), const QJsonObject &data = QJsonObject());

// ══════════════════════════════════════════════════════════════════════════
// 以下整段自 main.cpp 迁出（2026-10-31 main 拆分 #16），逻辑逐字保留。
// 起因：Range 断点续传曾直接写进 main.cpp（+50 行未记台账），把已拆的成果吃回去；
//       本刀把整个下载簇（护栏 → 拉包 → 续传 → 校验 → 派安装）一并外置。
// ══════════════════════════════════════════════════════════════════════════

/* 下载簇护栏说明（续自 main.cpp 原注释块的后半段——迁出时只截到块中部，这里补回起始符）
 *   ③ 下载体积上限 kOtaMaxBytes，超限即中止并删档；④ 落盘只在 ota 目录内的 .exe。
 *
 * ⚠️ 诚实声明：下载/校验/触发安装这条链路**无法在本机端到端验证**
 *   （一执行就会把本进程换掉）。故本实现只保证"护栏与逻辑正确、编译通过"，
 *   真实升级必须在教室机灰度验证后再全网铺开。
 * 应急开关：设 STE_QT_OTA_DISABLE=1 可在某台机器上硬关掉自更新（默认开）。
 * ══════════════════════════════════════════════════════════════════════════ */

static const qint64 kOtaMaxBytes = 300LL * 1024 * 1024;   // 安装包体积上限 300MB

static QNetworkAccessManager *g_otaNam = nullptr;
static QNetworkReply *g_otaReply = nullptr;
static QFile *g_otaFile = nullptr;
static QString g_otaId, g_otaSha, g_otaVer, g_otaPath;
static qint64 g_otaGot = 0;
static qint64 g_otaExisting = 0;          // 2026-10-31 Range 断点续传：本地已有字节数（续传起点）
static bool g_otaAppendPending = false;   // 2026-10-31：是否带 Range 头续传（200 回落全量时清掉）
static bool g_otaBusy = false;
// 见前面 otaIsBusy 的说明：强制升级的触发判定在文件更前面，只能靠这个函数看到这个标志。
bool otaIsBusy() { return g_otaBusy; }

// ── 周期性 OTA 复查（2026-10-17 · 修「云端发了版，设备不升级」）────────────
// 病灶：云端把最新版本**只随 registered 回执下发一次**（cloud-ws index.js 的 register 分支），
// 而 applyUpdateInfo() 也只在那里被调用。教室机是"一年不重启"的受管终端 ⇒ 云端发了新版本，
// 它永远不知道（日志里一直是"已是最新"），除非碰巧断线重连。这就是"OTA 不生效"。
//
// 补法：自己按 HTTP 轮询公开清单 GET {http基址}/api/public/ota?product=agent
//       &channel=<渠道>&current=<本机版本>，把结果**翻译成与注册回执同形**的 payload
//       交给 applyUpdateInfo —— 版本比较、托盘/岛提示、强制升级判定全部复用同一条路径。
// ⚠️ 绝不在这里另写一套版本比较：云端 ota.js 的注释已经点明，"两端各写一遍比较"
//    正是 0.6.23 之前"正式版被判成已是最新"那个静默 bug 的来源。
// 参数：STE_QT_OTA_CHECK_SEC 调间隔（默认 1800s，钳 [60, 86400]）；
//       STE_QT_OTA_DISABLE=1 与自更新共用同一把应急刹车。
void otaSelfCheck()
{
    if (qEnvironmentVariable("STE_QT_OTA_DISABLE") == QLatin1String("1")) return;
    if (otaIsBusy()) return;                    // 正在下载/安装，别插队
    const QString wsUrl = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    if (wsUrl.isEmpty()) return;                // 没配云端地址：无从查起（注册回执那条路也走不通）

    // M3（2026-10-20）：每轮复查先做一次健康快照比对。它是"外部变更"的唯一感知途径：
    //   · 管理员放下/拿走 ota-pause（maybeAutoUpdate 不一定会被调到）；
    //   · 状态文件被手工改过（排障时常见）；
    //   · 开机以来的基线快照（本进程第一次上报，让云端知道这台机器的 OTA 子系统活着、
    //     在哪个 channel、当前有没有欠账）。
    // 全程只读本地状态文件 + 一个文件存在性判断，不发网络请求；**没变就静默**。
    otaReportHealth(QStringLiteral("periodic"));

    // ws(s)://host[:port]/ws/agent → http(s)://host[:port]（与 uploadRecordingAsync 同款推导）
    QString base = wsUrl;
    base.replace(QLatin1String("wss://"), QLatin1String("https://"))
        .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = base.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) base.truncate(wsIdx);

    QUrl u(base + QStringLiteral("/api/public/ota"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("product"), QStringLiteral("agent"));
    q.addQueryItem(QStringLiteral("channel"), g_channel);
    q.addQueryItem(QStringLiteral("current"), QString::fromLatin1(kAppVersion));
    u.setQuery(q);

    auto *nam = new QNetworkAccessManager(qApp);
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("StelarithAgentQt/%1").arg(kAppVersion));

    QNetworkReply *r = nam->get(req);
    QObject::connect(r, &QNetworkReply::finished, r, [r, nam, base] {
        const QByteArray body = r->readAll();
        const QNetworkReply::NetworkError err = r->error();
        const QString errStr = r->errorString();
        r->deleteLater();
        nam->deleteLater();
        if (err != QNetworkReply::NoError) {
            // 复查失败**不打扰师生**（网络抖动很常见），但要留痕 —— 否则"静默失败"没法诊断
            qInfo("[agent-qt] OTA 复查失败（%s）：%s", qPrintable(base), qPrintable(errStr));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QJsonObject latest = o.value(QStringLiteral("latest")).toObject();
        if (latest.isEmpty()) {
            qInfo("[agent-qt] OTA 复查：云端未发布该渠道版本（channel=%s）", qPrintable(g_channel));
            return;
        }
        // 键名逐字对齐 applyUpdateInfo 的读法（latestVersion/updateUrl/updateSha256/…）
        QJsonObject pay;
        pay.insert(QStringLiteral("latestVersion"), latest.value(QStringLiteral("version")));
        pay.insert(QStringLiteral("updateUrl"), latest.value(QStringLiteral("url")));
        pay.insert(QStringLiteral("updateSha256"), latest.value(QStringLiteral("sha256")));
        pay.insert(QStringLiteral("updateNotes"), latest.value(QStringLiteral("notes")));
        pay.insert(QStringLiteral("updateMandatory"), latest.value(QStringLiteral("mandatory")));
        // 2026-11-02 渠道自动晋升：把 latest.channel / publishedAt / promotion 交给展示模块。
        // 放在这里（而不是 applyUpdateInfo 里）的理由：这个函数是**唯一**解析公开清单的
        // 网络回包处；注册回执那条路（云端 register 分支）没有 promotion 字段，不该清空它。
        otaChannelRemember(latest);
        applyUpdateInfo(pay);
    });
}

// 起定时器：默认 30 分钟一次，首次 3 分钟后（注册回执里那次已算一次检查，不必立刻重来）。
// STE_QT_OTA_CHECK_FIRST_SEC 可把首次提前（验证用；生产默认 180s，别在启动高峰抢带宽）。
void otaStartPeriodicCheck()
{
    int sec = qEnvironmentVariableIntValue("STE_QT_OTA_CHECK_SEC");
    if (sec <= 0) sec = 1800;
    sec = qBound(60, sec, 24 * 3600);
    int firstSec = qEnvironmentVariableIntValue("STE_QT_OTA_CHECK_FIRST_SEC");
    if (firstSec <= 0) firstSec = 180;
    firstSec = qBound(5, firstSec, 24 * 3600);
    auto *t = new QTimer(qApp);
    t->setInterval(sec * 1000);
    QObject::connect(t, &QTimer::timeout, qApp, [] { otaSelfCheck(); });
    t->start();
    QTimer::singleShot(firstSec * 1000, qApp, [] { otaSelfCheck(); });
    qInfo("[agent-qt] OTA 周期复查已启动：每 %d 秒一次、首次 %d 秒后（channel=%s；"
          "STE_QT_OTA_CHECK_SEC / STE_QT_OTA_CHECK_FIRST_SEC 可调）",
          sec, firstSec, qPrintable(g_channel));
}


// sendActionReceipt 已迁至本文件 namespace 下方（文件作用域），供 notify_entry 模块 extern 复用。

// A6（2026-10-11）：**周期遥测**上报（噪音采样这类"没有指令 id、要持续吐数"的场景）。
// ⚠️ 为什么不走 receipt：云端的 recordResult 是**按指令 id 落表**的（registry.js:636），
//    遥测没有 id，硬塞 receipt 会在云端 results 表里塞一堆 id=undefined 的脏记录。
//    云端对 type:"event" 已有处理（index.js:1284 → pushEvent 记进事件流水），
//    所以遥测走 event，语义干净、也不污染指令结果表。
// sendEvent 已迁至本文件 namespace 下方（文件作用域），供 study_mode 模块 extern 复用。

// otaReportHealth 已迁至 src/ota/ota_state.cpp（2026-10-30 main 拆分 #4a）。

static void otaOnFinished();   // 前向声明（下载结束回调）

// 启动一次自更新。**异步**：立即回执 started，下载完成后再回 installing/failed。
void startSelfUpdate(const QString &id, const QJsonObject &params)
{
    const QString action = QStringLiteral("self_update");

    if (qEnvironmentVariableIsSet("STE_QT_OTA_DISABLE")) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("本机已设 STE_QT_OTA_DISABLE，拒绝自更新"));
        return;
    }
    if (g_otaBusy) {
        sendActionReceipt(id, action, QStringLiteral("failed"), QStringLiteral("已有一个更新任务在进行中"));
        return;
    }

    const QString url = params.value(QStringLiteral("url")).toString().trimmed();
    const QString sha = params.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    const QString ver = params.value(QStringLiteral("version")).toString().trimmed();

    // 护栏①：只认 http(s)（挡掉 file:// / 本地路径注入）
    if (!url.startsWith(QStringLiteral("http://")) && !url.startsWith(QStringLiteral("https://"))) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("url 必须是 http(s) 地址：%1").arg(url));
        otaNoteFailure(QStringLiteral("url 不合法"));
        return;
    }
    // 护栏②：必须有合法 sha256 —— 没有校验的"更新"等于任意代码执行，一律拒绝
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex64.match(sha).hasMatch()) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("sha256 必须是 64 位小写十六进制（拒绝无校验更新）"));
        otaNoteFailure(QStringLiteral("sha256 不合法"));
        return;
    }

    // 落地目录：%LOCALAPPDATA% 下的 xingjikong/ota（注意：注释行末尾不能是反斜杠，
    // 否则 C/C++ 会当成续行符把下一行吞掉 —— 这里踩过一次，记下）
    const QString dir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/ota");
    QDir().mkpath(dir);
    QString base = QUrl(url).fileName();
    // 护栏④：只当安装包用，且只落在 ota 目录里的 .exe
    if (base.isEmpty() || !base.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        base = QStringLiteral("stelarith-agent-update.exe");
    // 护栏④b（2026-10-13 登记）：文件名白名单。QUrl::fileName() 挡得住 '/'，但挡不住 '\'
    //   —— Windows 上 QFile 把 '\' 也当路径分隔符，`a\..\..\evil.exe` 就能逃出 ota 目录
    //   （url 里塞反斜杠即可任意写盘 = 云端被攻破后升级通道变任意文件写入）。
    //   ':' 会命中 NTFS ADS（x.exe:stream 同样能写到别处），`.`/`..` 是目录自身/父目录。
    //   一律收紧成 [A-Za-z0-9._-]{1,80}，不合白名单就换默认名 —— 下载与 sha256 都不受影响。
    static const QRegularExpression otaSafeName(QStringLiteral("^[A-Za-z0-9._-]{1,80}$"));
    if (base == QStringLiteral(".") || base == QStringLiteral("..")
        || !otaSafeName.match(base).hasMatch()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] OTA url 文件名不进白名单，改用默认名：%1").arg(base);
        base = QStringLiteral("stelarith-agent-update.exe");
    }
    g_otaPath = dir + QLatin1Char('/') + base;

    // 2026-10-31：Range 断点续传（弱网教室机 38MB 包中断不再从头拉）。
    // 设计：先在**响应头到达后**再按状态决定写模式 ——
    //   · 服务器回 206（接受 Range）→ 续写在已有字节之后（Append + g_otaGot=已有大小）；
    //   · 服务器回 200（忽略 Range）→ 从头全量（Truncate + g_otaGot=0），GitHub/CF 代理均支持 206。
    // 哈希改在下载完成后对**落盘成品**流式重算（见 otaOnFinished），续传与全量两种路径天然正确。
    // 安全自愈：残留文件过不了 sha256 校验 → 删除 → 下次全量重试，绝不执行不可信内容。
    g_otaExisting = QFileInfo(g_otaPath).size();
    g_otaAppendPending = (g_otaExisting > 0);
    g_otaFile = nullptr;                       // 等 metaDataChanged 再开（200/206 决定 Truncate/Append）

    g_otaId = id; g_otaSha = sha; g_otaVer = ver; g_otaGot = (g_otaAppendPending ? g_otaExisting : 0);
    g_otaBusy = true;
    if (!g_otaNam) g_otaNam = new QNetworkAccessManager();

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StelarithAgentQt/%1").arg(kAppVersion));
    if (g_otaAppendPending) {
        // Range: bytes=<已有字节>- （无结束位 = 拉剩余到文件尾）。
        req.setRawHeader("Range", "bytes=" + QByteArray::number(g_otaExisting) + "-");
        qInfo("[agent-qt] ⬇ OTA 断点续传：已有 %lld/期望字节，发 Range 从 %lld 继续",
              (long long)g_otaExisting, (long long)g_otaExisting);
    }

    // 先回执"已受理"：管理端据此确认指令被接下（长下载不至于看起来"没反应"）
    sendActionReceipt(id, action, QStringLiteral("started"), QString(),
                      QJsonObject{{QStringLiteral("stage"), QStringLiteral("downloading")},
                                  {QStringLiteral("version"), ver}, {QStringLiteral("url"), url}});
    qInfo("[agent-qt] ⬇ OTA 开始下载 %s → %s（期望 sha256=%s%s）",
          qPrintable(url), qPrintable(g_otaPath), qPrintable(sha),
          g_otaAppendPending ? "，续传模式" : "");

    g_otaReply = g_otaNam->get(req);

    // 响应头先到（Qt 保证 metaDataChanged 先于 readyRead）—— 在这里按 206/200 开文件。
    QObject::connect(g_otaReply, &QNetworkReply::metaDataChanged, [] {
        if (!g_otaReply) return;
        const int status = g_otaReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool partial = (status == 206);
        const qint64 startAt = (partial && g_otaAppendPending) ? g_otaExisting : 0;
        if (status == 200 && g_otaAppendPending) {
            qInfo("[agent-qt] ⬇ OTA 服务器忽略 Range（HTTP 200 全量），改为从头下载");
            g_otaAppendPending = false;
            g_otaGot = 0;
        }
        if (!g_otaFile) {
            g_otaFile = new QFile(g_otaPath);
            const QIODevice::OpenMode mode = (startAt > 0)
                ? (QIODevice::WriteOnly | QIODevice::Append)
                : (QIODevice::WriteOnly | QIODevice::Truncate);
            if (!g_otaFile->open(mode)) {
                qWarning("[agent-qt] ⬇ OTA 写入文件失败（%s）—— 中止下载", qPrintable(g_otaPath));
                if (g_otaReply) g_otaReply->abort();
                return;
            }
            if (startAt > 0) {
                g_otaFile->seek(g_otaExisting);    // Append 模式下 seek 到既有大小（保险）
                qInfo("[agent-qt] ⬇ OTA 已从 %lld 字节处续写（bytes=%lld-）",
                      (long long)g_otaExisting, (long long)g_otaExisting);
            }
        }
    });

    // 注意：这些 lambda **无捕获**，只读命名空间级全局量，避免函数返回后引用悬空。
    QObject::connect(g_otaReply, &QNetworkReply::readyRead, [] {
        if (!g_otaReply || !g_otaFile) return;
        const QByteArray chunk = g_otaReply->readAll();
        g_otaGot += chunk.size();
        if (g_otaGot > kOtaMaxBytes) { g_otaReply->abort(); return; }  // 护栏③
        g_otaFile->write(chunk);
        // 2026-10-31：不再增量哈希 —— 断点续传下哈希须覆盖整包，
        // 且在响应头到达前不知道是续写还是全量，增量无法自洽。改为 otaOnFinished
        // 对落盘成品流式重算（38MB 读一遍毫秒级，安全性与原来等价：安装前必验 sha）。
    });
    QObject::connect(g_otaReply, &QNetworkReply::finished, [] { otaOnFinished(); });
}

// 下载结束：关文件、验 sha256、通过则派助手 + 执行静默安装；否则如实回 failed。
static void otaOnFinished()
{
    const QString action = QStringLiteral("self_update");
    QNetworkReply *reply = g_otaReply;
    if (!reply) return;
    const QNetworkReply::NetworkError nerr = reply->error();
    const QString nerrStr = reply->errorString();
    reply->deleteLater();
    g_otaReply = nullptr;
    if (g_otaFile) { g_otaFile->close(); g_otaFile->deleteLater(); g_otaFile = nullptr; }
    g_otaBusy = false;

    const QString id = g_otaId, ver = g_otaVer, sha = g_otaSha, path = g_otaPath;
    const qint64 got = g_otaGot;
    // 2026-10-31：哈希改为对落盘成品流式重算（断点续传下增量不可自洽）。
    // 任何半成品（网络中断尾部、误拼接）都算不出正确 sha → 走"校验不符"删除 → 下次重试。
    QString gotSha;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash h(QCryptographicHash::Sha256);
            QByteArray buf;
            while ((buf = f.read(1024 * 1024)).size() > 0) h.addData(buf);
            gotSha = QString::fromLatin1(h.result().toHex());
        }
    }

    // 三种失败都要进"退避账"：强制升级失败的机器不能立刻又装一遍。
    if (got > kOtaMaxBytes) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("安装包超过体积上限（%1 字节）").arg(kOtaMaxBytes));
        otaNoteFailure(QStringLiteral("安装包超过体积上限"));
        return;
    }
    if (nerr != QNetworkReply::NoError) {
        // 2026-10-31：网络错误**不删已下载文件** —— Range 断点续传靠它接着拉。
        // 内容损坏由 sha256 校验兜底（校验不符才删）；只删"完整但不对"的半成品。
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("下载中断：%1（已保留部分文件，下次自动续传）").arg(nerrStr));
        otaNoteFailure(QStringLiteral("下载中断：").append(nerrStr).left(160));
        return;
    }
    if (gotSha != sha) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("sha256 校验不符（期望 %1 实得 %2），已删除下载文件").arg(sha, gotSha));
        otaNoteFailure(QStringLiteral("sha256 校验不符"));
        return;
    }
    qInfo("[agent-qt] ✅ OTA 校验通过（%lld 字节，sha256=%s），准备安装", (long long)got, qPrintable(gotSha));

    // ①-0 动安装器之前：先做"回滚三件套"。顺序不能反（装完就没人有机会备份了）。
    //   · 删掉 ota-last-ok.txt，否则看门狗会拿**上一版的**成功标记冒充新版本起来了。
    //   · 备份当前 exe → 写 pending（看门狗靠它知道"这次升级该验谁的账"）
    //   · 备份失败就直接不装：宁可升不上，也不能留一台没有退路的机器。
    QFile::remove(otaOkPath());
    const QString prev = otaBackupExe();
    if (prev.isEmpty()) {
        QFile::remove(path);
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("备份当前 exe 失败，拒绝无回滚地覆盖自己"));
        otaNoteFailure(QStringLiteral("备份当前 exe 失败（拒绝无回滚升级）"));
        return;
    }
    otaWritePending(prev, ver);

    // ① 先派重启助手：等安装器把文件换完，再把新版拉起来（本进程随后会被安装器杀掉）
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString helper = exeDir + QStringLiteral("/ota-relaunch.bat");
    {
        QFile h(helper);
        if (h.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // 纯 ASCII + CRLF：cmd.exe 按 GBK 解析 .bat，中文注释会吞行（同 install-autostart 的教训）
            //
            // ⚠️ 这份脚本是**升级前**由旧版本写下的，之后不会被新版本改写 ——
            //    所以它体内是**旧代码的字节**：新版本一启动就崩也不影响它把机器救回来。
            //    只看 agent 自己是不够的（新 exe 崩了它自己也已经没了），这道狗必须在进程外。
            const QString content =
                QStringLiteral("@echo off\r\n"
                               "rem Stelarith OTA guard (auto-generated by the PRE-upgrade build).\r\n"
                               "rem Pure ASCII on purpose: cmd.exe parses .bat as GBK.\r\n"
                               "rem 1) give the installer time to replace files\r\n"
                               "ping -n 31 127.0.0.1 >nul\r\n"
                               "rem 2) start the new agent, then see whether it ever reached the cloud\r\n"
                               "start \"\" \"%3\"\r\n"
                               "ping -n 91 127.0.0.1 >nul\r\n"
                               "if exist \"%1\" goto done\r\n"
                               "rem 3) it never registered -> restore the pre-upgrade exe and relaunch it\r\n"
                               "del /f /q \"%1\" >nul 2>&1\r\n"
                               "copy /Y \"%2\" \"%3\" >nul\r\n"
                               "start \"\" \"%3\"\r\n"
                               ":done\r\n"
                               "del /f /q \"%~f0\"\r\n")
                    .arg(otaOkPath(), otaPrevPath(), exeDir + QStringLiteral("/stelarith-agent-qt.exe"));
            h.write(content.toUtf8());
            h.flush();
            h.close();
        } else {
            qInfo("[agent-qt] ⚠ OTA 重启助手写不出（%s）—— 升级后需靠下次登录拉起", qPrintable(helper));
        }
    }
    if (QFile::exists(helper))
        // #368 需求①：静默 detached，杜绝重启助手弹出 cmd 黑窗
        stelarithStartHiddenDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), helper});

    // ② 启动安装包。它先 Stop-Process 掉本进程，再覆盖文件。
    //   静默参数同时给 NSIS(/S) 与 Inno(/VERYSILENT)：旧版 NSIS 包认 /S，
    //   新版 Inno 包认 /VERYSILENT；两者都静默，过渡期不弹窗。
    const bool launched = QProcess::startDetached(
        path, QStringList{QStringLiteral("/S"), QStringLiteral("/VERYSILENT"),
                          QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART")});
    if (!launched) {
        sendActionReceipt(id, action, QStringLiteral("failed"),
                          QStringLiteral("安装包启动失败：%1").arg(path));
        return;
    }
    // 这是本进程能发出的**最后一条回执**（随后被安装器替换）。
    // 最终是否成功，以"设备重连后 register 上报的新 version"为准。
    sendActionReceipt(id, action, QStringLiteral("installing"), QString(),
                      QJsonObject{{QStringLiteral("stage"), QStringLiteral("installing")},
                                  {QStringLiteral("version"), ver},
                                  {QStringLiteral("path"), path}});
    qInfo("[agent-qt] 🚀 OTA 已启动安装包（/S + /VERYSILENT），重启助手已派发；本进程即将被替换");
}
