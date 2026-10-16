// 星集控 · 被控端运行时探针：U盘助手模块（_usb_probe.cpp，随仓库提交）
// ───────────────────────────────────────────────────────────────────────────
// 验什么：UsbModule 从「Windows 原生消息」到「灵动岛询问」的整条链——
//   · nativeEventFilter 解析 WM_DEVICECHANGE / DBT_DEVTYP_VOLUME / unitmask → 盘符
//   · 插入：只认可移动盘（C: 固定盘不得打扰）、去重、考试模式静默、
//           autoOpen 直开文件夹、prompt=false 不弹、点「打开」只开根目录
//   · 拔出：清理去重集合（再插能再问）、默认不通知
//   · dispatch("usb_drives") 认领与未启用回执
//
// 为什么不插真 U 盘：本机（N7-20091211）没有可移动介质。正路径用**合成消息**：
//   PostThreadMessageW 把一条 WM_DEVICECHANGE（lParam 指向 DEV_BROADCAST_VOLUME）
//   投到本线程消息队列 —— Qt 的 Windows 事件分发器会原样递给 QAbstractNativeEventFilter，
//   与真实插拔走同一条路。类型判定那一跳用编译宏 STE_USB_PROBE_FAKE_REMOVABLE 指定盘符
//   （usbmodule.cpp 内的探针钩子；生产构建不定义它，test-module-registry.mjs 盯着）。
//   ⚠️ 不链 main.cpp：那是被控端本体，按「本机不开刀」规矩不能起生产进程。
//   本探针只链 modulehost.cpp + usbmodule.cpp，起起来没有窗口、不联网。
//
// 退出码（项目既有约定）：10=全过 20=有断言失败 30=兜底超时。
// 判定不看 stdout（子进程输出常被父脚本管道吞掉）——写 _usb_probe_report.txt。

#include "modulehost.h"
#include "usbmodule.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QTextStream>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

namespace {

struct AskRecord {
    QString title, desc, icon;
    int form = -1;
    QStringList actions;
    int durationMs = -1;
    std::function<void(const QString &)> onAction;
};
struct ShowRecord {
    QString title, desc, icon;
    int form = -1;
    int durationMs = -1;
};

QVector<AskRecord>  g_asks;
QVector<ShowRecord> g_shows;
QStringList         g_openFolders;
bool                g_examActive = false;
QHash<QString, bool> g_moduleSettings;   // key = "<moduleId>/<key>"
QVector<void *>     g_msgAllocs;         // 合成消息的 lParam（退出前统一释放）
int                 g_nativeSeen = 0;    // 日志过滤器见到的消息条数
int                 g_devChangeSeen = 0; // 其中 WM_DEVICECHANGE 的条数

// 诊断用：装在最前面，把流经原生事件过滤器的消息类型/消息号落进报告。
// （合成 WM_DEVICECHANGE 一度没被 UsbModule 收到——没有这个看不见链路断在哪。）
class LogNativeFilter : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(result)
        ++g_nativeSeen;
#ifdef Q_OS_WIN
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg && msg->message == WM_DEVICECHANGE) ++g_devChangeSeen;
#endif
        return false;
    }
};

int  g_fail = 0;
int  g_pass = 0;
QTextStream *g_out = nullptr;

