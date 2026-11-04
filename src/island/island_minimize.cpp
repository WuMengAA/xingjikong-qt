#include "island.h"
#include "island_bridge.h"

// 灵动岛「收起最小化」状态机（2026-11-04 · m18334 用户点单）
//
// 需求原话：「添加收起最小化按钮，最小化为一个小小胶囊，默认隐藏状态维持到此时间段
// 结束为止，意思是课间或者课上，可以点击胶囊展开……但不是展开到整个页面」
//
// ── 为什么单独一个 TU ─────────────────────────────────────────────
//   island.cpp 是 2790 行、登记在门禁台账（2806）上，余量只够改几何常量；
//   而"最小化"本身是一段独立状态（进入/退出/与 m_idle 的相互作用），
//   按 §10.2「新代码进职能文件」落这里，island.cpp 只留一行 geometryForIdle 分支。
//
// ── 与 m_idle 的关系（本文件唯一需要想清楚的事）─────────────────────
//   最小化**不是**第三种形态，而是"空闲态的一个子态"：
//     · 只在 m_idle 为真时有意义（有内容时岛在展开区，收起条根本没画）；
//     · 退出最小化不改 m_idle —— 恢复后还是空闲态，只是收回完整 680 宽收起条。
//   所以：
//     · setMinimized(true)  在非空闲时**先 enterIdle()**（把内容收掉）再缩，
//       否则会出现"展开区还在、几何却被设成 112×26"的错位；
//     · setMinimized(false) 只重算几何，不动 m_idle / m_expanded。
//
// ── 幂等 ──────────────────────────────────────────────────────────
//   自动恢复的 singleShot 和用户手动点回来可能撞在一起：两者都调
//   setMinimized(false)，靠 `if (m_minimized == on) return;` 挡住重复动画。

void IslandOverlay::setMinimized(bool on)
{
    if (m_minimized == on) return;
    m_minimized = on;

    if (on) {
        // 有内容正在展开 ⇒ 先按既有路径收回空闲态（顺带停定时器、清面板）。
        if (!m_idle) enterIdle();
        if (!isVisible()) { setGeometry(geometryForIdle()); show(); raise(); }
        else animateGeometry(geometryForIdle());
    } else {
        // 恢复：仍是空闲态，只是宽回去。不 isVisible 就不动（保持既有的"没显示过"语义）。
        if (m_idle && isVisible()) animateGeometry(geometryForIdle());
        else if (m_idle) setGeometry(geometryForIdle());
    }
    if (m_bridge) m_bridge->sync();
}
