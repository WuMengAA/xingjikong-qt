#include "updater.h"
#include "viewerbackend.h"   // 只为 logf（全局日志出口，见 viewerbackend.h:48）

#include <QCoreApplication>
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
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

// 与被控端 self_update 同一口径的体积上限（那里的 kOtaMaxBytes 也是 300MB）。
// 目的不是"省流量"，是挡住"云端清单被写坏 / 指向一个大文件"这类事故把磁盘写满。
static const qint64 kMaxOtaBytes = 300LL * 1024 * 1024;
// 检查更新超时（15s）与下载"卡住"超时（60s 无新字节）。两者都是**不活动**超时。
static const int kCheckTimeoutMs = 15000;
static const int kDownloadStallMs = 60000;

Updater::Updater(const QString &wsUrl, QObject *parent)
    : QObject(parent), m_wsUrl(wsUrl)
{
    m_nam = new QNetworkAccessManager(this);
    // 启动时清一次上次更新留下的残骸（zip / staging / 替换脚本）。
    // ⚠️ 放在这里而不是"更新完立刻删"：替换脚本正跑着的时候删它会被 Windows 拒（文件被占），
    //    最后总会剩一个几十 KB 的 .cmd —— 由下一次启动的这个调用收掉，永远不积。
    cleanupOldDirs();
}

Updater::~Updater()
{
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
    if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
    delete m_hash;
    m_hash = nullptr;
}

QString Updater::currentVersion() const
{
    // 编译期注入（CMakeLists 的 add_compile_definitions），与 backend.version / 关于页同一个真源。
    return QStringLiteral(STELARITH_VIEWER_VERSION_STRING);
}

bool Updater::busy() const
{
    return m_state == QLatin1String("checking")
        || m_state == QLatin1String("downloading")
        || m_state == QLatin1String("verifying")
        || m_state == QLatin1String("extracting")
        || m_state == QLatin1String("launching");
}

void Updater::setState(const QString &s, const QString &msg)
{
    m_state = s;
    m_message = msg;
    emit changed();
}

void Updater::fail(const QString &why)
{
    m_state = QStringLiteral("failed");
    m_message = why;
    logf("[update] FAIL %s", why.toUtf8().constData());
    emit changed();
}

void Updater::reset()
{
    if (busy()) return;
    m_message.clear();
    m_latestVersion.clear();
    m_notes.clear();
    m_url.clear();
    m_sha.clear();
    m_size = 0;
    m_got = 0;
    m_percent = 0;
    m_mandatory = false;
    m_state = QStringLiteral("idle");
    emit changed();
}

QString Updater::httpBase() const
{
    // 与 viewerbackend.cpp 的 fetchRecordings（拉 /api/recordings）同一套换算：
    // 云端对外只有一个地址（wss://host/ws/viewer），http 面从它派生。
    QString s = m_wsUrl;
    s.replace(QLatin1String("wss://"), QLatin1String("https://"))
     .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int i = s.indexOf(QLatin1String("/ws/"));
    if (i > 0) s.truncate(i);
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}

void Updater::cleanupOldDirs()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                         + QStringLiteral("/xingjikong-update");
    if (QFileInfo::exists(base)) {
        if (QDir(base).removeRecursively())
            logf("[update] 已清理上次更新的残骸：%s", base.toUtf8().constData());
        // 删不干净（比如替换脚本还在跑）不报错：下次启动还会来收。
    }
}

// ──────────────────────────────────────────────────────────────────────
// 检查更新
// ──────────────────────────────────────────────────────────────────────
void Updater::checkForUpdate()
{
    if (busy()) return;
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    if (m_state == QLatin1String("available")) reset();

    setState(QStringLiteral("checking"), QStringLiteral("正在检查更新…"));
    const QString cur = QString::fromLatin1(QUrl::toPercentEncoding(currentVersion()));
    const QUrl u(httpBase() + QStringLiteral("/api/public/ota?product=viewer&current=") + cur);
    logf("[update] 检查更新 → %s（本机 %s）", u.toString().toUtf8().constData(),
         currentVersion().toUtf8().constData());

    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kCheckTimeoutMs);
    m_reply = m_nam->get(req);
    QObject::connect(m_reply, &QNetworkReply::finished, this, &Updater::onCheckFinished);
}

