// 星集控被控端 · 设置域实现（2026-10-28 模块三）
// 口径与设计见 settings_domain.h 顶部。
#include "settings_domain.h"

#include "../core/AgentSettings.h"   // 只读「异常后自动进安全模式」这一个开关（QSettings 口径统一在那边）

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace SettingsDomain {
namespace {

QString g_dataDir;
QString g_markerPath;
bool    g_lastAbnormal = false;
bool    g_safeMode = false;

QString markerPath(const QString &dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("agent-run.flag"));
}

} // namespace

void init(const QString &dataDir)
{
    g_dataDir = dataDir;
    QDir().mkpath(dataDir);
    g_markerPath = markerPath(dataDir);

    // ① 上次标记还在 = 上次没走有序退出（崩溃/断电/任务管理器结束）
    g_lastAbnormal = QFileInfo::exists(g_markerPath);

    // ② 写本次标记（内容给人看：pid + 启动时刻；判定只看"文件在不在"）
    {
        QFile f(g_markerPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            ts << QStringLiteral("pid=%1\r\nstarted=%2\r\n")
                      .arg(QCoreApplication::applicationPid())
                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        }
    }

    // ③ 有序退出时删标记（硬崩溃不会走到这里 —— 那正是判定依据）
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         [] { QFile::remove(g_markerPath); });
    }

    // ④ 异常退出 + 用户开了自动防护 ⇒ 本次直接进安全模式
    if (g_lastAbnormal && AgentSettings::safeModeAuto())
        g_safeMode = true;

    qInfo("[settings] 启动标记已写；上次退出＝%s；安全模式＝%s",
          g_lastAbnormal ? "异常" : "正常", g_safeMode ? "开" : "关");
}

bool lastExitAbnormal() { return g_lastAbnormal; }

bool safeModeActive() { return g_safeMode; }

void setSafeModeActive(bool on)
{
    if (g_safeMode == on) return;
    g_safeMode = on;
    qInfo("[settings] 安全模式 → %s", on ? "开（本次不加载第三方插件）" : "关");
}

QString lastExitText()
{
    if (!g_lastAbnormal)
        return QStringLiteral("上次正常退出");
    return QStringLiteral("上次异常退出（崩溃 / 断电 / 强杀进程）");
}

// 路径归一：Windows 上 dataDir 可能是 "C:/x/y" 而 absolutePath() 给 "C:\x\y"，
// 直接 startsWith 会因分隔符不同而恒 false（2026-11-02 存储面板三倍数的根因之一）。
// 注意不能只做 toLower 不换分隔符 —— 那正是出问题的写法。
static QString canonicalPath(const QString &p)
{
    if (p.isEmpty()) return p;
    QString s = QDir::cleanPath(p);
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return s.toLower();
}

// p 是否落在 dir 之下（含 dir 本身）。都走 canonicalPath，避免分隔符/大小写假阴性。
static bool pathWithin(const QString &p, const QString &dir)
{
    const QString cp = canonicalPath(p);
    const QString cd = canonicalPath(dir);
    if (cd.isEmpty() || cp.isEmpty()) return false;
    if (cp == cd) return true;
    return cp.startsWith(cd.endsWith(QLatin1Char('\\')) ? cd : cd + QLatin1Char('\\'));
}

StorageBreakdown storageBreakdown(const QString &dataDir, const QString &logDir)
{
    StorageBreakdown out;
    QFileInfo di(dataDir);
    if (!di.exists() || !di.isDir()) {
        out.logs = dirSizeBytes(logDir);
        return out;
    }
    // ★ logDir 常常**就是** dataDir 本身：g_logPath 默认 dataDir/agent-qt.log，
    //   取其 absolutePath 得到 dataDir。所以「是否在 logDir 之下」不能当分类依据
    //   （那会把所有文件都算成日志，配置与数据两栏变 0），必须按文件特征判。
    const bool logInsideData = !logDir.isEmpty() && pathWithin(logDir, dataDir);
    const QString logSub = canonicalPath(QDir(dataDir).filePath(QStringLiteral("logs")));

    QDirIterator it(dataDir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const qint64 sz = fi.size();
        const QString dirPath = canonicalPath(fi.absolutePath());
        const QString name = fi.fileName().toLower();
        const bool isLogName = name.endsWith(QStringLiteral(".log"))
                            || name.contains(QStringLiteral(".log."));
        if (pathWithin(dirPath, logSub) || isLogName) { out.logs += sz; continue; }
        // 直接躺在 dataDir 根下的是配置；有子目录层级的一律算数据。
        if (dirPath == canonicalPath(dataDir)) out.config += sz;
        else out.data += sz;
    }
    // 日志目录在数据目录之外时，那边的日志没被上面扫到，单独统计。
    if (!logInsideData) out.logs += dirSizeBytes(logDir);
    return out;
}

