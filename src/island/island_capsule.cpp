#include "island_capsule.h"
#include "island.h"
#include "../domain/panel_domain.h"

#include <QLoggingCategory>
#include <QTimer>

// 岛「收起最小化」的动作层（2026-11-04 · m18334）
//
// 本文件只做两件"请求"：告诉 IslandOverlay 该不该缩、以及**什么时候**该弹回来。
// 真正的几何/动画/状态机全在 IslandOverlay（含 island_minimize.cpp）——
// 这里若自己伸手改窗口几何，就绕过了它的弹性缓动与 m_idle 守卫，迟早打架。
//
// ── 自动恢复的两个边界口径（与倒计时面同源，见 PanelDomain::msUntilBoundary）──
//   上课中  → 撑到本节下课
//   课间    → 撑到下一节上课
//   已放学/今天无课 → msUntilBoundary() 返回 0 ⇒ **不排**，保持最小化。
//   为什么不排：放学后把整条收起条突然弹回来，比一直小胶囊更打扰人；
//   老师第二天开机走的是正常进入空闲态的路径，本来就不是最小化。

IslandCapsule::IslandCapsule(QObject *parent)
    : QObject(parent)
{
}

void IslandCapsule::minimize()
{
    IslandOverlay *isl = IslandOverlay::instance();
    if (!isl) return;
    isl->setMinimized(true);

    const qint64 ms = PanelDomain::msUntilBoundary();
    if (ms <= 0) {
        qInfo("[island] 最小化：今天已没有课表边界，保持最小化（不排自动恢复）");
        return;
    }
    // 夹到 6 小时：课表档案写坏/跨天时，别排一个第二天才响的单发定时器。
    const int delay = int(qMin<qint64>(ms, 6 * 3600 * 1000));
    qInfo("[island] 最小化：%d ms 后（本时段结束）自动恢复成完整收起条", delay);
    // 以 isl 为上下文：岛若被销毁，这条回调自动作废（不会打到野指针上）。
    QTimer::singleShot(delay, isl, [isl] { isl->setMinimized(false); });
}

void IslandCapsule::restore()
{
    if (IslandOverlay *isl = IslandOverlay::instance())
        isl->setMinimized(false);
}

IslandCapsule *islandCapsuleInstance()
{
    static IslandCapsule g;
    return &g;
}