void Updater::onCheckFinished()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (!r) return;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = r->readAll();
    const QString netErr = (r->error() == QNetworkReply::NoError) ? QString() : r->errorString();
    r->deleteLater();

    if (status != 200) {
        fail(netErr.isEmpty()
             ? QStringLiteral("检查更新失败：云端返回 HTTP %1（%2/api/public/ota）")
                   .arg(status).arg(httpBase())
             : QStringLiteral("检查更新失败：%1").arg(netErr));
        return;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(QStringLiteral("检查更新失败：云端返回的不是合法 JSON（%1）").arg(pe.errorString()));
        return;
    }
    const QJsonObject o = doc.object();
    if (!o.value(QStringLiteral("published")).toBool(false)) {
        // 云端没发布过版本 —— 如实说，不假装"已是最新"（和 /api/public/ota 的口径一致）。
        setState(QStringLiteral("uptodate"), QStringLiteral("云端还没发布过版本信息，无需更新"));
        return;
    }
    const QJsonObject latest = o.value(QStringLiteral("latest")).toObject();
    const QString ver = latest.value(QStringLiteral("version")).toString();
    m_latestVersion = ver;
    m_notes = latest.value(QStringLiteral("notes")).toString();
    m_url = latest.value(QStringLiteral("url")).toString();
    m_sha = latest.value(QStringLiteral("sha256")).toString();
    m_size = (qint64)latest.value(QStringLiteral("size")).toDouble();
    m_mandatory = latest.value(QStringLiteral("mandatory")).toBool(false);

    // 比较在云端做（见 updater.h 第 1 条）：这里只读结论。
    const bool has = o.value(QStringLiteral("hasUpdate")).toBool(false);
    if (!has) {
        setState(QStringLiteral("uptodate"),
                 QStringLiteral("已是最新版本（本机 %1，云端 %2）").arg(currentVersion(), ver));
        logf("[update] 已是最新（本机 %s / 云端 %s）",
             currentVersion().toUtf8().constData(), ver.toUtf8().constData());
        return;
    }
    setState(QStringLiteral("available"),
             QStringLiteral("发现新版本 %1（当前 %2）").arg(ver, currentVersion()));
    logf("[update] 发现新版本 %s（本机 %s），包 %lld 字节",
         ver.toUtf8().constData(), currentVersion().toUtf8().constData(), (long long)m_size);
}

// ──────────────────────────────────────────────────────────────────────
// 下载 → 校验 → 解压 → 写替换脚本 → 退出
// ──────────────────────────────────────────────────────────────────────
bool Updater::installDirWritable(QString *why) const
{
    const QString dir = QCoreApplication::applicationDirPath();
    QFile t(dir + QStringLiteral("/.ste-update-write-test"));
    if (!t.open(QIODevice::WriteOnly)) {
        *why = QStringLiteral("程序目录不可写（%1）：%2。"
                              "把管理端放到你有写权限的目录，或以管理员身份运行后再更新。")
                   .arg(QDir::toNativeSeparators(dir), t.errorString());
        return false;
    }
    t.close();
    t.remove();
    return true;
}

