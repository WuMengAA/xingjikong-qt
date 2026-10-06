// 远程终端（被控端侧）实现 —— 协议与约束见 terminal_console.h 顶部注释。

#include "terminal_console.h"
#include "cloud_proto.h"      // 协议 v1 信封的唯一声明（定义在 main.cpp）

#include <QWebSocket>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QDebug>

// 信封封装走共享声明（cloud_proto.h）：别在这边另写一份，格式一变两端就对不上。

TerminalConsole &TerminalConsole::inst()
{
    static TerminalConsole c;
    return c;
}

TerminalConsole::TerminalConsole(QObject *parent)
    : QObject(parent)
{
    m_flush.setSingleShot(true);
    connect(&m_flush, &QTimer::timeout, this, &TerminalConsole::flushPending);

    m_cmdTimer.setSingleShot(true);
    connect(&m_cmdTimer, &QTimer::timeout, this, &TerminalConsole::onCmdTimeout);

    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &TerminalConsole::onIdle);
}

void TerminalConsole::setSocket(QWebSocket *ws) { m_ws = ws; }

bool TerminalConsole::hasSession() const { return m_active; }
bool TerminalConsole::busy() const { return m_busy; }
QString TerminalConsole::sid() const { return m_sid; }

void TerminalConsole::sendFrame(const char *type, const QJsonObject &payload)
{
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) {
        // 云端不在就**如实记下来**，不许假装推成功（否则管理端会看到一半输出凭空消失）
        qWarning("[term] 云端不在，terminal 帧发不出去（type=%s）", type);
        return;
    }
    m_ws->sendTextMessage(makeEnvelope(QString::fromLatin1(type), payload));
}

QString termPrompt(const QString &cwd)
{
    // cmd 风格的提示符，末尾留一个空格（管理端直接接在命令前）
    return cwd + QStringLiteral("> ");
}

// ── 开会话 ─────────────────────────────────────────────────────────────────
bool TerminalConsole::open(const TermReq &req, QString *err)
{
    if (m_active) {
        if (err) *err = QStringLiteral("已有终端会话开着（sid=%1），同一台机器同时只放一个").arg(m_sid);
        qWarning("[term] 拒绝开新会话：%s", qPrintable(err ? *err : QString()));
        return false;
    }
    if (req.sid.isEmpty()) {
        if (err) *err = QStringLiteral("sid 不能为空");
        return false;
    }

    m_sid = req.sid;
    m_shell = req.shell;
    m_cwd = QDir::currentPath();
    m_cmdMs = qMax(1000, req.cmdMs);
    m_idleMs = qMax(5000, req.idleMs);
    m_active = true;
    m_busy = false;

    qInfo("[term] ✅ 会话开好 sid=%s shell=%s cwd=%s（idle %d ms / 单命令 %d ms）",
          qPrintable(m_sid), qPrintable(m_shell), qPrintable(m_cwd), m_idleMs, m_cmdMs);

    QJsonObject p;
    p.insert(QStringLiteral("sid"), m_sid);
    p.insert(QStringLiteral("mode"), QStringLiteral("repl"));   // 不是 PTY，管理端别按真终端画
    p.insert(QStringLiteral("pty"), false);
    p.insert(QStringLiteral("shell"), m_shell);
    p.insert(QStringLiteral("cwd"), m_cwd);
    p.insert(QStringLiteral("prompt"), termPrompt(m_cwd));
    sendFrame("terminal_opened", p);

    m_idleTimer.start(m_idleMs);
    return true;
}

// ── 收一行输入 ─────────────────────────────────────────────────────────────
void TerminalConsole::input(const QString &sid, const QString &keys)
{
    if (!m_active || sid != m_sid) return;
    m_idleTimer.start(m_idleMs);

    if (m_busy) {
        // 并发是这条链路最容易出事的口子：两个命令同时写同一个 shell，输出会互相插花。
        // 宁可回一句"还在跑"，也不排队（排队会让管理端看着像卡死）。
        QJsonObject p; p.insert(QStringLiteral("sid"), sid);
        p.insert(QStringLiteral("data"), QStringLiteral("\r\n[上一条命令还在跑，等它结束]\r\n"));
        sendFrame("terminal_data", p);
        return;
    }

    // 先回显（含提示符），再执行 —— 与管理端"输入即可见"的习惯一致
    const QString line = keys;
    QJsonObject p; p.insert(QStringLiteral("sid"), sid);
    p.insert(QStringLiteral("data"), termPrompt(m_cwd) + line + QStringLiteral("\r\n"));
    sendFrame("terminal_data", p);

    runLine(line);
}

void TerminalConsole::resize(const QString &sid, int cols, int rows)
{
    // REPL 模式没有伪控制台，尺寸改不了。记一笔就行，别让对端以为生效了。
    if (!m_active || sid != m_sid) return;
    qInfo("[term] 收到 resize %dx%d —— REPL 模式不支持，忽略", cols, rows);
}

