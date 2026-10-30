#include "policy_engine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>

#include <windows.h>
#include <tlhelp32.h>

#include "../core/proc_inspect.h"   // snapshotProcessNames（外部链接，main 拆分 #13；构建无 -Isrc，跨 src/ 用 ../ 前缀）

// ── 注入上下文（main.cpp 早期 policyInit 填充）──
PolicyDeps *g_policyDeps = nullptr;

// ── 状态全局（外部链接；main.cpp extern 后取地址注入 ActionDeps）──
QHash<QString, QTimer *> g_schedTimerById;   // 任务 id → 定时器（取消时必须停掉，否则到点仍会执行）
QString g_schedFile;               // 调度持久化文件（崩溃恢复后重挂未到期任务）
SoftPolicy g_soft;
QString g_softPolicyFile;          // %LOCALAPPDATA%\xingjikong\soft-manage.json
QSet<quint32> g_frozenPids;        // 已冻结的 pid（避免重复冻结/误解冻）

// ── 内部状态（文件内 static，不暴露）──
static QString g_schedDir;         // 调度文件所在目录（写文件要 mkpath）
static QVector<QTimer *> g_schedTimers;   // 定时关机：每个任务一个单次定时器（触发即执行 ExitWindowsEx）
static QTimer *g_softGuard = nullptr;

// ══════════ 三张**硬禁止名单**：写死在代码里，不可配置、不可被下发指令绕过（Rust 版原话）。 ══════════
// 判定口径（照抄 Rust `is_protected`）：
//   集控自身 —— 去掉 .exe 后 **等于或以它开头**
//   系统关键 —— 去掉 .exe 后 **精确等于**
//   安全软件 —— 去掉 .exe 后 **包含**即命中（360/火绒/卡巴的变体太多）
// 注：namespace 作用域 const 默认内部链接，本文件内可见即可。
static const char *const kProtectedSelf[] = { "classisland", "xingjikong", "stelarith-agent", "stelarith", nullptr };
static const char *const kProtectedSystem[] = {
    "system", "idle", "csrss", "wininit", "winlogon", "lsass", "smss", "services", "svchost",
    "explorer", "dwm", "fontdrvhost", "ctfmon", "runtimebroker", "taskhostw", "searchindexer",
    "spoolsv", "audiodg", "conhost", "wudfhost", "sihost", "shellhost", "startmenuexperiencehost",
    "searchhost", "textinputhost", "applicationframehost", "systemsettings", "lockapp", nullptr
};
static const char *const kProtectedSecurity[] = {
    "360", "qhsafe", "360safe", "360tray", "360sd", "huorong", "hrsword", "usysdiag", "hipstray",
    "msmpeng", "nissrv", "mssense", "securityhealth", "defender", "smartscreen", "avp", "avguard",
    "kxfed", "kxetray", "kxe", "qqpcrtp", "qqpctray", "baidusd", "bdagent", "ksafe", "kxescore",
    "kws", "avast", "avg", "avira", "norton", "mcafee", "kaspersky", "eset", "ekrn", "bdservice",
    "safemon", "rsmain", "rsnetsvr", "wrar", "trojan", "hips", nullptr
};

/** 受保护就返回人话类别（写进回执 error），可安全关闭返回空串。 */
QString protectedCategory(const QString &exeName)
{
    QString base = exeName.toLower();
    if (base.endsWith(QStringLiteral(".exe"))) base.chop(4);
    for (const char *const *p = kProtectedSelf; *p; ++p)
        if (base == QLatin1String(*p) || base.startsWith(QLatin1String(*p)))
            return QStringLiteral("集控自身的组成程序");
    for (const char *const *p = kProtectedSystem; *p; ++p)
        if (base == QLatin1String(*p)) return QStringLiteral("系统关键进程");
    for (const char *const *p = kProtectedSecurity; *p; ++p)
        if (base.contains(QLatin1String(*p))) return QStringLiteral("安全软件");
    return QString();
}

