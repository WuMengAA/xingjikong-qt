#pragma once
#include <QObject>

// 灵动岛 ·「收起最小化」（2026-11-04 · m18334 用户点单）
//
// 用户原话：「添加收起最小化按钮，最小化为一个小小胶囊，默认隐藏状态维持到
// 此时间段结束为止，意思是课间或者课上，可以点击胶囊展开……但不是展开到整个页面」
//
// ── 三句话拆成三条硬约束 ────────────────────────────────────────────
//   ① 有按钮：完整收起条右端一枚「收起最小化」，点了就缩。
//   ② 缩成小胶囊：几何从 680×38 收到 112×28（island.cpp geometryForIdle 的
//      m_minimized 分支），**不是隐藏**——老师还得能看见它还活着、还能点回来。
//   ③ 撑到本时段结束：正在上课→撑到下课；课间→撑到下一节上课（PanelDomain::
//      msUntilBoundary，与倒计时面同一个边界口径，绝不自己再算一套）。
//      已无课（放学/周末）→ 不排自动恢复，保持最小化，避免"到点突然整条弹回来"。
//
// ── 为什么做成独立 QObject 而不是往 IslandBridge 加属性 ──────────────
//   island_bridge.h 已 318 行、登记在门禁台账上**零余量**；而 QML 侧只需要两个
//   动作（缩 / 恢复），不需要可观察状态 —— 动作走 Q_INVOKABLE，状态留在 C++。
//   与 island_panel_source.cpp 同一种接法（context property + 自由函数取单例）。
//
// ── 为什么 QML 用 typeof 守卫 ───────────────────────────────────────
//   InteractDialog / 预览等复用岛面 QML 的宿主**没有**这个注入；模块未注册时
//   QML 里直接引用未定义标识符是**硬错误**（整面空白且不报业务错，2026-11-02 踩过）。
class IslandCapsule : public QObject
{
    Q_OBJECT
public:
    explicit IslandCapsule(QObject *parent = nullptr);

    // 缩成小胶囊，并按「本时段结束时刻」排一次自动恢复。
    Q_INVOKABLE void minimize();
    // 从胶囊恢复到完整收起条（**不**展开成面板 —— m18334 最后一句）。
    Q_INVOKABLE void restore();
};

// 进程级单例（与 islandPanelSourceInstance() 同口径：岛壳只注一次）。
IslandCapsule *islandCapsuleInstance();