void check(bool ok, const QString &what)
{
    if (ok) { ++g_pass; *g_out << QStringLiteral("PASS  %1\n").arg(what); }
    else    { ++g_fail; *g_out << QStringLiteral("FAIL  %1\n").arg(what); }
    g_out->flush();
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

#ifdef Q_OS_WIN
// 投一条合成 WM_DEVICECHANGE（DBT_DEVTYP_VOLUME，unitmask 命中 letter）。
// ⚠️ 用 SendMessage 打到探针自己的隐藏顶层窗口——2026-10-17 实测：
//    PostThreadMessageW 投的 WM_DEVICECHANGE 到不了原生过滤器（原因未明，
//    同法投 WM_APP+0x7F00 能到），而真实系统的 DBT 广播本来就是 SendMessage 到
//    各顶层窗口、Qt 再转给 QAbstractNativeEventFilter（实验见 _nativeev_test）。
//    这条路径与真实插拔同形，探针才测得真东西。
HWND g_probeHwnd = nullptr;

void postDeviceChange(WPARAM wp, char letter)
{
    DEV_BROADCAST_VOLUME *v = new DEV_BROADCAST_VOLUME;
    memset(v, 0, sizeof(*v));
    v->dbcv_size       = sizeof(DEV_BROADCAST_VOLUME);
    v->dbcv_devicetype = DBT_DEVTYP_VOLUME;
    v->dbcv_reserved   = 0;
    v->dbcv_unitmask   = 1u << (toupper(letter) - 'A');
    g_msgAllocs.append(v);
    if (g_probeHwnd)
        SendMessageW(g_probeHwnd, WM_DEVICECHANGE, wp, reinterpret_cast<LPARAM>(v));
}
#endif

void resetRecords()
{
    g_asks.clear();
    g_shows.clear();
    g_openFolders.clear();
}

ModuleAgentApi makeStubApi()
{
    ModuleAgentApi api;
    api.islandShow = [](const QString &title, const QString &desc, const QString &icon,
                        int form, int durationMs) {
        g_shows.append(ShowRecord{ title, desc, icon, form, durationMs });
    };
    api.islandAsk = [](const QString &title, const QString &desc, const QString &icon, int form,
                       const QStringList &actions,
                       const std::function<void(const QString &)> &onAction, int durationMs) {
        g_asks.append(AskRecord{ title, desc, icon, form, actions, durationMs, onAction });
    };
    api.notify     = [](const QJsonObject &) {};
    api.settingsGet  = [](const QString &) -> QJsonValue { return QJsonValue(); };
    api.settingsSet  = [](const QString &, const QJsonValue &) {};
    api.moduleGet = [](const QString &moduleId, const QString &key,
                       const QJsonValue &dflt) -> QJsonValue {
        const auto it = g_moduleSettings.constFind(moduleId + QLatin1Char('/') + key);
        return it != g_moduleSettings.constEnd() ? QJsonValue(*it) : dflt;
    };
    api.moduleSet = [](const QString &moduleId, const QString &key, const QJsonValue &v) {
        g_moduleSettings.insert(moduleId + QLatin1Char('/') + key, v.toBool());
    };
    api.reportCloud = [](const QString &, const QJsonObject &) {};
    api.openFolder  = [](const QString &path) { g_openFolders.append(path); };
    api.examActive  = []() { return g_examActive; };
    return api;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // 探针自己的设置域，不碰生产 QSettings
    QCoreApplication::setOrganizationName(QStringLiteral("stelarith-probe"));
    QCoreApplication::setApplicationName(QStringLiteral("usb-probe"));

    // 隐藏顶层窗口：合成 WM_DEVICECHANGE 的投递目标（真实 DBT 广播也是打到顶层窗口）
    QWidget probeWindow;
    probeWindow.setWindowTitle(QStringLiteral("usb-probe-window"));
    probeWindow.resize(4, 4);
    (void)probeWindow.winId();
#ifdef Q_OS_WIN
    g_probeHwnd = reinterpret_cast<HWND>(probeWindow.winId());
#endif

    QFile report(QStringLiteral("_usb_probe_report.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "[usb-probe] 报告文件打不开，无法判定\n");
        return 30;
    }
    QTextStream out(&report);
    g_out = &out;
    out << QStringLiteral("== U盘助手模块运行时探针（pid=%1）==\n").arg(qApp->applicationPid());
    out.flush();

    ModuleHost *mh = ModuleHost::instance();
    static LogNativeFilter s_logger;
    qApp->installNativeEventFilter(&s_logger);   // 先装：日志过滤器（不影响链路，只旁观）
    mh->setAgentApi(makeStubApi());
    mh->registerModule(new UsbModule());
    mh->startEnabled();
    out << QStringLiteral("INFO  模块=%1 启用=%2 运行=%3\n")
               .arg(mh->moduleIds().join(QLatin1Char(',')))
               .arg(mh->isEnabled(QStringLiteral("usb")))
               .arg(mh->isRunning(QStringLiteral("usb")));
    out.flush();
    if (!mh->isRunning(QStringLiteral("usb"))) {
        out << QStringLiteral("FAIL  模块未运行（start() 没被调用）\n");
        return 30;
    }

    const QString kFake = QStringLiteral("X");   // 与编译宏 STE_USB_PROBE_FAKE_REMOVABLE 一致

    // ── 1. 插入（正路径）：Centered + 打开/忽略 + 15s 兜底 ──
    resetRecords();
    g_examActive = false;
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);                                   // 覆盖 600ms 延后判定
    check(g_asks.size() == 1, QStringLiteral("1a 插入可移动盘 → 恰好一次询问（实际 %1）").arg(g_asks.size()));
    if (g_asks.size() == 1) {
        const AskRecord &a = g_asks.first();
        check(a.title == QStringLiteral("U 盘已插入"),
              QStringLiteral("1b 标题=「U 盘已插入」（实际 %1）").arg(a.title));
        check(a.desc == QStringLiteral("可移动磁盘 (X:)"),
              QStringLiteral("1c 卷标读不到时回退盘符描述（实际 %1）").arg(a.desc));
        check(a.form == 1, QStringLiteral("1d 形态=Centered(1)（实际 %1）").arg(a.form));
        check(a.actions == QStringList{ QStringLiteral("打开"), QStringLiteral("忽略") },
              QStringLiteral("1e 动作=打开/忽略（实际 %1）").arg(a.actions.join(QLatin1Char('/'))));
        check(a.durationMs == 15000,
              QStringLiteral("1f 15s 兜底超时（实际 %1）").arg(a.durationMs));
        // 点「打开」→ 只开根目录文件夹
        a.onAction(QStringLiteral("打开"));
        check(g_openFolders.size() == 1 && g_openFolders.first() == QStringLiteral("X:\\"),
              QStringLiteral("1g 点「打开」→ 开根目录 X:\\（实际 %1）")
                  .arg(g_openFolders.join(QLatin1Char(';'))));
        a.onAction(QStringLiteral("忽略"));
        check(g_openFolders.size() == 1,
              QStringLiteral("1h 点「忽略」→ 不开任何文件夹（openFolder 仍 %1 次）")
                  .arg(g_openFolders.size()));
    }

    // ── 2. 去重：同一盘重复 arrival 不再问 ──
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);
    check(g_asks.size() == 1,
          QStringLiteral("2a 同一盘重复插入消息 → 不重复询问（累计 %1）").arg(g_asks.size()));

    // ── 3. 拔出 → 清理去重集合（再插能再问），默认不弹拔出提示 ──
    postDeviceChange(DBT_DEVICEREMOVECOMPLETE, 'X');
    spin(300);
    check(g_shows.isEmpty(),
          QStringLiteral("3a 拔出默认静默（islandShow %1 次）").arg(g_shows.size()));
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);
    check(g_asks.size() == 2,
          QStringLiteral("3b 拔出后再插 → 能再次询问（累计 %1）").arg(g_asks.size()));

    // ── 4. 反路径：固定盘（C:）不得打扰 ──
    postDeviceChange(DBT_DEVICEREMOVECOMPLETE, 'X');
    spin(300);
    resetRecords();
    postDeviceChange(DBT_DEVICEARRIVAL, 'C');
    spin(900);
    check(g_asks.isEmpty() && g_openFolders.isEmpty(),
          QStringLiteral("4a 固定盘插入 → 不弹不开（ask %1 / open %2）")
              .arg(g_asks.size()).arg(g_openFolders.size()));

    // ── 5. 考试模式静默 ──
    g_examActive = true;
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);
    check(g_asks.isEmpty(),
          QStringLiteral("5a 考试模式生效 → 静默不弹（ask %1）").arg(g_asks.size()));
    g_examActive = false;

    // ── 6. autoOpen：直接开文件夹，不询问 ──
    postDeviceChange(DBT_DEVICEREMOVECOMPLETE, 'X');     // 先拔出清集合（阶段 5 已把 X 记进 known）
    spin(300);
    g_moduleSettings.insert(QStringLiteral("usb/autoOpen"), true);
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);
    check(g_asks.isEmpty() && g_openFolders.size() == 1
              && g_openFolders.first() == QStringLiteral("X:\\"),
          QStringLiteral("6a autoOpen → 直开根目录不询问（ask %1 / open %2）")
              .arg(g_asks.size()).arg(g_openFolders.join(QLatin1Char(';'))));
    g_moduleSettings.remove(QStringLiteral("usb/autoOpen"));

    // ── 7. prompt=false：彻底不打扰 ──
    g_moduleSettings.insert(QStringLiteral("usb/prompt"), false);
    postDeviceChange(DBT_DEVICEREMOVECOMPLETE, 'X');   // 先拔出清集合
    spin(300);
    resetRecords();
    postDeviceChange(DBT_DEVICEARRIVAL, 'X');
    spin(900);
    check(g_asks.isEmpty() && g_openFolders.isEmpty(),
          QStringLiteral("7a prompt=false → 不弹不开（ask %1 / open %2）")
              .arg(g_asks.size()).arg(g_openFolders.size()));
    g_moduleSettings.remove(QStringLiteral("usb/prompt"));

    // ── 8. dispatch：认领 usb_drives（本机无可移动盘 ⇒ 空数组但 ok）──
    {
        ModuleResult r;
        const bool handled = mh->dispatch(QStringLiteral("usb_drives"), QJsonObject(), r);
        check(handled && r.ok && r.result == QStringLiteral("done")
                  && r.data.value(QStringLiteral("drives")).isArray(),
              QStringLiteral("8a dispatch(usb_drives) 认领且 ok（handled=%1 result=%2）")
                  .arg(handled).arg(r.result));
    }

    // ── 9. 停用后：明确回执「未启用」，不静默落到内置指令 ──
    mh->setEnabled(QStringLiteral("usb"), false);
    {
        ModuleResult r;
        const bool handled = mh->dispatch(QStringLiteral("usb_drives"), QJsonObject(), r);
        check(handled && !r.ok
                  && r.error.contains(QStringLiteral("未启用")),
              QStringLiteral("9a 停用后回执「未启用」（handled=%1 error=%2）")
                  .arg(handled).arg(r.error));
        check(!mh->isRunning(QStringLiteral("usb")),
              QStringLiteral("9b 停用后 running=false（实际 %1）")
                  .arg(mh->isRunning(QStringLiteral("usb"))));
    }
    mh->setEnabled(QStringLiteral("usb"), true);
    check(mh->isRunning(QStringLiteral("usb")),
          QStringLiteral("9c 重新启用后 running=true（实际 %1）")
              .arg(mh->isRunning(QStringLiteral("usb"))));

    // ── 汇总 ──
    out << QStringLiteral("INFO  原生过滤器见到的消息=%1，其中 WM_DEVICECHANGE=%2\n")
               .arg(g_nativeSeen).arg(g_devChangeSeen);
    out << QStringLiteral("== 汇总：PASS %1 / FAIL %2 ==\n").arg(g_pass).arg(g_fail);
    out.flush();
    for (void *p : g_msgAllocs) delete static_cast<DEV_BROADCAST_VOLUME *>(p);
    return g_fail == 0 ? 10 : 20;
}
