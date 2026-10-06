#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 远程终端（2026-10-06 加）—— REPL 式_cmd 终端，不走 ConPTY。
//
// 为什么是 REPL 而不是真 PTY：
//   这台机器 Windows 11 26200 的 System32 里**没有 conpty.dll**，conhost.exe 也不导出
//   CreatePseudoConsole —— 真伪控制台在这台开发机上没法端到端验证。盲写一个自己没跑过
//   PTY 路径，等于把"能跑"这件事赌给别人看。所以先做能跑、能验、能审计的这一版：
//   管理端一个输入框，回车提交一行 → 被控端执行 → 流式回显 → 结束。
//   代价：不支持 Tab 补全 / 交互程序（python REPL、ftp 之类）；
//         好处：每条命令都是一条完整审计、超时可强杀、断线即终止。
//
// 协议（对端一律经云端透传，见云端 registry.js 的转发分支）：
//   入：instruction terminal_open   {sid, shell, cols, rows, idleMs, cmdMs}
//        instruction terminal_input {sid, keys}
//        instruction terminal_resize{sid, cols, rows}
//        instruction terminal_close {sid}
//   出：terminal_opened {sid, mode:"repl", shell, prompt, cwd}
//       terminal_data   {sid, data}      ← 流式（攒 50ms 一块发）
//       terminal_exit   {sid, code, ms}
//       terminal_closed {sid, reason}    ← denied/idle/remote/error
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>   // 成员里是真定时器，必须完整类型（只前向声明会 C2079）

class QWebSocket;
class QProcess;
class QTimer;

struct TermReq {
    QString sid;
    QString shell = QStringLiteral("cmd");   // cmd | powershell
    int cols = 100;
    int rows = 30;
    int idleMs = 60000;                      // 会话无人理会自动关（教室机别白开）
    int cmdMs = 30000;                       // 单条命令超时强杀（防 cat 大文件/死循环卡住）
};

class TerminalConsole : public QObject
{
    Q_OBJECT
public:
    static TerminalConsole &inst();

    /// 设置出帧通道（云端连上的那个 ws）。空指针 = 发不出，会如实记 error 不假装成功。
    void setSocket(QWebSocket *ws);

    bool open(const TermReq &req, QString *err = nullptr);
    void input(const QString &sid, const QString &keys);
    void resize(const QString &sid, int cols, int rows);   // REPL 模式仅记日志（无 PTY 可调尺寸）
    void close(const QString &sid, const QString &reason);

    bool hasSession() const;
    bool busy() const;        // 有命令在跑 → 新输入直接拒绝，不许并发
    QString sid() const;

    void shutdown();          // 进程退出前：关会话 + 杀进程

private slots:
    void onStdoutReady();
    void onStderrReady();
    void onProcFinished(int code);
    void onCmdTimeout();
    void onIdle();
    void flushPending();

private:
    explicit TerminalConsole(QObject *parent = nullptr);
    ~TerminalConsole() = default;
    TerminalConsole(const TerminalConsole &) = delete;
    TerminalConsole &operator=(const TerminalConsole &) = delete;

    void sendFrame(const char *type, const QJsonObject &payload);
    void refreshCwd();                       // 命令跑完问一次当前目录，更新提示符
    void runLine(const QString &line);
    void stopCmdTimers();

    // ── 状态 ──（全部值成员，别用裸指针自制生命周期）
    QWebSocket *m_ws = nullptr;
    QString m_sid;
    QString m_shell = QStringLiteral("cmd");
    QString m_cwd = QStringLiteral("C:\\");
    bool m_active = false;                   // 会话开着
    bool m_busy = false;                     // 有命令在跑
    QProcess *m_proc = nullptr;
    QByteArray m_pending;                    // 攒着的输出，满 50ms 发一块
    QElapsedTimer m_startAt;                 // 单条命令耗时（回 terminal_exit 用）
    int m_cmdMs = 30000;
    int m_idleMs = 60000;
    QTimer m_flush;                          // 输出节流
    QTimer m_cmdTimer;                       // 单条命令超时
    QTimer m_idleTimer;                      // 会话空转
};
