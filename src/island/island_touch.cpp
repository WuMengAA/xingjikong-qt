// 灵动岛「按住不收」域（2026-11-02）
//
// 用户原话：「灵动岛自动收起时机不对，有触摸就等到无触摸才收起。可以优化」
//
// 为什么单独一个 TU，而不是写在 island.cpp 里：
//   island.cpp 已在行数白名单（只降不升）。功能是真的，行数也是真的 ——
//   正确做法是把它放进自己的域，而不是给白名单开一次口子（开一次就有第二次）。
//
// 为什么用**一个布尔**而不是"stop 计时器 + 记住剩余量"：
//   剩余量方案在"按下 100ms 就抬起"时会把剩下 5.9s 原样续上 —— 观感仍是
//   「手一松就闪掉」，那不算"等无触摸"，只是"迟早闪"。必须是松手后重新起表。
//
// 三层闸，缺一条都会出现「岛再也收不回去」：
//   ① QML onPressed/onReleased/onCanceled 主动上报（QQuickWidget 吃掉了 C++ 侧事件）；
//   ② 本窗口 mousePress/Release 也调（非 QML 覆盖区的空白地带仍走原生事件）；
//   ③ m_touchGuard 超时兜底（released 真丢了：指针在窗外抬起 / 窗口失焦 / QML 重载）。
#include "island.h"

#include <QDebug>
#include <QMouseEvent>
#include <QTimer>
#include <QtMath>

// 触控容差（像素）：手指按下到抬起之间会有肉眼看不见的抖动，小于这个位移一律
// 算点击不算拖拽；超过它就只当手势、不当点击。随指针域一起从 island.cpp 搬来。
static const int kDragSlop = 24;
// 松开后重新起表的时长。取 5s 与展开态硬回收同档：用户刚松手，还要给"再看一眼"的余地。
static const int kTouchIdleMs = 5000;
// ⚠️ 与 island.cpp 的 kIdleExpandMs 同值（那里是文件级 static，跨 TU 看不见）。
//  两个数语义不同（一个管展开态回收、一个管松手后起表），只是当前恰好同档；
//  要改请分别判断，别顺手把两个一起调。
static const int kIdleExpandMs = 5000;
// 按住态兜底超时。正常"按住看详情"远到不了 3 分钟，但比"永远卡住"安全得多。
static const int kTouchMaxMs = 180000;

void IslandOverlay::initTouchGuard()
{
    m_touchGuard = new QTimer(this);
    m_touchGuard->setSingleShot(true);
    connect(m_touchGuard, &QTimer::timeout, this, [this]() {
        qWarning("[island] ⚠ 按住态超时未收到松手 —— 强制恢复（曾卡在 m_touching）");
        setTouching(false);
    });
}

void IslandOverlay::setTouching(bool on)
{
    if (m_touching == on) return;
    m_touching = on;
    if (!on) {                       // 松手＝一次真实交互，重新起表
        m_touchGuard->stop();
        // ⚠️ 这里**不能**只调 noteActivity()：它重排 m_autoClose 的前提是
        //    `m_autoClose->isActive()`，而按住时恰恰把它 stop() 了 ⇒ 条件不成立 ⇒
        //    表再也不起、岛永不收回。2026-11-02 由 --touch 探针实测抓到（松手 +5.6s
        //    仍在上屏）。松手后必须**无条件**重起（只要当时确实挂着一则有显式时长的通知）。
        if (m_durationMs > 0 && !isIdle())
            m_autoClose->start(qMax(kTouchIdleMs, m_durationMs));
        if (m_expanded) m_expandGuard->start(kIdleExpandMs);
        return;
    }
    m_autoClose->stop();             // 按住：先停表（不是延后，是完全不跑）
    m_expandGuard->stop();
    m_touchGuard->start(kTouchMaxMs);   // 兜底：released 丢了也要能自愈
}

void IslandOverlay::noteActivity()
{
    if (m_touching) return;          // 按住期间由 setTouching 统一接管，别在这里把表又跑起来
    if (m_expanded) m_expandGuard->start(kIdleExpandMs);
    // 显式时长（U盘 15s / OTA 20s）是调用方"到点就收"的契约，不因交互顺延；
    // 常驻（<=0）本就不排期。其余情况一律按"最后一次交互"重新计时。
    if (m_durationMs > 0 && m_autoClose->isActive())
        m_autoClose->start(qMax(kTouchIdleMs, m_durationMs));
}