void Updater::installUpdate()
{
    if (busy()) return;
    // 护栏①：URL 只认 http(s)
    if (!m_url.startsWith(QLatin1String("http://")) && !m_url.startsWith(QLatin1String("https://"))) {
        fail(QStringLiteral("更新地址不合法（只接受 http/https）：%1").arg(m_url));
        return;
    }
    // 护栏②：sha256 必须是 64 位小写十六进制 —— 没有校验的"更新"等于任意代码执行
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex64.match(m_sha).hasMatch()) {
        fail(QStringLiteral("云端给的 sha256 不合法（拒绝无校验更新）：%1").arg(m_sha));
        return;
    }
    // 护栏③：体积上限
    if (m_size > kMaxOtaBytes) {
        fail(QStringLiteral("更新包声明体积 %1 超过上限 %2，已中止")
                 .arg(m_size).arg(kMaxOtaBytes));
        return;
    }
    QString why;
    if (!installDirWritable(&why)) { fail(why); return; }

    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/xingjikong-update");
    if (!QDir().mkpath(m_dir)) {
        fail(QStringLiteral("建不了更新工作目录：%1").arg(QDir::toNativeSeparators(m_dir)));
        return;
    }
    QString base = QFileInfo(QUrl(m_url).path()).fileName();
    if (base.isEmpty() || !base.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        base = QStringLiteral("stelarith-viewer-update.zip");
    m_zipPath = m_dir + QLatin1Char('/') + base;
    QFile::remove(m_zipPath);

    m_file = new QFile(m_zipPath);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString es = m_file->errorString();
        delete m_file; m_file = nullptr;
        fail(QStringLiteral("建不了更新文件（%1）：%2").arg(QDir::toNativeSeparators(m_zipPath), es));
        return;
    }
    delete m_hash;
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_got = 0;
    m_percent = 0;

    setState(QStringLiteral("downloading"),
             QStringLiteral("正在下载新版本 %1…").arg(m_latestVersion));
    logf("[update] 开始下载 → %s", m_url.toUtf8().constData());

    // ⚠️ 必须用花括号初始化：写 `QNetworkRequest req(QUrl(m_url));` 是经典的
    //    most vexing parse —— 编译器把这行当成**函数声明**，后续 req.setAttribute 直接 C2228。
    QNetworkRequest req{ QUrl(m_url) };
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kDownloadStallMs);
    m_reply = m_nam->get(req);
    QObject::connect(m_reply, &QNetworkReply::readyRead, this, &Updater::onDownloadReadyRead);
    QObject::connect(m_reply, &QNetworkReply::finished, this, &Updater::onDownloadFinished);
}

void Updater::onDownloadReadyRead()
{
    if (!m_reply) return;
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;
    m_got += chunk.size();
    if (m_got > kMaxOtaBytes) {
        m_reply->abort();   // 触发 finished(error)，由那边统一收口
        return;
    }
    if (m_file && m_file->write(chunk) != chunk.size()) {
        const QString es = m_file->errorString();
        m_reply->abort();
        fail(QStringLiteral("写更新文件失败：%1").arg(es));
        return;
    }
    if (m_hash) m_hash->addData(chunk);
    const int p = (m_size > 0) ? (int)(m_got * 100 / m_size) : 0;
    if (p != m_percent) { m_percent = p; emit changed(); }
}

void Updater::onDownloadFinished()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (m_file) { m_file->flush(); m_file->close(); delete m_file; m_file = nullptr; }
    if (!r) return;
    const QNetworkReply::NetworkError err = r->error();
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString es = r->errorString();
    r->deleteLater();

    if (err != QNetworkReply::NoError || (status >= 400 && status != 0)) {
        fail(QStringLiteral("下载失败：%1（HTTP %2）").arg(es).arg(status));
        QFile::remove(m_zipPath);
        return;
    }
    if (m_got <= 0) {
        fail(QStringLiteral("下载到的更新包是空的（0 字节）"));
        QFile::remove(m_zipPath);
        return;
    }
    if (m_size > 0 && m_got != m_size) {
        fail(QStringLiteral("下载不完整：收到 %1 字节，云端声明 %2 字节").arg(m_got).arg(m_size));
        QFile::remove(m_zipPath);
        return;
    }

    setState(QStringLiteral("verifying"), QStringLiteral("正在校验文件完整性…"));
    const QByteArray gotHex = m_hash ? m_hash->result().toHex() : QByteArray();
    delete m_hash;
    m_hash = nullptr;
    if (QString::fromLatin1(gotHex).compare(m_sha, Qt::CaseInsensitive) != 0) {
        fail(QStringLiteral("校验失败：下载到的文件与云端公布的 sha256 不一致"
                            "（收到 %1，应为 %2），已丢弃该文件。可能是下载被中间设备改写。")
                 .arg(QString::fromLatin1(gotHex), m_sha));
        QFile::remove(m_zipPath);
        return;
    }
    logf("[update] sha256 校验通过（%s），开始解压", m_sha.toUtf8().constData());

    setState(QStringLiteral("extracting"), QStringLiteral("正在解压新版本…"));
    const QString staging = m_dir + QStringLiteral("/staging");
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        fail(QStringLiteral("建不了解压目录：%1").arg(QDir::toNativeSeparators(staging)));
        return;
    }
    QString why;
    if (!extractZip(m_zipPath, staging, &why)) {
        fail(QStringLiteral("解压失败：%1").arg(why));
        return;
    }
    QFile::remove(m_zipPath);   // 130MB 的包留着没用，清掉

    setState(QStringLiteral("launching"), QStringLiteral("正在重启以完成更新…"));
    if (!launchReplacer(staging, &why)) {
        fail(QStringLiteral("写替换脚本失败：%1").arg(why));
        return;
    }
    logf("[update] 替换脚本已启动，本进程即将退出");
    // 留一点时间让 logf 落盘、让脚本把句柄拿稳
    QTimer::singleShot(500, qApp, [] { QCoreApplication::quit(); });
}

