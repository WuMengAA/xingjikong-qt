// 星集控 · 内置模块：U盘助手 · 实现（2026-10-16）
// 设计说明与安全口径见 usbmodule.h 顶部。

#include "usbmodule.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#include <fileapi.h>     // GetDriveTypeW / GetVolumeInformationW
#endif

ModuleManifest UsbModule::manifest() const
{
    ModuleManifest m;
    m.id = QStringLiteral("usb");
    m.name = QStringLiteral("U盘助手");
    m.version = QStringLiteral("1.0.0");
    m.description = QStringLiteral("U 盘插入时在灵动岛弹窗询问是否打开（只开文件夹、绝不自动运行）；考试模式可自动静默。");
    m.permissions = { QStringLiteral("island"), QStringLiteral("notify"), QStringLiteral("settings") };
    // usb_drives：管理端可查这台机器当前插着哪些可移动盘（只读列举，不读内容）。
    m.actions = { QStringLiteral("usb_drives") };
    m.events = {};
    m.enabledByDefault = true;
    return m;
}

void UsbModule::start()
{
#ifdef Q_OS_WIN
    qApp->installNativeEventFilter(this);
#endif
}

void UsbModule::stop()
{
#ifdef Q_OS_WIN
    qApp->removeNativeEventFilter(this);
#endif
    m_known.clear();
}

bool UsbModule::execAction(const QString &action, const QJsonObject &params, ModuleResult &out)
{
    Q_UNUSED(params);
    if (action != QStringLiteral("usb_drives")) return false;
    const QStringList ds = removableDrives();
    QJsonArray arr;
    for (const QString &d : ds) arr.append(d);
    out.ok = true;
    out.result = QStringLiteral("done");
    out.data = QJsonObject{ { QStringLiteral("drives"), arr } };
    return true;
}

QStringList UsbModule::removableDrives() const
{
    QStringList out;
#ifdef Q_OS_WIN
    for (char c = 'A'; c <= 'Z'; ++c) {
        const QString root = QString(QChar::fromLatin1(c)) + QStringLiteral(":\\");
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_REMOVABLE)
            out << QString(QChar::fromLatin1(c)) + QStringLiteral(":");
    }
#endif
    return out;
}

bool UsbModule::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    // Qt6 在 Windows 上的事件类型名（Qt5 是 windows_generic_MSG；两条都接，防版本差异）
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (!msg || msg->message != WM_DEVICECHANGE) return false;

    const WPARAM wp = msg->wParam;
    if (wp != DBT_DEVICEARRIVAL && wp != DBT_DEVICEREMOVECOMPLETE) return false;
    if (!msg->lParam) return false;

    const PDEV_BROADCAST_HDR hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(msg->lParam);
    if (hdr->dbch_devicetype != DBT_DEVTYP_VOLUME) return false;   // 只关心卷（U盘/光盘）
    const PDEV_BROADCAST_VOLUME vol = reinterpret_cast<PDEV_BROADCAST_VOLUME>(hdr);

    for (int i = 0; i < 26; ++i) {
        if (!(vol->dbcv_unitmask & (1u << i))) continue;
        const QString drive = QString(QChar::fromLatin1('A' + i)) + QStringLiteral(":");
        if (wp == DBT_DEVICEARRIVAL) {
            // ⚠️ 文件系统挂载有延迟：此刻 GetDriveType 可能还判不出类型（甚至还没盘符就绪），
            //    立刻判定会把 U 盘漏掉 ⇒ 延后 600ms 再看（实测 300ms 偶尔仍早，600 稳）。
            QTimer::singleShot(600, this, [this, drive] { onDriveArrived(drive); });
        } else {
            onDriveRemoved(drive);
        }
    }
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
#endif
    return false;   // 不吞消息：别的过滤器（含 Qt 自己的）照常收到
}

