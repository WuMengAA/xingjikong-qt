// 考试模式实现（见 exam_mode.h 头注释）。
#include "exam_mode.h"
#include "notify/notify_entry.h"   // 2026-11-01：考试模式提示（提供方 exam）
#include "proc_guard.h"   // 内核闸门（与 main.cpp 的 process_stop 共用，2026-10-13 抽出）

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QProcess>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <windows.h>
#include <tlhelp32.h>   // CreateToolhelp32Snapshot（进程枚举，替代 fork tasklist.exe）

// Windows 原生：把窗口置顶（WS_EX_TOPMOST）+ 抢占 Alt+Tab 等（WS_EX_TOOLWINDOW 隐藏任务栏图标）。
// 不拦截 Ctrl+Alt+Del（Windows 安全键，无法完全拦截 —— 文档 3.7 诚实边界）。
// 第一版不装低级键盘钩子：全屏无边框 + 置顶 + 无焦点逃逸路径，对学生已足够"防君子"。

namespace {
// 黑名单轮询间隔（每 2 秒比对一次进程列表）
constexpr int kWatchdogMs = 2000;
// 倒计时刷新间隔（1 秒）
constexpr int kTickerMs = 1000;

// ── 进程安全性判定：一律**问内核**，不猜进程名名单 ──────────────────────────
// 2026-10-09 学校机房"反复重启"事故换来的教训：
//   以前这里有一个 kSystemGuards = {"svchost","csrss",...} 的名单来判断"是不是系统进程"。
//   两个致命问题叠在一起，让它完全失效：
//     ① tasklist 给出的进程名**带 .exe**（"svchost.exe"），名单里写的是不带的形式
//        ⇒ contains() 永远 false —— 系统保护一项都没生效；
//     ② 白名单比较前剥了 .exe、进程名没剥 ⇒ 同理，白名单也没匹配上任何人。
//   于是"除了白名单以外全杀"实际退化成了"PID>=1000 的全杀"，svchost 中招 ⇒
//   CRITICAL_PROCESS_DIED(0xEF) 蓝屏 ⇒ 重启 ⇒ 自启 ⇒ 再杀 ⇒ 反复重启。
//
//   猜名字这条路本身就不成立（系统组件/服务/驱动宿主/杀软穷举不完）。
//   正确做法是问内核这两个标志位 —— 进程自己带着"杀我会导致系统崩溃"的标记：
//     · ProcessBreakOnTermination(29)：被标记 critical，杀它内核立刻蓝屏
//     · ProcessProtectionInformation(61)：受保护进程（PPL），杀不掉也不该杀
//   再叠加两条几何判断：Session 0（服务会话）与镜像位于 %SystemRoot% 下。
//
//   ⚠️ 铁律：**打不开句柄 / 查询失败，一律按"受保护"处理**（宁可不杀，不可杀错）。
//      这类保守处理是这个功能的全部价值所在。
// ── 进程安全性判定（上面 28-45 行那段说明仍然有效）已抽到公共件 `proc_guard.{h,cpp}`：
//    exam_mode 与 main.cpp 的 process_stop 现在共用**同一道**内核闸门 —— 后者的保护此前只有
//    一份纯进程名名单（缺口见 proc_guard.h 头注释），抽出来才能堵住那条路径。
//    本文件的调用点在下方 killBlacklisted()。

/** tasklist 的 "Foo.exe" 与配置里的 "foo" 要能对上：双侧都剥 .exe 再比。 */
QString normalizeProcName(const QString &raw)
{
    QString s = raw.trimmed().toLower();
    if (s.endsWith(QLatin1String(".exe"))) s.chop(4);
    return s;
}
} // namespace

ExamMode &ExamMode::inst()
{
    static ExamMode s;
    return s;
}

ExamMode::ExamMode()
{
    m_ticker = new QTimer(this);
    m_ticker->setInterval(kTickerMs);
    connect(m_ticker, &QTimer::timeout, this, &ExamMode::updateTitle);

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(kWatchdogMs);
    connect(m_watchdog, &QTimer::timeout, this, &ExamMode::killBlacklisted);
}