bool Updater::extractZip(const QString &zip, const QString &dest, QString *why)
{
    // 包名来自云端清单，理论上任意；这里再确认一次它真的是个文件。
    if (!QFileInfo::exists(zip)) {
        *why = QStringLiteral("找不到已下载的包：%1").arg(QDir::toNativeSeparators(zip));
        return false;
    }

    const QString tar = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))
                        + QStringLiteral("/System32/tar.exe");
    bool ok = false;
    QString detail;
    if (QFileInfo::exists(tar)) {
        // Win10 1803+ 自带 bsdtar，直接吃 zip。比 PowerShell Expand-Archive 快得多。
        QProcess p;
        p.setProgram(tar);
        p.setArguments({ QStringLiteral("-xf"), QDir::toNativeSeparators(zip),
                         QStringLiteral("-C"), QDir::toNativeSeparators(dest) });
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        if (!p.waitForStarted(5000)) {
            detail = QStringLiteral("tar 起不来：%1").arg(p.errorString());
        } else if (!p.waitForFinished(20 * 60 * 1000)) {
            p.kill();
            detail = QStringLiteral("tar 解压超时（20 分钟）");
        } else if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            detail = QStringLiteral("tar 退出码 %1：%2")
                         .arg(p.exitCode())
                         .arg(QString::fromLocal8Bit(p.readAll()).trimmed().left(300));
        } else {
            ok = true;
        }
    } else {
        detail = QStringLiteral("系统里没有 tar.exe，改用 PowerShell 解压…");
    }

    if (!ok) {
        logf("[update] tar 路径不可用（%s），回落 PowerShell Expand-Archive", detail.toUtf8().constData());
        // 回落：不依赖 tar.exe 的机器（很老的 Win10）。
        // 路径用双引号包、并把内部的 " 转义 —— Expand-Archive 需要真实存在的路径。
        const QString ps = QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "Expand-Archive -LiteralPath \"%1\" -DestinationPath \"%2\" -Force")
            .arg(QString(zip).replace(QLatin1Char('"'), QStringLiteral("`\"")))
            .arg(QString(dest).replace(QLatin1Char('"'), QStringLiteral("`\"")));
        QProcess p2;
        p2.setProgram(QStringLiteral("powershell.exe"));
        p2.setArguments({ QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                          QStringLiteral("-Command"), ps });
        p2.setProcessChannelMode(QProcess::MergedChannels);
        p2.start();
        if (!p2.waitForStarted(8000)) {
            *why = QStringLiteral("PowerShell 起不来：%1").arg(p2.errorString());
            return false;
        }
        if (!p2.waitForFinished(20 * 60 * 1000)) {
            p2.kill();
            *why = QStringLiteral("PowerShell 解压超时（20 分钟）");
            return false;
        }
        if (p2.exitStatus() != QProcess::NormalExit || p2.exitCode() != 0) {
            *why = QStringLiteral("PowerShell 解压失败（退出码 %1）：%2")
                       .arg(p2.exitCode())
                       .arg(QString::fromLocal8Bit(p2.readAll()).trimmed().left(300));
            return false;
        }
    }

    // ⚠️ 关键自检：解压出来的东西必须**真的像个管理端包**。
    //    少了这一步，一个内容不相干的 zip 会被原样覆盖到程序目录上 —— 那比不更新糟得多。
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (!QFileInfo::exists(dest + QLatin1Char('/') + exeName)) {
        *why = QStringLiteral("解压后没找到 %1，这个包与本程序不匹配（拒绝覆盖）").arg(exeName);
        return false;
    }
    return true;
}

