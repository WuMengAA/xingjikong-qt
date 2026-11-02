// 远程终端（被控端侧）实现 —— 协议与约束见 terminal_console.h 顶部注释。

#include "terminal_console.h"
#include "../cloud_proto.h"      // 协议 v1 信封的唯一声明（定义在 main.cpp）
#include "../system/winlauncher.h"      // 2026-10-19 需求①：子进程静默启动（杜绝终端子进程弹 cmd 窗）

#include <QWebSocket>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QDebug>

// 信封封装走共享声明（cloud_proto.h）：别在这边另写一份，格式一变两端就对不上。

// cd 词法解析（定义在文件尾，runLine 里要先判"这行有没有 cd"）
static bool termParseCdTarget(const QString &raw, bool powershell, QString *outTarget, QString *outErr);

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
        // ⚠️ 2026-10-13：原来 qPrintable(err ? *err : QString()) —— 临时 QString 在本条
        //    语句结束即销毁，qPrintable 取到的指针悬空（Windows 上日志可能异步转发，
        //    就有概率踩到）。改流式写法，参数是值、不经临时对象的裸指针。
        qWarning().noquote() << "[term] 拒绝开新会话：" << (err ? *err : QStringLiteral("已有会话"));
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
bool TerminalConsole::input(const QString &sid, const QString &keys)
{
    if (!m_active || sid != m_sid) return false;
    m_idleTimer.start(m_idleMs);

    if (m_busy) {
        // 并发是这条链路最容易出事的口子：两个命令同时写同一个 shell，输出会互相插花。
        // 宁可回一句"还在跑"，也不排队（排队会让管理端看着像卡死）。
        QJsonObject p; p.insert(QStringLiteral("sid"), sid);
        p.insert(QStringLiteral("data"), QStringLiteral("\r\n[上一条命令还在跑，等它结束]\r\n"));
        sendFrame("terminal_data", p);
        return false;
    }

    // 先回显（含提示符），再执行 —— 与管理端"输入即可见"的习惯一致
    const QString line = keys;
    QJsonObject p; p.insert(QStringLiteral("sid"), sid);
    p.insert(QStringLiteral("data"), termPrompt(m_cwd) + line + QStringLiteral("\r\n"));
    sendFrame("terminal_data", p);

    runLine(line);
    return true;
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

    // 记下这行 + 有没有 cd（命令跑通后由 applyCwdChange 换目录 —— 见文件尾注释）
    m_lastLine = trimmed;
    QString cdTarget;
    m_lastLineHadCd = termParseCdTarget(trimmed,
        m_shell.compare(QStringLiteral("powershell"), Qt::CaseInsensitive) == 0, &cdTarget, nullptr);

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
    // #368 需求①：终端子进程（cmd/powershell）I/O 已走管道接 Qt，禁止再分配可见控制台窗
    stelarithHideChildConsole(*m_proc);
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
    if (code == 0 && m_lastLineHadCd) applyCwdChange();   // 跑通了才换目录（cd 失败整体码非 0）

    m_proc->deleteLater();
    m_proc = nullptr;
    m_idleTimer.start(m_idleMs);
}