bool ExamMode::start(int minutes, const QStringList &blacklist)
{
    if (m_active) stop(QStringLiteral("restart"));

    // 黑名单为空 ⇒ **一个进程都不杀**。这时考试模式仍然生效（全屏拦截 + 倒计时），
    // 只是不执行终止；原因必须如实回报，不能报"started 一切正常"。
    // （黑名单制下这条是"没配就别动手"的自然结果，不再是白名单时代那种拼运气的安全兜底）
    // ⚠️ 2026-10-13 修：m_enforcing 原来按**原始列表**是否为空来定，且位置在归一化**之前**。
    //    下发 ["", "  "] 时：m_enforcing=true，而归一化后 m_blacklist 为空 ⇒ killBlacklisted()
    //    进门就 return（零终止），但 start() 的返回值与回执都报"已启用进程终止 / 无错误" ——
    //    把"没做"报成了"做了"（正是本项目明确禁止的）。改为按**过滤后**的列表判定。
    m_blacklist.clear();
    for (const QString &raw : blacklist) {
        const QString n = normalizeProcName(raw);
        if (!n.isEmpty() && !m_blacklist.contains(n)) m_blacklist.append(n);
    }
    // 自己人永远不能被自己的黑名单杀掉 —— 这是防"老师把自己端掉"的一道保险
    m_blacklist.removeAll(QStringLiteral("stelarith-agent-qt"));
    m_blacklist.removeAll(QStringLiteral("taskkill"));
    m_blacklist.removeAll(QStringLiteral("tasklist"));

    m_enforcing = !m_blacklist.isEmpty();
    m_reason.clear();
    if (!m_enforcing) {
        m_reason = blacklist.isEmpty()
            ? QStringLiteral("黑名单为空：只做全屏拦截与倒计时，不终止任何进程")
            : QStringLiteral("黑名单里没有有效进程名（下发 %1 项全被过滤）：只做全屏拦截与倒计时")
                  .arg(blacklist.size());
        qInfo("[exam] 考试模式已启动但未启用进程终止（%s）—— 只做全屏拦截", qPrintable(m_reason));
    }
    m_durationSec = minutes > 0 ? minutes * 60 : 0; // 0 = 不自动结束（管理员手动结束）
    createWindow(minutes);
    m_active = true;
    m_clock.restart();
    m_ticker->start();
    m_watchdog->start();
    notifyExamMode(true, m_reason);   // 2026-11-01：考试开始提示（关掉「考试」提供方后不弹）
    emit stateChanged(true, remainingSec(), false);
    qInfo("[exam] 考试模式开始：%s，黑名单 %d 条%s",
          m_durationSec ? qPrintable(QStringLiteral("%1 分钟").arg(minutes)) : "不自动结束",
          m_blacklist.size(),
          m_enforcing ? "" : qPrintable(QStringLiteral("（未启用进程终止：%1）").arg(m_reason)));
    return m_enforcing;
}

bool ExamMode::stop(const QString &reason)
{
    if (!m_active) return false;
    m_active = false;
    m_ticker->stop();
    m_watchdog->stop();
    destroyWindow();
    qInfo("[exam] 考试模式结束（%s）", reason.isEmpty() ? qPrintable(reason) : "手动");
    notifyExamMode(false, reason);   // 2026-11-01：考试结束提示（同上）
    emit stateChanged(false, -1, reason == QLatin1String("timeout"));
    return true;
}

int ExamMode::remainingSec() const
{
    if (!m_active || m_durationSec <= 0) return -1;
    const int left = m_durationSec - (int)(m_clock.elapsed() / 1000);
    return left > 0 ? left : 0;
}

void ExamMode::onTimeout()
{
    // 到点自动结束：学生恢复使用。
    stop(QStringLiteral("timeout"));
}

void ExamMode::createWindow(int minutes)
{
    destroyWindow();

    QWidget *w = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    w->setAttribute(Qt::WA_DeleteOnClose, false);
    w->setStyleSheet(QStringLiteral(
        "background:#0A0E1A; color:#F1F5F9;"
        "QWidget { background:#0A0E1A; }"
        "QLabel#big { font-size:42px; font-weight:600; color:#F1F5F9; }"
        "QLabel#sub { font-size:16px; color:#94A3B8; }"
        "QLabel#timer { font-size:64px; font-weight:700; color:#3B82F6; }"
        "QLabel#hint { font-size:13px; color:#64748B; margin-top:24px; }"));

    auto *big = new QLabel(QStringLiteral("考试模式"), w);
    big->setObjectName(QStringLiteral("big"));
    big->setAlignment(Qt::AlignCenter);
    auto *sub = new QLabel(minutes > 0
        ? QStringLiteral("考试进行中，请勿切换程序 · 到点自动结束")
        : QStringLiteral("考试进行中，请勿切换程序 · 由管理员结束"),
        w);
    sub->setObjectName(QStringLiteral("sub"));
    sub->setAlignment(Qt::AlignCenter);
    auto *timer = new QLabel(w);
    timer->setObjectName(QStringLiteral("timer"));
    timer->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(QStringLiteral("若需帮助请联系监考老师"), w);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setAlignment(Qt::AlignCenter);

    auto *lay = new QVBoxLayout(w);
    lay->setSpacing(8);
    lay->addStretch(2);
    lay->addWidget(big);
    lay->addWidget(sub);
    lay->addWidget(timer);
    lay->addWidget(hint);
    lay->addStretch(3);

    m_window = w;

    // 全屏铺满主屏 + 置顶
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        w->setGeometry(s->geometry());
    } else {
        w->setGeometry(0, 0, 1280, 720);
    }
    w->show();
    w->raise();
    w->activateWindow();
    // WS_EX_TOPMOST 兜底（Qt 的 WindowStaysOnTopHint 在个别环境会被降级）
    if (HWND h = (HWND)w->winId()) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    }

    updateTitle();
}