bool Updater::launchReplacer(const QString &staging, QString *why)
{
    const QString appDir  = QCoreApplication::applicationDirPath();
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QString cmdPath = m_dir + QStringLiteral("/apply-update.cmd");

    // ⚠️ 批处理按系统 ANSI 读（中文机 = GBK）。用 toLocal8Bit() 落盘，
    //    与全项目日志出口同一口径；写 UTF-8 会让含中文的安装路径在 cmd 里变乱码。
    QString s;
    s += QStringLiteral("@echo off\r\n");
    s += QStringLiteral("rem 星集控管理端自更新替换脚本（自动生成；本目录由下次启动时清理）\r\n");
    s += QStringLiteral("setlocal enabledelayedexpansion\r\n");
    s += QStringLiteral("set \"SRC=%1\"\r\n").arg(QDir::toNativeSeparators(staging));
    s += QStringLiteral("set \"DST=%1\"\r\n").arg(QDir::toNativeSeparators(appDir));
    s += QStringLiteral("rem 1) 等旧进程退出（exe 与同目录 Qt DLL 在运行时是被锁的）\r\n");
    s += QStringLiteral("set /a N=0\r\n");
    s += QStringLiteral(":wait\r\n");
    s += QStringLiteral("tasklist /FI \"IMAGENAME eq %1\" 2>nul | find /I \"%1\" >nul\r\n").arg(exeName);
    s += QStringLiteral("if not errorlevel 1 (\r\n");
    s += QStringLiteral("  set /a N+=1\r\n");
    s += QStringLiteral("  if !N! geq 90 goto giveup\r\n");
    s += QStringLiteral("  ping -n 2 127.0.0.1 >nul\r\n");
    s += QStringLiteral("  goto wait\r\n");
    s += QStringLiteral(")\r\n");
    s += QStringLiteral("rem 2) 覆盖。⚠️ /XF viewer.env：包里的那份是**默认值**，"
                        "整目录覆盖会把老师配好的站点/令牌冲回默认。\r\n");
    s += QStringLiteral("robocopy \"%SRC%\" \"%DST%\" /E /IS /IT /XF viewer.env /R:2 /W:1 "
                        "/NFL /NDL /NJH /NJS >nul\r\n");
    s += QStringLiteral("rem 3) 拉起新版本\r\n");
    s += QStringLiteral("start \"\" \"%DST%\\%1\"\r\n").arg(exeName);
    s += QStringLiteral("goto done\r\n");
    s += QStringLiteral(":giveup\r\n");
    s += QStringLiteral("rem 旧进程 90 秒没退：**不动文件**（避免半覆盖把安装目录弄坏），如实留个脚印\r\n");
    s += QStringLiteral("echo [%DATE% %TIME%] 等待旧进程退出超时，本次更新未执行 > \"%DST%\\update-failed.txt\"\r\n");
    s += QStringLiteral(":done\r\n");
    s += QStringLiteral("endlocal\r\n");

    QFile f(cmdPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *why = QStringLiteral("建不了 %1：%2").arg(QDir::toNativeSeparators(cmdPath), f.errorString());
        return false;
    }
    const QByteArray bytes = s.toLocal8Bit();
    const qint64 wrote = f.write(bytes);
    f.close();
    if (wrote != bytes.size()) {
        *why = QStringLiteral("替换脚本没写全（%1/%2 字节）").arg(wrote).arg(bytes.size());
        return false;
    }

    // detached 启动：本进程马上要退出，脚本必须活得比它久。
    const bool started = QProcess::startDetached(QStringLiteral("cmd.exe"),
                                                 { QStringLiteral("/c"), QDir::toNativeSeparators(cmdPath) });
    if (!started) {
        *why = QStringLiteral("替换脚本启动失败（cmd.exe 没能拉起 %1）").arg(QDir::toNativeSeparators(cmdPath));
        return false;
    }
    return true;
}