void UsbModule::onDriveArrived(const QString &drive)
{
    if (m_known.contains(drive)) return;                 // 同一盘短时间重复消息去重
#ifdef Q_OS_WIN
    const QString root = drive + QStringLiteral("\\");
#ifdef STE_USB_PROBE_FAKE_REMOVABLE
    // 探针专用钩子：本机没有 U 盘可插，正路径（islandAsk 询问）只能把宏指明的盘符
    // 当作可移动盘来验。**生产构建绝不定义本宏**——scripts/test-module-registry.mjs
    // 盯着 CMakeLists，出现即红。
    // ⚠️ 只对宏指定的盘符放行，其余盘符仍走真实 GetDriveType（否则"固定盘不打扰"
    //    会变成恒真断言，探针自欺）。
    const bool fakeRemovable =
        drive.compare(QStringLiteral(STE_USB_PROBE_FAKE_REMOVABLE), Qt::CaseInsensitive) == 0;
    const UINT typeProbe = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
    if (!fakeRemovable && typeProbe != DRIVE_REMOVABLE) return;
#else
    const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
    if (type != DRIVE_REMOVABLE) return;                 // 只认 U 盘/移动硬盘；光驱、网络盘不打扰
#endif
    m_known.insert(drive);

    // 卷标（读不到就退回盘符）
    WCHAR volName[MAX_PATH + 1] = { 0 };
    const BOOL gotVol = GetVolumeInformationW(reinterpret_cast<LPCWSTR>(root.utf16()),
                                              volName, MAX_PATH,
                                              nullptr, nullptr, nullptr, nullptr, 0);
    const QString label = gotVol ? QString::fromWCharArray(volName).trimmed() : QString();
    const QString human = label.isEmpty()
                          ? QStringLiteral("可移动磁盘 (%1)").arg(drive)
                          : QStringLiteral("%1 (%2)").arg(label, drive);

    if (setting(QStringLiteral("reportCloud"), false)) {
        api().reportCloud(QStringLiteral("usb_event"), QJsonObject{
            { QStringLiteral("kind"),   QStringLiteral("insert") },
            { QStringLiteral("drive"),  drive },
            { QStringLiteral("label"),  label } });
    }

    // 考试模式静默：不弹、不自动打开（默认开）——上课/考试时被 U 盘打断是事故。
    if (api().examActive && api().examActive() && setting(QStringLiteral("quietInExam"), true))
        return;

    if (setting(QStringLiteral("autoOpen"), false)) {
        if (api().openFolder) api().openFolder(root);
        return;
    }
    if (!setting(QStringLiteral("prompt"), true)) return;

    // 询问：Centered（胶囊那档太矮放不下按钮）+ 15s 兜底超时（不给超时会一直占着岛）。
    api().islandAsk(QStringLiteral("U 盘已插入"), human, QStringLiteral("folder"),
                    1 /* Centered */,
                    QStringList{ QStringLiteral("打开"), QStringLiteral("忽略") },
                    [this, root](const QString &label) {
                        // ⚠️ 只开**文件夹**：绝不打开/运行盘上任何文件（不可信介质）。
                        if (label == QStringLiteral("打开") && api().openFolder)
                            api().openFolder(root);
                    },
                    15000);
#else
    Q_UNUSED(drive);
#endif
}

void UsbModule::onDriveRemoved(const QString &drive)
{
    if (!m_known.remove(drive)) return;
    if (setting(QStringLiteral("reportCloud"), false)) {
        api().reportCloud(QStringLiteral("usb_event"), QJsonObject{
            { QStringLiteral("kind"),  QStringLiteral("remove") },
            { QStringLiteral("drive"), drive } });
    }
    if (!setting(QStringLiteral("notifyRemove"), false)) return;
    if (api().examActive && api().examActive() && setting(QStringLiteral("quietInExam"), true))
        return;
    api().islandShow(QStringLiteral("U 盘已拔出"), QStringLiteral("可移动磁盘 (%1)").arg(drive),
                     QStringLiteral("folder"), 0 /* Capsule */, 3000);
}

bool UsbModule::setting(const QString &key, bool dflt) const
{
    if (!api().moduleGet) return dflt;
    return api().moduleGet(QStringLiteral("usb"), key, dflt).toBool();
}
