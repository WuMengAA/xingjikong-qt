// 考试模式实现（见 exam_mode.h 头注释）。
#include "exam_mode.h"

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

// Windows 原生：把窗口置顶（WS_EX_TOPMOST）+ 抢占 Alt+Tab 等（WS_EX_TOOLWINDOW 隐藏任务栏图标）。
// 不拦截 Ctrl+Alt+Del（Windows 安全键，无法完全拦截 —— 文档 3.7 诚实边界）。
// 第一版不装低级键盘钩子：全屏无边框 + 置顶 + 无焦点逃逸路径，对学生已足够"防君子"。

namespace {
// 白名单轮询间隔（文档 3.7：每 2 秒轮询进程列表）
constexpr int kWatchdogMs = 2000;
// 倒计时刷新间隔（1 秒）
constexpr int kTickerMs = 1000;
}

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
    connect(m_watchdog, &QTimer::timeout, this, &ExamMode::killNonWhitelisted);
}

void ExamMode::start(int minutes, const QStringList &whitelist)
{
    if (m_active) stop(QStringLiteral("restart"));
    m_whitelist = whitelist;
    for (QString &w : m_whitelist) w = w.trimmed().toLower();
    m_durationSec = minutes > 0 ? minutes * 60 : 0; // 0 = 不自动结束（管理员手动结束）
    createWindow(minutes);
    m_active = true;
    m_clock.restart();
    m_ticker->start();
    m_watchdog->start();
    emit stateChanged(true, remainingSec(), false);
    qInfo("[exam] 考试模式开始：%s，白名单 %d 条",
          m_durationSec ? qPrintable(QStringLiteral("%1 分钟").arg(minutes)) : "不自动结束",
          m_whitelist.size());
}

bool ExamMode::stop(const QString &reason)
{
    if (!m_active) return false;
    m_active = false;
    m_ticker->stop();
    m_watchdog->stop();
    destroyWindow();
    qInfo("[exam] 考试模式结束（%s）", reason.isEmpty() ? qPrintable(reason) : "手动");
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

void ExamMode::killNonWhitelisted()
{
    // tasklist 快照（/NH 去掉表头，/FO CSV 便于解析）。
    // 每次子进程调用约几十 ms，2 秒一次可接受；不比"轮询 1 秒一次"重。
    QProcess p;
    p.start(QStringLiteral("tasklist"), { QStringLiteral("/FO"), QStringLiteral("CSV"),
                                           QStringLiteral("/NH") });
    if (!p.waitForFinished(1500)) return;
    const QByteArray out = p.readAllStandardOutput();
    // CSV 每行：`"Image Name","PID","Session Name","Session#","Mem Usage"` —— 取第 1、2 列。
    const QList<QByteArray> lines = out.split('\n');
    struct Proc { QString name; qint64 pid = 0; };
    QList<Proc> procs;
    for (const QByteArray &raw : lines) {
        QByteArray line = raw.trimmed();
        if (line.isEmpty() || !line.startsWith('"')) continue;
        const QList<QByteArray> cols = line.split(',');
        if (cols.size() < 2) continue;
        const QString name = QString::fromUtf8(cols[0]).remove('"').trimmed().toLower();
        const QString pidStr = QString::fromUtf8(cols[1]).remove('"').trimmed();
        bool ok = false;
        const qint64 pid = pidStr.toLongLong(&ok);
        if (ok) procs.append({ name, pid });
    }

    // 白名单匹配（含扩展名剥离：tasklist 的 exe 名不带 .exe）
    QStringList allowed = m_whitelist;
    for (QString &a : allowed) {
        if (a.endsWith(QStringLiteral(".exe"))) a.chop(4);
    }
    auto isAllowed = [&allowed](const QString &name) {
        for (const QString &a : allowed) if (a == name) return true;
        return false;
    };

    // 进程终止：白名单进程如 tasklist 本身、系统关键进程等必须跳过，
    // 只杀"非白名单且有窗口/非系统"的 —— 第一版保守：杀所有非白名单的 exe，
    // 但保留白名单 + 系统关键进程（svchost/csrss/winlogon 等由 PID<1000 保护）。
    const QStringList kSystemGuards = {
        QStringLiteral("svchost"), QStringLiteral("csrss"), QStringLiteral("winlogon"),
        QStringLiteral("lsass"), QStringLiteral("services"), QStringLiteral("system"),
        QStringLiteral("smss"), QStringLiteral("dwm"), QStringLiteral("wininit"),
        QStringLiteral("explorer"), QStringLiteral("tasklist"), QStringLiteral("cmd"),
        QStringLiteral("conhost"), QStringLiteral("fontdrvhost"), QStringLiteral("sihost"),
        QStringLiteral("runtimebroker"), QStringLiteral("searchapp"), QStringLiteral("startmenuexperiencehost"),
    };
    int killed = 0;
    for (const Proc &pr : procs) {
        if (pr.name.isEmpty() || pr.pid < 0) continue;
        if (isAllowed(pr.name)) continue;
        if (kSystemGuards.contains(pr.name)) continue;
        if (pr.pid < 1000) continue; // 系统进程保护
        // 结束可疑进程（不递归子进程 —— 第一版保守，避免误杀）
        QProcess k;
        k.start(QStringLiteral("taskkill"), { QStringLiteral("/PID"),
                                              QString::number(pr.pid),
                                              QStringLiteral("/F") });
        k.waitForFinished(1000);
        ++killed;
        qInfo("[exam] 白名单拦截：终止 %s (PID %lld)", qPrintable(pr.name), pr.pid);
    }
    if (killed > 0) qInfo("[exam] 本轮终止 %d 个非白名单进程", killed);
}
