// 进程黑名单 + 开机自启 / 桌面快捷方式（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）
//
// // 搬出理由：系统集成层（Win32 进程 / 注册表自启 / 快捷方式），与云端链路无关。
// // 单独成文件的价值：这类代码在本机调试时最容易被安全软件拦，隔离后有据可查。

#include "viewerbackend.h"
#include "viewerbackend_shared.h"
#include <QCoreApplication>
#include "winlauncher.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

QStringList ViewerBackend::procBlacklist() const { return m_procBlacklist; }
void ViewerBackend::addProcToBlacklist(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    // 不区分大小写去重：Windows 进程名大小写随意（Chrome.exe / chrome.EXE 都见过），
    // 存两份会让"一键关闭"看起来只关掉一半。
    for (const QString &e : m_procBlacklist)
        if (e.compare(n, Qt::CaseInsensitive) == 0) return;
    m_procBlacklist.append(n);
    QSettings s;
    s.setValue(QStringLiteral("proc/blacklist"), m_procBlacklist);
    logf("[viewer] 进程黑名单新增：%s", n.toUtf8().constData());
    emit procBlacklistChanged();
}
void ViewerBackend::removeProcFromBlacklist(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    const int before = m_procBlacklist.size();
    m_procBlacklist.removeIf([&n](const QString &e) { return e.compare(n, Qt::CaseInsensitive) == 0; });
    if (m_procBlacklist.size() == before) return;
    QSettings s;
    s.setValue(QStringLiteral("proc/blacklist"), m_procBlacklist);
    logf("[viewer] 进程黑名单移除：%s", n.toUtf8().constData());
    emit procBlacklistChanged();
}
int ViewerBackend::killBlacklisted(const QVariantList &items)
{
    if (m_currentUid.isEmpty()) {
        logf("[viewer] FAIL 没选设备，一键关闭黑名单无从下手");
        return -1;   // -1 = 没选设备（区别于 0 = 选了但一个都没匹配上）
    }
    if (m_procBlacklist.isEmpty()) return 0;

    int sent = 0;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        const int pid = m.value(QStringLiteral("pid")).toInt();
        if (name.isEmpty() || pid <= 0) continue;
        bool hit = false;
        for (const QString &b : m_procBlacklist) {
            if (name.compare(b, Qt::CaseInsensitive) == 0) { hit = true; break; }
        }
        if (!hit) continue;
        // 受保护的进程（系统关键进程）不碰：被控端自己也会拒，
        // 但这里先拦一道，免得回执流水里多出一堆"拒绝"让人误会功能坏了。
        if (m.contains(QStringLiteral("protected"))) continue;
        QJsonObject p;
        p.insert(QStringLiteral("pid"), pid);
        sendAction(QStringLiteral("process_stop"), p);
        ++sent;
    }
    logf("[viewer] 一键关闭黑名单：命中并下发 %d 个 process_stop → %s",
         sent, m_currentUid.toUtf8().constData());
    return sent;
}
bool ViewerBackend::autoStartEnabled() const
{
    QSettings run(QString::fromLatin1(kViewerRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kViewerRunValue));
}
void ViewerBackend::setAutoStart(bool on)
{
    QSettings run(QString::fromLatin1(kViewerRunKey), QSettings::NativeFormat);
    const QString key = QString::fromLatin1(kViewerRunValue);
    if (on) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(key, QStringLiteral("\"%1\"").arg(exe));
    } else {
        run.remove(key);
    }
    run.sync();
    logf("[viewer] 开机自启 → %s（HKCU Run）", on ? "开" : "关");
    emit autostartChanged();
}
bool ViewerBackend::desktopShortcutExists() const
{
    const QString p = viewerDesktopLnk();
    return !p.isEmpty() && QFileInfo::exists(p);
}
void ViewerBackend::setDesktopShortcut(bool on)
{
    const QString lnk = viewerDesktopLnk();
    if (lnk.isEmpty()) {
        setStatus(QStringLiteral("取不到桌面路径，快捷方式建不了"), true);
        return;
    }
    if (!on) {
        if (QFile::exists(lnk) && !QFile::remove(lnk)) {
            setStatus(QStringLiteral("桌面快捷方式删不掉（可能被占用）"), true);
            return;
        }
        emit autostartChanged();
        return;
    }
    const QString exe  = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString work = QDir::toNativeSeparators(QFileInfo(exe).absolutePath());
    // 用 WScript.Shell 建 .lnk（无 Qt Ax 依赖）。失败会在签名/策略受限的机器上发生 ——
    // 这里只 startDetached，不阻塞主线程；建没建成靠 desktopShortcutExists() 复查。
    const QString ps = QStringLiteral(
        "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%1');"
        "$s.TargetPath='%2';$s.WorkingDirectory='%3';$s.IconLocation='%2,0';$s.Save()")
        .arg(QDir::toNativeSeparators(lnk), exe, work);
    // #368 需求①：静默 detached 拉起，杜绝建桌面快捷方式时弹出 powershell 窗
    stelarithStartHiddenDetached(QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("bypass"),
         QStringLiteral("-Command"), ps});
    emit autostartChanged();
}