void ExamMode::destroyWindow()
{
    if (m_window) {
        m_window->close();
        m_window->deleteLater();
        m_window = nullptr;
    }
}

void ExamMode::updateTitle()
{
    if (!m_window) return;
    QLabel *timer = m_window->findChild<QLabel *>(QStringLiteral("timer"));
    if (!timer) return;
    if (m_durationSec > 0) {
        const int left = remainingSec();
        const int mm = left / 60, ss = left % 60;
        timer->setText(QStringLiteral("%1:%2").arg(mm, 2, 10, QLatin1Char('0')).arg(ss, 2, 10, QLatin1Char('0')));
        if (left <= 0) {
            // 到点：自动结束（同 onTimeout）
            QTimer::singleShot(0, this, &ExamMode::onTimeout);
        }
    } else {
        timer->setText(QStringLiteral("--:--"));
    }
}

void ExamMode::killBlacklisted()
{
    if (!m_enforcing || m_blacklist.isEmpty()) return;

    // 进程列表走**内核快照**（CreateToolhelp32Snapshot），不再 fork tasklist.exe。
    // 2026-10-13 改：原来每 2 秒 fork 一次 tasklist、每杀一个进程 fork 一次 taskkill ——
    //   60 台机器同时跑 = 每 2 秒 60 次子进程（教室机本来就弱），且 waitForFinished 会
    //   阻塞主线程最多 1 秒。整条链路换成零 fork。
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    struct Proc { QString name; qint64 pid = 0; };
    QList<Proc> procs;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            // ⚠️ 归一化必须**双侧一致**：快照给 "Foo.exe"，黑名单里写 "foo"，
            //    两边都过 normalizeProcName() 才能对上。旧版只剥了配置那一侧，
            //    于是黑名单/保护名单**一条都匹配不上**（2026-10-09 事故的直接导火索）。
            procs.append({ normalizeProcName(QString::fromWCharArray(pe.szExeFile)),
                           static_cast<qint64>(pe.th32ProcessID) });
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    auto isBlacklisted = [this](const QString &normName) {
        return m_blacklist.contains(normName);
    };

    int killed = 0, guarded = 0;
    for (const Proc &pr : procs) {
        if (pr.name.isEmpty() || pr.pid <= 0) continue;
        // ── 黑名单语义：只处理老师点名的这几个，其余**一概不碰** ──
        if (!isBlacklisted(pr.name)) continue;

        // ── 到这一步还要过三重内核闸门才敢动手 ──
        //    即使老师手滑在黑名单里填了 svchost.exe，也会被这里拦下来。
        //    一次 OpenProcess 同时要 QUERY（查闸门）+ TERMINATE（杀），省一次打开。
        HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, static_cast<DWORD>(pr.pid));
        if (!h) {
            // 打不开句柄：权限更高或是受保护/系统进程 —— 一律放过（保护优先）
            ++guarded;
            continue;
        }
        const DWORD sessionId = queryProcessSessionId(h);
        const bool untouchable = processIsUntouchable(h, sessionId);
        if (untouchable) {
            CloseHandle(h);
            ++guarded;
            qInfo("[exam] 已保护 %s (PID %lld)：内核标记为 critical/受保护或服务会话，跳过",
                  qPrintable(pr.name), pr.pid);
            continue;
        }

        // 零 fork 终止（原为 fork taskkill.exe）。
        const BOOL ok = TerminateProcess(h, 1);
        const DWORD lastErr = ok ? 0 : GetLastError();
        CloseHandle(h);
        if (ok) {
            ++killed;
            qInfo("[exam] 黑名单拦截：终止 %s (PID %lld)", qPrintable(pr.name), pr.pid);
        } else {
            ++guarded;
            qInfo("[exam] 终止失败 %s (PID %lld) 错误码 %lu（按保护处理，不重试）",
                  qPrintable(pr.name), pr.pid, lastErr);
        }
    }
    if (killed > 0) qInfo("[exam] 本轮终止 %d 个黑名单进程", killed);
    if (guarded > 0) qInfo("[exam] 本轮 %d 个命中黑名单的进程被安全阀拦下（未杀）", guarded);
}