// ── 调度（定时关机/重启）──

// 持久化：写失败必须报出来（否则崩溃恢复等于没做）。
void saveSchedules(const QVector<SchedTask> &tasks)
{
    if (g_schedDir.isEmpty()) return;
    QDir().mkpath(g_schedDir);
    QJsonArray arr;
    for (const auto &t : tasks) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), t.id);
        o.insert(QStringLiteral("what"), t.what);
        o.insert(QStringLiteral("at"), QJsonValue(t.at));
        o.insert(QStringLiteral("createdMs"), QJsonValue(t.createdMs));
        arr.append(o);
    }
    QFile f(g_schedFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.close();
    } else {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 调度任务持久化失败（写 %1 打不开）—— 本机重启后这批定时任务会丢").arg(g_schedFile);
    }
}

QVector<SchedTask> loadSchedules()
{
    QVector<SchedTask> out;
    QFile f(g_schedFile);
    if (!f.open(QIODevice::ReadOnly)) return out;   // 没文件＝还没有任何计划，正常
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 调度文件不是合法 JSON 数组：%1（已忽略，不当成功）").arg(g_schedFile);
        return out;
    }
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        SchedTask t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.what = o.value(QStringLiteral("what")).toString();
        t.at = o.value(QStringLiteral("at")).toVariant().toLongLong();
        t.createdMs = o.value(QStringLiteral("createdMs")).toVariant().toLongLong();
        if (!t.id.isEmpty() && !t.what.isEmpty() && t.at > 0) out.append(t);
    }
    return out;
}

// 到期是否**真的**关机/重启。
//
// 默认**不真执行**（fail-safe，不是 fail-open）：到点只记日志、任务记录保留。
// 只有装机脚本显式把 STE_QT_ALLOW_SCHED_FIRE 设成 1 才真关机 ——
// 本机验证环境 / 开发机上跑定时，绝不能"一测就重启自己"。
// 这条与项目红线一致：破坏性动作只许在 TEST1 那台真触发。
// 代价：忘了设变量的机器上"设了定时但实际没关"。所以**回执里必须把 fireAllowed 报出来**，
// 让管理端一眼看见"这条定时当前不会真执行"，而不是静默假装成功。
bool schedFireAllowed()
{
    return qEnvironmentVariableIntValue("STE_QT_ALLOW_SCHED_FIRE") == 1;
}

// 到点回调：删掉对应任务记录 → 执行关机/重启。
// 顺序不能反：先删记录再关机，否则下次启动又把它挂回来，变成"永远关不掉"。
static void fireScheduledTask(const QString &taskId, const QString &what)
{
    qInfo("[agent-qt] ⏰ 定时任务到期 id=%s action=%s", qPrintable(taskId), qPrintable(what));

    // 不允许真执行：保留任务记录（下次到期继续提醒，不静默丢弃），如实说清楚为什么没执行。
    if (!schedFireAllowed()) {
        qWarning("[agent-qt] ⏰ 定时任务 id=%s 已到期，但 STE_QT_ALLOW_SCHED_FIRE≠1，"
                 "**未真执行** %s（默认保护）。装机时把该变量设为 1 才会真正关机/重启。",
                 qPrintable(taskId), qPrintable(what));
        return;
    }

    {
        QVector<SchedTask> cur = loadSchedules();
        for (int i = 0; i < cur.size(); ++i)
            if (cur.at(i).id == taskId) { cur.removeAt(i); break; }
        saveSchedules(cur);
    }
    // 单次定时器触发后不会再自己消失，手动摘掉映射（对象由 Qt 事件循环后续回收）
    g_schedTimerById.remove(taskId);
    g_policyDeps->enableShutdownPriv();
    const DWORD flags = (what == QStringLiteral("reboot") ? EWX_REBOOT : EWX_SHUTDOWN) | EWX_FORCE;
    const BOOL ok = ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED);
    if (!ok)
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 定时 %1 的 ExitWindowsEx 失败，错误码 %2").arg(what).arg((int)GetLastError());
    // 进程即将被系统杀掉；等一小会儿让日志刷出去
    QCoreApplication::processEvents();
    if (ok) QCoreApplication::exit(0);
}

