// 远控会话生命周期管理（2026-10-30 main 拆分 #8）。
// 从 main.cpp（原 5791-5837 收口/看门狗 + 1649-1667 访问密码）迁出，函数/注释逐字保留。
//
// 为什么单列一簇：remote_control_stop 动作与泄漏看门狗共用同一段清理（置位、解冻
// 灵动岛、停桥、停服、恢复帧率）。抽成函数是因为以前"停会话"只此一条路径（管理端
// 自觉发 stop），而管理端直接离开（关窗/切页/进程崩/网络分区）时没有任何人调它 ⇒
// g_remoteControl 永真、灵动岛长期冻结、VNC server 空转烧 CPU（2026-10-19 实证：
// 泄漏近 30 分钟，只能重启 agent 收口）。幂等：无会话时早退。
//
// 状态全局仍驻 main（文件作用域外部链接），本模块按既有 extern 模式复用。

#include "remote_control.h"

#include "../island.h"       // IslandOverlay（setSuppressed / instance()->ensureIdle）
#include "../vnc_bridge.h"   // VncBridge
#ifdef STE_USE_LIBVNCSERVER
#include "../vnc_server.h"   // VncServer（方案 A，编译开关同 main）
#endif
#include <QDateTime>
#include <QFile>

// ── 复用 main 的状态全局（提升文件作用域外部链接，同 g_uid / agentEnvPath 模式）──
extern bool g_remoteControl;
extern int g_currentFps;
extern qint64 g_vncSessionStartMs;
extern qint64 g_lastVncDownMs;

// ── 远控会话收口（2026-10-19 修复③）────────────────────────────────────────
// remote_control_stop 动作与泄漏看门狗共用同一段清理：置位、解冻灵动岛、
// 停桥、停服、恢复帧率。抽成函数是因为以前"停会话"只此一条路径（管理端
// 自觉发 stop），而管理端直接离开（关窗/切页/进程崩/网络分区）时没有任何
// 人调它 ⇒ g_remoteControl 永真、灵动岛长期冻结、VNC server 空转烧 CPU
// （2026-10-19 实证：泄漏近 30 分钟，只能重启 agent 收口）。
// 幂等：无会话时早退（stop 动作据此仍要返回 failed，见调用方）。
void stopRemoteControlSession(const QString &reason)
{
    if (!g_remoteControl) return;
    const int was = g_currentFps;
    g_remoteControl = false;
    // 2026-10-21（审查 #5）：setSuppressed(false) 只清标志不恢复岛 —— 远控开始时
    //   hide() 掉了岛，结束不补 ensureIdle() 的话顶部空闲半胶囊就丢了（要等下一次
    //   通知才回来）。ensureIdle 内部受 m_idleEnabled 约束 + isVisible 判据（岛上
    //   正显示内容时不会把内容顶成空闲）。
    IslandOverlay::setSuppressed(false);   // 2026-10-19：解除岛冻结（远控结束）
    IslandOverlay::instance()->ensureIdle();
#ifdef STE_USE_LIBVNCSERVER
    // 统一方案 A：停整条桥再销毁 in-agent VNC Server（RFB 源）。
    VncBridge::instance().stop();
    VncServer::instance().stop();
#endif
    g_currentFps = 5;   // 恢复默认
    qInfo("[agent-qt] 🟢 远控会话关闭（%s），抓屏帧率恢复 5 fps（原 %d）",
          qPrintable(reason), was);
}

// ── 远控会话泄漏看门狗（2026-10-19 修复③）──────────────────────────────────
// 判据：管理端 VncClient 每 5s 经 RFB 发一个 1x1 增量 FramebufferUpdateRequest
//   （无害探活），字节经 vnc-data down 到达本端即刷新 g_lastVncDownMs；
//   老师纯观看不碰键鼠时也有这个流量，所以"在看"与"已离开"可区分。
// 口径：会话起步 30s 内不判（留给 DXGI 首帧 2s 黑帧预算 + RFB 握手 +
//   首个保活）；之后超过 25s（保活周期 5s 的 5 倍）没收过任何 down 字节
//   ⇒ 判管理端已离开，自动收口。
// 误杀代价可控（老师重新选中设备即重建会话）；漏杀代价是灵动岛永久冻结
//   + VNC server 空转，不可接受。搭心跳车执行，零新定时器。
void reapRemoteControlIfStale()
{
    if (!g_remoteControl) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (g_vncSessionStartMs > 0 && now - g_vncSessionStartMs < 30000) return;   // 起步宽限
    if (g_lastVncDownMs > 0 && now - g_lastVncDownMs <= 25000) return;          // 保活正常
    qWarning("[agent-qt] [vnc] ⚠️ 远控会话管理端保活超时（down %s）—— 判定已离开，自动收口",
             g_lastVncDownMs > 0 ? "超过 25s 未到" : "从未收到（会话起步 30s 后仍无）");
    stopRemoteControlSession(QStringLiteral("泄漏看门狗"));
}

// ── 被控端访问密码本地持久化（2026-10-26）──────────────────────────────────
// 云端统一设置/修改，明文经已鉴权设备 WS 下发到本机。真正的访问闸门在云端
// 中继处；本机落盘仅为"被控端持有自己的访问密码"与未来本地加强。
static QString vncAccessPath()
{
    return qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong/vnc_access.txt");
}
/** 落盘访问密码。enabled=false → 清空文件（等于关闭）；enabled=true → 写明文（agent 本机即被控端本体，
 *  且云端已在校验通过后下发，这里不再二次散列，避免与云端 verifier 口径耦合）。 */
void saveVncAccessLocally(bool enabled, const QString &password)
{
    QFile f(vncAccessPath());
    if (!enabled) {
        if (f.exists() && !f.remove())
            qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法清除访问密码文件：%1").arg(f.errorString());
        else
            qInfo("[agent-qt] 已清除本地访问密码");
        return;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning().noquote() << QStringLiteral("[agent-qt] ⚠️ 无法保存访问密码：%1").arg(f.errorString());
        return;
    }
    f.write(QStringLiteral("enabled=1\npassword=%1\n").arg(password).toUtf8());
    f.close();
    qInfo("[agent-qt] 已保存本地访问密码（enabled=1）");
}
