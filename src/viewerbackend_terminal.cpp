// 远程终端（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）
//
// // 搬出理由：终端只有开/输/关三个动作 + 一个状态机，与远控并列但互不依赖。

#include "viewerbackend.h"
#include "viewerbackend_shared.h"
#include <QJsonValue>
#include <QTimer>
#include <QJsonObject>

// ──────────────────────────────────────────────────────────────────────
// 远程终端
// ──────────────────────────────────────────────────────────────────────
void ViewerBackend::setTermState(TermState s, const QString &sid, const QString &note)
{
    if (m_termState == s && m_termSid == sid && m_termNote == note) return;
    m_termState = s;
    m_termSid = sid;
    m_termNote = note;
    emit termStateChanged();
}
void ViewerBackend::sendTermAction(const QString &action, const QJsonObject &params)
{
    if (m_currentUid.isEmpty()) {
        setStatus(QStringLiteral("还没选设备，终端动作没发（不报错就是骗人）"), true);
        return;
    }
    QJsonObject p;
    p.insert(QStringLiteral("uid"), m_currentUid);
    p.insert(QStringLiteral("action"), action);
    if (!params.isEmpty()) p.insert(QStringLiteral("params"), params);
    sendEnvelope(QStringLiteral("instruction"), p);
    logf("[viewer] → %s 终端动作 %s", m_currentUid.toUtf8().constData(), action.toUtf8().constData());
}
void ViewerBackend::termOpen(const QString &shell)
{
    const QString sh = shell.trimmed().toLower();
    // 只放行 cmd / powershell：和被控端一致（那边也只认这两个），别让"开了个空壳终端"
    if (sh != QStringLiteral("cmd") && sh != QStringLiteral("powershell")) {
        setStatus(QStringLiteral("不支持的 shell：%1（只认 cmd / powershell）").arg(shell), true);
        return;
    }
    if (m_termState != TermIdle) {
        setStatus(QStringLiteral("这台已经有终端会话了，先关掉再开"), true);
        return;
    }
    const QString sid = QStringLiteral("v%1").arg(++m_termSidSeq);
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("shell"), sh);
    params.insert(QStringLiteral("cols"), 120);
    params.insert(QStringLiteral("rows"), 40);
    // 空闲 10 分钟没人理就自己关：这条链路上没人看着的会话开着等于给个后门
    params.insert(QStringLiteral("idleMs"), 600000);
    params.insert(QStringLiteral("cmdMs"), 30000);
    setTermState(TermPending, sid, QStringLiteral("等本机点头…"));
    sendTermAction(QStringLiteral("terminal_open"), params);
    // 收口：本机可能一直不点头（弹窗没人看），到点上面那条定时器会把状态放回 Idle
    m_termWait->start();
}
void ViewerBackend::termInput(const QString &sid, const QString &keys)
{
    if (m_termState != TermOpen || sid != m_termSid) {
        setStatus(QStringLiteral("终端会话不在，命令没发（先开终端）"), true);
        return;
    }
    // 不排队：REPL 是按"一条跑完再来下一条"的口子做的，排队的命令会错序
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    params.insert(QStringLiteral("keys"), keys);
    sendTermAction(QStringLiteral("terminal_input"), params);
}
void ViewerBackend::termClose()
{
    if (m_termState == TermIdle) return;
    // 去重：「等本机点头」和「关断请求已发、在等回话」两边状态都是 Pending（前一种也要能取消），
    // 光看状态分不出来。不加这一道，连点按钮 / 弹窗 esc 关（onClosed→doClose）会重复下发
    // terminal_close —— 界面上那颗按钮已经禁用了，这里再兜一道，别只靠 QML 那一层。
    if (m_termClosing) return;
    m_termClosing = true;
    const QString sid = m_termSid;
    QJsonObject params;
    params.insert(QStringLiteral("sid"), sid);
    sendTermAction(QStringLiteral("terminal_close"), params);
    // 本地先收摊：等被控端回 terminal_closed 可能要几百毫秒，
    // 这期间输入框留着会让人以为还能敲（敲了也是发给一个已经关掉的会话）
    setTermState(TermPending, sid, QStringLiteral("正在关…"));
    // 收口：被控端不回 terminal_closed（会话早死了/网络断了）时，2.5 秒后按已关处理，
    // 否则状态永远停在 Pending ⇒ 这台再也开不了新终端（#95 的"关不掉"那一半）。
    m_termCloseWait->start();
}