// 把某个任务挂成单次定时器。timeout 传剩余毫秒。
void armScheduleTimer(const SchedTask &t, int delayMs)
{
    QTimer *tm = new QTimer();
    tm->setParent(qApp);
    tm->setSingleShot(true);
    tm->start(delayMs);
    const QString id = t.id;
    const QString what = t.what;
    QObject::connect(tm, &QTimer::timeout, [id, what] { fireScheduledTask(id, what); });
    g_schedTimers.append(tm);
    g_schedTimerById.insert(id, tm);   // 登记映射：cancel_schedule 要能精确停掉对应定时器
}

// 启动时从 %LOCALAPPDATA%\xingjikong\schedules.json 把未到期任务重新挂上。
// 任务不能只活在内存里 —— 进程崩溃/重启后必须能自己接上，否则"定时关机"就是个摆设。
static void reschedulePersisted()
{
    const QVector<SchedTask> tasks = loadSchedules();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int dropped = 0;
    QVector<SchedTask> kept;
    for (const auto &t : tasks) {
        if (t.at <= now) { ++dropped; continue; }   // 过期未执行（当时不在线），不再补做——如实丢弃
        kept.append(t);
        int delay = (int)qMin<qint64>(t.at - now, 2147483647LL);   // QTimer 上限 INT_MAX
        armScheduleTimer(t, delay);
    }
    saveSchedules(kept);   // 过期项清掉，文件保持干净
    if (!tasks.isEmpty())
        qInfo("[agent-qt] 调度恢复：载入 %d 条，重挂 %d 条，丢弃过期 %d 条",
              (int)tasks.size(), (int)kept.size(), dropped);
}

// ── 软控（黑白名单/进程冻结/去网/守护）──

/** 归一化：去掉 .exe、小写 —— 与保护名单判定、exam_mode 的口径一致。 */
static QString softNorm(const QString &raw)
{
    QString s = raw.trimmed().toLower();
    if (s.endsWith(QStringLiteral(".exe"))) s.chop(4);
    return s;
}

bool softHit(const QStringList &list, const QString &exeName)
{
    const QString n = softNorm(exeName);
    if (n.isEmpty()) return false;
    for (const QString &raw : list)
        if (softNorm(raw) == n) return true;
    return false;
}

void saveSoftPolicy()
{
    if (g_softPolicyFile.isEmpty()) return;
    QJsonObject o;
    QJsonArray bl, wl;
    for (const QString &s : g_soft.blacklist) bl.append(s);
    for (const QString &s : g_soft.whitelist) wl.append(s);
    o.insert(QStringLiteral("blacklist"), bl);
    o.insert(QStringLiteral("whitelist"), wl);
    o.insert(QStringLiteral("denyRun"), g_soft.denyRun);
    QFile f(g_softPolicyFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 软件策略持久化失败（写 soft-manage.json 打不开）");
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

static void loadSoftPolicy()
{
    if (g_softPolicyFile.isEmpty()) return;
    QFile f(g_softPolicyFile);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const auto fill = [](const QJsonArray &a) {
        QStringList out;
        for (const auto &v : a) { const QString s = v.toString().trimmed(); if (!s.isEmpty()) out.append(s); }
        return out;
    };
    g_soft.blacklist = fill(o.value(QStringLiteral("blacklist")).toArray());
    g_soft.whitelist = fill(o.value(QStringLiteral("whitelist")).toArray());
    g_soft.denyRun = o.value(QStringLiteral("denyRun")).toBool(false);
}

/** pid → 可执行文件路径（去网要按 program 路径加防火墙规则）。 */
QString processPathOf(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return QString();
    WCHAR buf[MAX_PATH * 2] = {0};
    DWORD sz = sizeof(buf) / sizeof(WCHAR);
    const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &sz);
    CloseHandle(h);
    return ok ? QString::fromWCharArray(buf) : QString();
}

// 冻结/解冻：ntdll 的 NtSuspendProcess / NtResumeProcess（未文档化但长期稳定；
// 取不到或权限不足就如实失败，不做"假装成功"）。
typedef LONG (NTAPI *NtSuspResPtr)(HANDLE);
bool setProcessFrozen(quint32 pid, bool frozen)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    const char *sym = frozen ? "NtSuspendProcess" : "NtResumeProcess";
    FARPROC fn = GetProcAddress(ntdll, sym);
    if (!fn) return false;
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, (DWORD)pid);
    if (!h) return false;
    const LONG st = reinterpret_cast<NtSuspResPtr>(fn)(h);
    CloseHandle(h);
    return st >= 0;
}