void TerminalConsole::close(const QString &sid, const QString &reason)
{
    // 这里有两种关闭，别混为一谈：
    //   ① 会话真开着 —— 收尾（杀进程、停计时器）再发 terminal_closed；
    //   ② 会话压根没开起过 —— 典型是 openTerminal 里本机点了「拒绝」。这时候**也必须发一帧**：
    //      管理端�已经点了"开终端"，等的是 terminal_opened；一句 terminal_closed(reason=denied)
    //      是它唯一能知道"这事黄了"的信号。之前这行因为 (!m_active || sid != m_sid) 直接 return，
    //      帧没出去 → 管理端只能干等超时，看着像链路断了。
    if (m_active && sid == m_sid) {
        stopCmdTimers();
        if (m_proc) {
            m_proc->kill();      // 会话被关：正在跑的命令必须一起死，不能留孤儿进程
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        m_active = false; m_busy = false;
    }
    QJsonObject p; p.insert(QStringLiteral("sid"), sid);
    p.insert(QStringLiteral("reason"), reason);
    sendFrame("terminal_closed", p);
    qInfo("[term] 会话关闭 sid=%s reason=%s", qPrintable(sid), qPrintable(reason));
}

void TerminalConsole::shutdown()
{
    if (m_active) close(m_sid.isEmpty() ? QStringLiteral("-") : m_sid, QStringLiteral("agent-exit"));
}

void TerminalConsole::stopCmdTimers()
{
    m_cmdTimer.stop();
    m_flush.stop();
    m_idleTimer.stop();
}

// ── 执行一行 ───────────────────────────────────────────────────────────────
void TerminalConsole::runLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) { flushPending(); return; }   // 空回车：只回显，不启进程

    m_busy = true;
    m_cmdTimer.start(m_cmdMs);

    QStringList args;
    if (m_shell.compare(QStringLiteral("powershell"), Qt::CaseInsensitive) == 0)
        args = QStringList { QStringLiteral("-NoProfile"), QStringLiteral("-Command"), trimmed };
    else
        args = QStringList { QStringLiteral("/c"), trimmed };

    const bool ps = m_shell.compare(QStringLiteral("powershell"), Qt::CaseInsensitive) == 0;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &TerminalConsole::onStdoutReady);
    connect(m_proc, &QProcess::readyReadStandardError, this, &TerminalConsole::onStderrReady);
    connect(m_proc, &QProcess::finished, this, &TerminalConsole::onProcFinished);

    m_proc->setWorkingDirectory(m_cwd);
    // ⚠️ 只能 start 一次：连着 start 两次会把第一个进程顶掉（那正是第一版的写法）
    m_proc->start(ps ? QStringLiteral("powershell.exe") : QStringLiteral("cmd.exe"), args);
    if (!m_proc->waitForStarted(5000)) {
        qWarning("[term] FAIL 起进程失败：%s", qPrintable(m_proc->errorString()));
        m_busy = false;
        QJsonObject p; p.insert(QStringLiteral("sid"), m_sid);
        p.insert(QStringLiteral("data"), QStringLiteral("\r\n[起进程失败：") + m_proc->errorString() + QStringLiteral("\r\n"));
        sendFrame("terminal_data", p);
        m_proc->deleteLater(); m_proc = nullptr;
        m_idleTimer.start(m_idleMs);
        return;
    }

    m_startAt.restart();
    qInfo("[term] ▶ %s（cwd=%s）", qPrintable(trimmed), qPrintable(m_cwd));
}

void TerminalConsole::onStdoutReady()
{
    if (!m_proc) return;
    m_pending += m_proc->readAllStandardOutput();
    if (!m_flush.isActive()) m_flush.start(50);   // 攒 50ms 发一块，别来一个字节一帧
}

void TerminalConsole::onStderrReady()
{
    if (!m_proc) return;
    m_pending += m_proc->readAllStandardError();
    if (!m_flush.isActive()) m_flush.start(50);
}

void TerminalConsole::flushPending()
{
    if (m_pending.isEmpty()) return;
    const QString s = QString::fromLocal8Bit(m_pending, m_pending.size());  // cmd 输出是本地代码页
    m_pending.clear();
    if (s.isEmpty()) return;
    QJsonObject p; p.insert(QStringLiteral("sid"), m_sid);
    p.insert(QStringLiteral("data"), s);
    sendFrame("terminal_data", p);
}

void TerminalConsole::onProcFinished(int code)
{
    m_cmdTimer.stop();
    flushPending();   // 尾巴一定发干净，不许丢最后几行

    // Qt 6.8 的 QElapsedTimer 没有 hasElapsed()，判"起过没有"用 isValid()
    const qint64 ms = m_startAt.isValid() ? m_startAt.elapsed() : 0;
    QJsonObject p; p.insert(QStringLiteral("sid"), m_sid);
    p.insert(QStringLiteral("code"), code);
    p.insert(QStringLiteral("ms"), ms);
    sendFrame("terminal_exit", p);

    m_busy = false;
    if (code == 0) refreshCwd();          // 跑通了才问当前目录（cd 成功才该改提示符）

    m_proc->deleteLater();
    m_proc = nullptr;
    m_idleTimer.start(m_idleMs);
}

void TerminalConsole::refreshCwd()
{
    // 同步问一次当前目录：比解析"cd 命令长什么样"可靠（cd /d X && dir 这类也认）
    QProcess p;
    p.start(QStringLiteral("cmd.exe"), QStringList { QStringLiteral("/c"), QStringLiteral("cd") });
    p.waitForFinished(3000);
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    if (!out.isEmpty() && out != m_cwd) {
        m_cwd = out;
        qInfo("[term] 提示符随目录更新：%s", qPrintable(m_cwd));
    }
}

void TerminalConsole::onCmdTimeout()
{
    if (!m_proc) return;
    qWarning("[term] ⏱ 单命令超时 %d ms，强杀（防死循环/大文件把会话卡死）", m_cmdMs);
    QJsonObject p; p.insert(QStringLiteral("sid"), m_sid);
    p.insert(QStringLiteral("data"), QStringLiteral("\r\n[命令超时，已终止]\r\n"));
    sendFrame("terminal_data", p);
    m_proc->kill();
}

void TerminalConsole::onIdle()
{
    // 没人管就自己关：教室机不是服务器，挂着一个空会话只会成为别人随时进来的门
    qInfo("[term] 会话空转 %d ms 无输入，自动关闭", m_idleMs);
    close(m_sid, QStringLiteral("idle"));
}