// ── cd 持久化（2026-10-13 登记项重写）─────────────────────────────────────
// 根因：每条命令都是 cmd.exe /c <line> 的**全新进程**，进程里的 cd 改的是它自己的
// 当前目录，进程一退就没了；原 refreshCwd 再开一个新进程去问 `cd`，问回来的永远是
// setWorkingDirectory(m_cwd) 设进去的旧值 —— 所以提示符恒不更新。
// 修法：不再问子进程，改成 agent 侧**词法解析**这行命令里有没有 cd，有就在本地把
// m_cwd 解析出来（下一条命令 setWorkingDirectory 用新值，等于目录在会话里持久了）。
//   cmd:  cd[/d] [X]  / chdir[/d] [X] / cd..（cmd 特有的无空格写法） / X:（切盘符）
//   ps:   cd / Set-Location / sl / chdir [X]（含 -Path X）
// 解析不了（网络路径重定向、pushd、多命令里的 cd 失败等）就不动 m_cwd ——
// 宁可提示符旧一点，也不能指错。同时不再起子进程查询，原先那 3 秒主线程阻塞一并消灭。
static bool termParseCdTarget(const QString &raw, bool powershell, QString *outTarget, QString *outErr)
{
    const QString line = raw.trimmed();
    if (line.isEmpty()) return false;

    // 只看第一个命令段：被 && / & / | 隔开的第一段里找 cd（`cd x && dir` 是常见写法）
    QString head = line;
    static const QRegularExpression segSplit(QStringLiteral("[&|]"));
    const QStringList segs = line.split(segSplit);
    if (!segs.isEmpty()) head = segs.first().trimmed();
    if (head.isEmpty()) return false;

    // cmd 特有：`cd..`、`cd\`（无空格也能跑）
    if (!powershell) {
        if (head.compare(QStringLiteral("cd.."), Qt::CaseInsensitive) == 0) {
            *outTarget = QStringLiteral(".."); return true;
        }
        if (head.compare(QStringLiteral("cd\\"), Qt::CaseInsensitive) == 0) {
            *outTarget = QStringLiteral("\\"); return true;
        }
    }

    static const QRegularExpression cdCmd(
        QStringLiteral("^(?:cd|chdir|set-location|sl)(?:\\s+(?:-path|-literalpath))?\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = cdCmd.match(head);
    if (!m.hasMatch()) {
        // 裸 `cd`（cmd 里是"打印当前目录"，ps 里是"回 home"）不是"换目录"
        return false;
    }
    QString arg = m.captured(1).trimmed();
    arg.remove(QLatin1Char('"'));
    if (arg.isEmpty()) return false;
    // cmd 的 `cd /d X`：参数段可能整个是 "/d X" —— 把开头的 /d 标志摘掉
    if (!powershell && arg.startsWith(QStringLiteral("/d "), Qt::CaseInsensitive))
        arg = arg.mid(3).trimmed();
    if (arg.isEmpty()) return false;
    *outTarget = arg;
    return true;
}

void TerminalConsole::applyCwdChange()
{
    QString target, why;
    if (!termParseCdTarget(m_lastLine, m_shell.compare(QStringLiteral("powershell"), Qt::CaseInsensitive) == 0,
                           &target, &why)) {
        return;   // 没有 cd / 解析不了 → 目录不变（不是错误）
    }

    // 相对路径 → 基于 m_cwd 解析；`..`/`.` 由 cleanPath 归一
    QString candidate;
    const QFileInfo fi(target);
    if (fi.isAbsolute()) candidate = QDir::cleanPath(target);
    else if (QRegularExpression(QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z]:"))).match(target).hasMatch())
        candidate = QDir::cleanPath(target);   // `C:` 或 `C:xx` 这类盘符形态
    else
        candidate = QDir::cleanPath(m_cwd + QLatin1Char('/') + target);

    // 目标不存在/不是目录 → 不换（cd 打不开时 cmd 退出码非 0，正常到不了这；双保险）
    if (!QDir(candidate).exists()) {
        qInfo("[term] cd 目标不存在，目录不变：%s", qPrintable(candidate));
        return;
    }
    if (candidate != m_cwd) {
        m_cwd = candidate;
        qInfo("[term] 提示符随目录更新：%s", qPrintable(m_cwd));
        QJsonObject p; p.insert(QStringLiteral("sid"), m_sid);
        p.insert(QStringLiteral("cwd"), m_cwd);
        p.insert(QStringLiteral("prompt"), termPrompt(m_cwd));
        sendFrame("terminal_data", p);   // 把新提示符推给管理端（单独一帧，无输出混排）
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