// 去网 / 恢复：Windows 防火墙**出站**规则（netsh advfirewall）。
// ⚠️ 需管理员权限 —— 权限不足时 netsh 非零退出，如实回 failed，不静默装成功。
bool setNetBlocked(const QString &exePath, bool block)
{
    if (exePath.isEmpty()) return false;
    const QString rule = QStringLiteral("StelarithDenyNet_") + QFileInfo(exePath).completeBaseName();
    QStringList args;
    if (block) {
        args << QStringLiteral("advfirewall") << QStringLiteral("firewall") << QStringLiteral("add")
             << QStringLiteral("rule") << (QStringLiteral("name=") + rule)
             << QStringLiteral("dir=out") << QStringLiteral("action=block")
             << (QStringLiteral("program=") + exePath);
    } else {
        args << QStringLiteral("advfirewall") << QStringLiteral("firewall") << QStringLiteral("delete")
             << QStringLiteral("rule") << (QStringLiteral("name=") + rule);
    }
    QProcess p;
    p.start(QStringLiteral("netsh"), args);
    if (!p.waitForFinished(8000)) return false;
    return p.exitCode() == 0;
}

/** 黑名单守护：轮询终止命中黑名单的进程（保护名单优先，绝不动系统/安全/自身）。 */
static void softGuardTick()
{
    if (!g_soft.denyRun || g_soft.blacklist.isEmpty()) return;
    const QHash<quint32, QString> names = snapshotProcessNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        const QString exe = it.value();
        if (exe.isEmpty()) continue;
        if (!protectedCategory(exe).isEmpty()) continue;      // 护栏：受保护一律跳过
        if (!softHit(g_soft.blacklist, exe)) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)it.key());
        if (!h) continue;
        TerminateProcess(h, 1);
        CloseHandle(h);
        qInfo("[agent-qt] 软件管控：已终止黑名单进程 %s (pid=%u)", qPrintable(exe), (unsigned)it.key());
    }
}

// ── 装配（main() 调用）──

void policyInit(const PolicyDeps &deps)
{
    static PolicyDeps s_deps = deps;   // 拷贝一次，之后经 g_policyDeps 只读
    g_policyDeps = &s_deps;
}

void policySetSchedDir(const QString &dir)
{
    g_schedDir = dir;
    g_schedFile = dir + QStringLiteral("/schedules.json");
}

void policyStart(const QString &softPolicyFile, QObject *parent)
{
    g_softPolicyFile = softPolicyFile;
    loadSoftPolicy();
    g_softGuard = new QTimer(parent);
    g_softGuard->setInterval(3000);                              // 3 秒一轮，命中黑名单即终止
    QObject::connect(g_softGuard, &QTimer::timeout, [] { softGuardTick(); });
    g_softGuard->start();
    if (!g_soft.blacklist.isEmpty() || !g_soft.whitelist.isEmpty())
        qInfo("[agent-qt] 软件策略已载入：黑 %d / 白 %d / 守护=%s",
              (int)g_soft.blacklist.size(), (int)g_soft.whitelist.size(),
              g_soft.denyRun ? "开" : "关");
}