qint64 dirSizeBytes(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists()) return 0;
    if (fi.isFile()) return fi.size();

    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    const double kb = double(bytes) / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    const double mb = kb / 1024.0;
    if (mb < 1024.0) return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
}

int backupConfig(const QString &dataDir, const QString &dir, QString *err)
{
    const QStringList files{
        QStringLiteral("agent.env"),
        QStringLiteral("devrole.txt"),
        QStringLiteral("classcode.txt"),
        QStringLiteral("channel.txt"),
        QStringLiteral("timesync.json"),
        QStringLiteral("play-schedule.json"),
        QStringLiteral("automation.json"),
    };
    int copied = 0;
    if (!QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建备份目录：%1").arg(dir);
        return 0;
    }
    QStringList missed;
    for (const QString &name : files) {
        const QString src = QDir(dataDir).filePath(name);
        if (!QFileInfo::exists(src)) { missed << name; continue; }
        const QString dst = QDir(dir).filePath(name);
        if (QFile::copy(src, dst)) ++copied;
        else if (err) *err = QStringLiteral("复制失败：%1").arg(name);
    }
    // 插件清单（只记名字与版本，不搬本体）：恢复时用来对账"少装了哪些"
    {
        const QStringList pluginDirs{
            QDir(dataDir).filePath(QStringLiteral("plugins")),
        };
        QStringList lines;
        for (const QString &p : pluginDirs) {
            QDir d(p);
            if (!d.exists()) continue;
            for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                lines << sub;
        }
        if (!lines.isEmpty()) {
            QFile f(QDir(dir).filePath(QStringLiteral("plugins.txt")));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream ts(&f);
                ts.setEncoding(QStringConverter::Utf8);
                for (const QString &l : lines) ts << l << "\r\n";
                ++copied;
            }
        }
    }
    if (copied == 0 && err)
        *err = QStringLiteral("没有可备份的配置文件（数据目录：%1）").arg(dataDir);
    qInfo("[settings] 备份完成：%d 个文件 → %s（缺 %d 项）",
          copied, qPrintable(dir), int(missed.size()));
    return copied;
}

qint64 clearOldLogs(const QString &dataDir)
{
    qint64 freed = 0;
    QDir d(dataDir);
    const QStringList files =
        d.entryList(QStringList{QStringLiteral("*.log.*")}, QDir::Files | QDir::Hidden);
    for (const QString &fn : files) {
        const QString p = d.filePath(fn);
        const qint64 sz = QFileInfo(p).size();
        if (QFile::remove(p)) freed += sz;
    }
    qInfo("[settings] 清理旧日志：%d 个文件，释放 %s",
          int(files.size()), qPrintable(humanSize(freed)));
    return freed;
}

int resetSettings(const QString &dataDir)
{
    // 只删"设置"：连云端地址/令牌一起清掉（下次启动回到未配置）。
    // ⚠️ 不碰 devrole.txt / classcode.txt —— 那是**设备身份**，删了要重新 OOBE 绑定，
    //    在教室里等于把设备从班里踢出去。也不碰日志与数据。
    const QStringList files{
        QStringLiteral("agent.env"),
        QStringLiteral("timesync.json"),
        QStringLiteral("play-schedule.json"),
        QStringLiteral("automation.json"),
        QStringLiteral("channel.txt"),
    };
    int removed = 0;
    QDir d(dataDir);
    for (const QString &name : files) {
        const QString p = d.filePath(name);
        if (QFileInfo::exists(p) && QFile::remove(p)) ++removed;
    }
    // QSettings 里的功能开关一并清
    QSettings().clear();
    QSettings().sync();
    qInfo("[settings] 重置设置：删除 %d 个文件 + 清空 QSettings（设备身份保留）", removed);
    return removed;
}

} // namespace SettingsDomain