void IslandOverlay::mousePressEvent(QMouseEvent *ev)
{
    if (!m_interactive) { ev->ignore(); return; }   // 穿透态不消费
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }
    // ⚠️ 触控（2026-10-07）：按下**不做任何动作**，只记起点。
    //    原先把"展开/打开"写在 press 里，手指在触屏上按下就有肉眼不可见的抖动，
    //    轻轻一滑就被判定成点（表现为拉了一下就弹出资源管理器）。
    //    现在动作一律挪到 release，且中间滑过 kDragSlop 就只当拖拽、不当点击。
    // ⚠️ 起点用**全局坐标**：手势期间窗口自己会位移（空闲 y=10 → 展开 y=24），
    //    局部坐标会被窗口位移污染，同一个屏幕位置算出来的 dy 会漂。
    m_pressGlobal = ev->globalPosition();
    m_pressed = true;
    m_dragged = false;
    setTouching(true);   // 2026-11-02：按住期间不自动收（见 setTouching）
    ev->accept();
}

void IslandOverlay::mouseMoveEvent(QMouseEvent *ev)
{
    if (!m_interactive || !m_pressed) { ev->ignore(); return; }
    // ⚠️ 方向在跨过阈值的那一刻就**锁死**，之后不再跟手重判：
    //    拖拽会触发 geometry 动画，窗口自己在变大/变宽（空闲 y=10 → 展开 y=24 还会下移），
    //    于是"下拉展开"的下一次 move 可能算出反向的 dy 又调 setExpanded(false)，
    //    表现为一拉就展开、抖一下又收回去。锁定之后下拉就是下拉。
    if (m_dragged) { ev->accept(); return; }
    const qreal dy = ev->globalPosition().y() - m_pressGlobal.y();
    if (qAbs(dy) < kDragSlop) { ev->accept(); return; }   // 还在抖动容差里，不算拖
    m_dragged = true;
    // 下拉展开、上推收起 —— 和 iPhone 灵动岛同一个方向直觉
    setExpanded(dy > 0);
    ev->accept();
}

void IslandOverlay::mouseReleaseEvent(QMouseEvent *ev)
{
    // 2026-11-02：松手必须无条件清按住态 —— 放在下面那串提前 return **之前**。
    //   否则"右键抬起""没有对应按下""穿透态"这三种早退都会把 m_touching 永久挂住，
    //   表现就是岛再也收不回去（比原来的 bug 更糟，且现场完全看不出原因）。
    setTouching(false);
    if (!m_interactive) { ev->ignore(); return; }
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }  // 右键/中键抬起不算点击
    if (!m_pressed) { ev->ignore(); return; }                      // 没有对应的按下，不当点击
    m_pressed = false;
    if (m_dragged) {                  // 这是一次拖拽手势，收尾就完，别再当点击
        m_dragged = false;
        ev->accept();
        return;
    }

    // 交互按钮优先：「确认 / 快捷回复」点哪枚回哪句。点完即清按钮并收回（不再展开/收起）。
    int ai = -1;
    if (hitActionButton(ev->pos(), &ai)) {
        const QString reply = m_actions.value(ai);
        auto cb = m_onAction;
        m_actions.clear();
        m_onAction = nullptr;
        setInteractive(false);          // 内部会 sync
        dismiss();                      // 内部会 sync
        if (cb) cb(reply);
        ev->accept();
        return;
    }

    if (!m_openPath.isEmpty() && hitOpenButton(ev->pos())) {   // pos()=QPoint（不是 position()=QPointF）
        // ⚠️ 点击**分区**处理：岛体本身只负责展开/收起，"打开文件夹"只认右侧那个「▸ 打开」。
        //    原来一道按下就 openInShell + dismiss，而这条岛是 StaysOnTop 且开了交互（收走鼠标
        //    穿透），正好压在课件/白板上方 —— 学生本想点下面的课件、结果弹出资源管理器，
        //    教室大屏上属于事故。把破坏性动作关进一个小按钮区，误点最坏只是展开一下。
        openInShell();
        ev->accept();
        clearOpenPath();
        setInteractive(false);
        dismiss();
        return;
    }
    // 岛体：点一下展开看详情，**再点一下收回**（切换语义）。
    // ⚠️ 原来这里恒传 true：展开后再点毫无反应，而注释/归档都写的是"再点收回"，
    //    触屏用户又基本不会想到"上推"这个手势，结果点开就收不回去。
    //
    // 2026-11-04（m18334）：最小化胶囊态点一下**只回到完整收起条**，不是展开整页。
    //   用户原话「但不是展开到整个页面」—— 落到这里就是：展开动作被拦成"退最小化"。
    if (m_minimized) {
        setMinimized(false);
        ev->accept();
        return;
    }
    setExpanded(!m_expanded);
    ev->accept();
}
