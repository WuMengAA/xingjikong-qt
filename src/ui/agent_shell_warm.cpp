// 控制面板：首开埋点 + 启动期预热（2026-11-03 · 用户工作笔记序号 3「启动慢」）。
//
// ══ 问题是什么（实测，不是推断）══════════════════════════════════════════
// 用户报「启动慢」。本文件的两段埋点把"慢"精确切到一段上：
//
//   | 阶段                        | 墙钟（进程 spawn = 0） |
//   |-----------------------------|------------------------|
//   | 托盘显形 tray-show          | 113 ms                 |
//   | 灵动岛落定 island-idle      | 667 ms                 |
//   | **点开面板 panel-open**     | **本身 1660 ms**       |
//
// 也就是说：**启动主链根本不慢**（托盘点一下 113ms 就在那儿了）。
// 用户感知的"慢"发生在"点了托盘 → 面板出来"这条**另一条**链上：
// AgentShell 是懒建的（不点不加载），那一下要现做 QQuickWidget 构造 +
// 整套 AgentShell.qml 首解析（实测构造+解析 1247ms / 显示 413ms）。
//
// ══ 怎么解（预热，而不是"优化 QML"）══════════════════════════════════════
// 那 1.2 秒的构造+解析是**一次性固定成本**，优化 QML 只能摊薄、摊不掉。
// 真正的解法是**挑一个用户不在等的时间点把它做掉**：启动完成后桌面是闲的。
// 预热把它挪过去，用户点的时候只剩 show —— 实测降到 224ms。
//
// ══ 三条设计约束（每条都有踩过的理由）══════════════════════════════════════
// ① **不能挪进启动主链**：构造 QQuickWidget + 首解析整套 QML 会跟灵动岛的
//    空闲态首帧抢同一帧（岛自己的 ensureIdle 实测 601ms），一起做等于把岛的
//    显形往后推 —— 那是把一个"用户没在等"的成本换成"用户正在等"的成本。
// ② **延迟 1.5s，不是启动末尾立刻做**：岛 667ms 落定，1.5s 留了一倍余量；
//    首版写成 3s，把"用户 1.5~3s 之间点面板"这段最可能发生的窗口白白让了出去。
// ③ **必须先 show 再 hide**：QQuickWidget 的 QML 直到**可见**才真正完成首帧
//    合成与 item materialization。只 setSource 不显示，构造耗时会缩水，
//    但首开时仍要把剩下的补上 —— 等于白预热（这一点反直觉，实测确认）。
//    为了不闪，show 之前把 windowOpacity 打 0，hide 之后立刻还原。
//
// ══ 代价与边界（如实记账）══════════════════════════════════════════════════
// · 常驻内存多一个 QQuickWidget 与整套面板 QML 场景（30~40MB 量级）；
//   极端省内存 / 老机器可以 `STE_QT_PANEL_PREWARM=0` 关掉，关掉即退回原行为。
// · 预热失败不影响任何功能：它只是"提前做过"，出错时 openPanel 照旧现场建。
// · 无托盘环境（Session 0）也会预热 —— 那里没人会点面板，但代价只是一个
//   隐藏窗口，且这条路径本来就有 env 开关；不为此加特判，免得两条路径行为分叉。
//
// ══ 为什么实现在这个 .cpp 而不是 agent_ui.h 内联 ══════════════════════════
// agent_ui.h 是 707 行登记在 §10.5 台账里的历史包袱文件，**一行都不许再加**；
// 且按 §10.2「新代码必须进职能目录」，预热是行为不是装配，本就该有自己的 TU。
// 放在 src/ui/ 与 agent_panel.cpp / agent_shell_reload.cpp 同域。

#include "../agent_ui.h"

#include <QElapsedTimer>
#include <QTimer>

void AgentShell::openPanel()
{
    QElapsedTimer t;
    t.start();
    AgentShell *w = ensure();   // 已被预热过 ⇒ 这里几乎零成本（见 prewarmPanel）
    const qint64 tBuild = t.elapsed();
    w->m_ui.refresh();          // → refreshRequested → agentUiWire 接收端同步 fill+publish
    w->show();
    w->raise();
    w->activateWindow();
    w->setWindowState((w->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    // 判读：构造+解析应接近 0（预热已付过），剩下的全是显示/合成成本。
    // 若这一项又变回上千毫秒，说明预热没跑到（env 关了 / 定时器没触发 / 崩溃）。
    qInfo("[boot] panel-open             累计 %5lld ms（构造+解析 %lld / 显示 %lld）",
          static_cast<long long>(t.elapsed()), static_cast<long long>(tBuild),
          static_cast<long long>(t.elapsed() - tBuild));
}

void AgentShell::prewarmPanel(int delayMs)
{
    // 默认开；`STE_QT_PANEL_PREWARM=0` 关。
    // ⚠️ 按值判定而不是"设没设"：与 STE_ALLOW_MULTI 同一条教训 —— 写 0 表达"关"
    //    是人的直觉，若按"设没设"判定，写 0 反而会被当成开启。
    if (qEnvironmentVariable("STE_QT_PANEL_PREWARM") == QLatin1String("0")) return;

    QTimer::singleShot(delayMs, qApp, [] {
        QElapsedTimer t;
        t.start();
        AgentShell *w = ensure();
        const qint64 tBuild = t.elapsed();
        // ⚠️ 全透明再 show：show() 会创建原生窗口；即便同一栈里紧接着 hide()、
        //    中间没有一次事件循环（理论上画不出来），遇到会立即合成的合成器
        //    也有闪一下的可能。透明度打 0 是零成本的保险。
        w->setWindowOpacity(0.0);
        w->show();                            // ← 触发真正的首帧合成（见头注释约束③）
        w->hide();
        w->setWindowOpacity(1.0);             // 立刻还原，首次真正打开时才不透明
        qInfo("[boot] panel-prewarm          构造+解析 %lld ms（已隐藏，首次点击不再等）",
              static_cast<long long>(tBuild));
    });
}
