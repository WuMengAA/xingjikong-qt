// 星集控被控端 · 灵动岛实现（2026-10-07，设计文档 3.8 被控端版）
//
// 实现说明：
//   · 弹性缓动：QPropertyAnimation + OutBack（=iPhone 灵动岛的「果冻感」），
//     收起↔展开、出现↔消失都走动画，绝不用「啪一下出现」。
//   · 形状转变：Collapsed(56) ↔ Expanded(168) 两种高度，宽度随内容自适应；
//     内容重绘跟手，不「哐当」换。
//   · 交互穿透：默认 WA_TransparentForMouseEvents（纯展示，不挡下层课件）；
//     setInteractive(true) 后本岛接收点击（展开/收起）。
//   · 深/浅色：默认跟随系统 QGuiApplication::styleHints()->colorScheme()，
//     setLightMode() 可强制。
//   · 背景：与 NotifyWindow 同思路 —— WA_TranslucentBackground + paintEvent
//     手绘圆角半透明底（QSS 背景会在不透明窗口上先画一层方角底，露馅）。

#include "island.h"

#include <QApplication>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QStyleHints>
#include <QSvgRenderer>

// 2026-10-08 lucide(morphicons 同款) SVG 图标渲染：读取 qrc 内 /icons/<name>.svg，
// 把 stroke/fill 的 currentColor 换成主题前景色后渲染成 QPixmap（深浅主题自动适配）。
static QPixmap renderSvgIcon(const QString &name, int size, const QColor &color)
{
    if (name.isEmpty()) return QPixmap();
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QPixmap();
    const QByteArray raw = f.readAll();
    f.close();
    QString svg = QString::fromUtf8(raw);
    const QString col = color.name(QColor::HexRgb); // #rrggbb
    svg.replace(QStringLiteral("currentColor"), col);
    QSvgRenderer r;
    if (!r.load(svg.toUtf8())) return QPixmap();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p, QRect(0, 0, size, size));
    return pm;
}

static IslandOverlay *g_island = nullptr;

// 空闲半胶囊的尺寸（比活动态小一号，真的"收回顶部"）
static const int kIdleW = 120;
// 活动态胶囊的最小宽度（原来写死 200；收到文件时路径很长会撑宽，短文案时兜这个下限）
static const int kMinContentW = 200;
// 触控容差（像素）：手指按下到抬起之间会有肉眼看不见的抖动，
// 小于这个位移一律算点击不算拖拽；反过来超过它就只当手势、不当点击。
static const int kDragSlop = 24;
// 展开态最长停留时间（毫秒）：到点强制收回，避免大块胶囊长期压着屏幕顶部吃点击。
// 交互态展开本来没有回收路径（m_autoClose 的守卫会吞掉），这是唯一兜底。
static const int kExpandMaxMs = 20000;

IslandOverlay *IslandOverlay::instance()
{
    if (!g_island)
        g_island = new IslandOverlay();
    return g_island;
}

IslandOverlay::IslandOverlay(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    // 半透明圆角底：和 NotifyWindow 的岛/弹窗同一套做法（全屏遮罩不走这）。
    setAttribute(Qt::WA_TranslucentBackground);
    // 纯展示默认穿透：被控端在教室大屏，灵动岛不能挡住后面的课件/白板。
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose, false);
    // 触控（2026-10-07）：教室大屏基本都是触摸屏。这里**不开 WA_AcceptTouchEvents** ——
    // 开了就得自己处理 QTouchEvent 的触点编号/接受/拒绝一条龙，而默认行为已经把触点
    // 合成为 mouse press/move/release（配 mouseMove 做手势足够），自己啃只会多做acie_ERROR多错。
    // 触控要改的是**另一件事**：手指不像鼠标那样精确，见 kTapSlop / setInteractive 里的热区。
    // 先按系统深浅色定一次基调
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    m_light = (scheme == Qt::ColorScheme::Light);

    // 动画器：几何（位置/大小）+ 透明度
    m_geoAnim = new QPropertyAnimation(this, "geometry", this);
    m_geoAnim->setDuration(380);
    m_geoAnim->setEasingCurve(QEasingCurve::OutBack);

    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(1.0);
    setGraphicsEffect(eff);
    m_fadeAnim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim->setDuration(220);
    // ⚠️ finished 只连**一次**，而且是成员函数槽（不是 lambda）：
    //    · 淡入/淡出共用这一条动画，finished 两边都会发。原先把 `hide()` 直接挂在
    //      dismiss()/enterIdle() 里 connect(lambda)，那条连接**永不解绑** ⇒ 下一次
    //      淡入跑完也会 hide()，STE_QT_ISLAND_IDLE=0 下表现为"第一条之后每条提示只闪 220ms"。
    //    · 而且 Qt::UniqueConnection **对 lambda 不生效**（运行时只打一句告警照样连），
    //      所以那两处以为的"防重复"是空的。
    //    现在改成成员槽 + m_closing 状态位：只有"这次是淡出"才 hide。
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, &IslandOverlay::onFadeFinished);
    // ⚠️ 淡入/淡出必须**显式给起止值**。原来只 new 出来绑上 opacity 就完事，
    // 没 setStartValue/setEndValue ⇒ 起点终点都是"当前值"（都是 1.0）：
    //   ① 淡入淡出等于没做（一直在 1.0）；
    //   ② 更要命的是 QPropertyAnimation 值不变时 state 会一直挂在 Running，
    //      dismiss() 第一行 `if (state==Running) return` 于是**恒真** ——
    //      手动 dismiss 和 6 秒自动关闭全被它堵死，胶囊赖在屏幕上不走
    //      （实测：t=7 手动 dismiss() 后窗口仍 visible，t=7.3 还是 visible）。
    //      两个函数里都改成"先 stop() 再设起止值"就没这个问题了。

    // dismiss 兜底：淡出动画没发 finished 就强制收掉（胶囊不能赖在屏幕上不走）
    m_dismissGuard = new QTimer(this);
    m_dismissGuard->setSingleShot(true);
    connect(m_dismissGuard, &QTimer::timeout, this, [this]() {
        m_closing = false;                    // 兜底接管了，别让 finished 再 hide 一次
        if (m_idleEnabled) enterIdle(); else hide();
    });

    // 自动收起
    m_autoClose = new QTimer(this);
    m_autoClose->setSingleShot(true);
    connect(m_autoClose, &QTimer::timeout, this, [this]() {
        if (m_interactive && m_expanded) return;   // 交互态展开不自动收（见下 expandGuard 的硬超时兜底）
        dismiss();
    });

    // 展开态硬回收：交互态展开时 m_autoClose 会被守卫吞掉且不再排期，
    // 没有这条就是"点开就再也收不回"（教室大屏顶部被永久占住）。
    m_expandGuard = new QTimer(this);
    m_expandGuard->setSingleShot(true);
    connect(m_expandGuard, &QTimer::timeout, this, [this]() {
        if (m_expanded) setExpanded(false);
    });

    hide();
}

IslandOverlay::~IslandOverlay()
{
    if (g_island == this) g_island = nullptr;
}

// 空闲半胶囊的位置：比活动态更贴顶（y=10）、更小（120×36），真正"收回顶部"。
QRect IslandOverlay::geometryForIdle() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 120, int(Idle));
    const QRect sg = screen->geometry();
    return QRect(sg.x() + (sg.width() - kIdleW) / 2, sg.y() + 10, kIdleW, int(Idle));
}

// 活动态的位置：宽度由内容决定（上限 60% 屏宽），顶部居中 y=24。
QRect IslandOverlay::geometryForContent(int shapeHeight) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, shapeHeight);
    const QRect sg = screen->geometry();

    // 内容决定宽度（标题/描述最长行）
    const int maxW = qMin(sg.width() * 6 / 10, 640);
    QFontMetrics fm(font());
    const int iconW = m_icon.isEmpty() ? 0 : 28;
    const int textW = qMax(fm.horizontalAdvance((m_title.isEmpty() ? "" : m_title) + "  "),
                           fm.horizontalAdvance(m_desc));
    const int w = qBound(kMinContentW, iconW + textW + 48, maxW);
    return QRect(sg.x() + (sg.width() - w) / 2, sg.y() + 24, w, shapeHeight);
}

// 按**形态**算目标几何（#98）。过渡不用另外写：animateGeometry() 吃任意 QRect，
// 从当前几何补间到目标几何，所以"胶囊 → 居中卡片 → 全屏"之间的放大/收缩天然带动画。
QRect IslandOverlay::geometryForForm(Form f) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect(0, 0, 200, int(Collapsed));
    const QRect sg = screen->geometry();

    if (f == Form::Capsule)
        return geometryForContent(m_expanded ? int(Expanded) : int(Collapsed));

    if (f == Form::Centered) {
        // 屏幕正中的卡片：宽不超过屏宽 40%，高度 200（够放图标+标题+两行描述）
        const int w = qMin(sg.width() * 2 / 5, 520);
        const int h = 200;
        return QRect(sg.x() + (sg.width() - w) / 2,
                     sg.y() + (sg.height() - h) / 2, w, h);
    }

    // Fullscreen：铺满主屏，四周留 24px —— ClassIsland 那种"贴边但留一口气"的观感。
    // 留边而不是真全屏，是为了让它明显是"浮层"而不是"桌面换了"，学生不会误以为死机。
    const int m = 24;
    return QRect(sg.x() + m, sg.y() + m, sg.width() - 2 * m, sg.height() - 2 * m);
}

void IslandOverlay::onFadeFinished()
{
    // 淡入结束：什么都不做（这条动画是共用的，原来在这里无条件 hide() 会把新提示灭掉）。
    // 只有"这次真的是淡出"（dismiss/enterIdle 把 m_closing 置了 true）才收窗。
    if (!m_closing) return;
    m_closing = false;
    m_dismissGuard->stop();
    hide();
}

void IslandOverlay::showIsland(const QString &title, const QString &desc,
                               const QString &icon, int durationMs, Form form)
{
    m_title = title;
    m_desc = desc;
    m_icon = icon;
    m_durationMs = durationMs;
    m_form = form;
    m_idle = false;
    m_hadContent = true;
    m_contentActive = true;
    m_closing = false;          // 新内容上屏：取消"淡出中"，否则淡入结束会被当成淡出去 hide
    m_dismissGuard->stop();     // ⚠️ 必须停：上一条的兜底守卫是"给上一条用的"，
                                //    不停的话它到点会把刚上屏的新提示打掉
    m_expandGuard->stop();      // 展开定时器同理（新内容回到收起态，不该继承上一条的回收时刻）
    const bool wasExpanded = m_expanded;
    m_expanded = false;         // 新内容一律回到收起态，别沿用上一条的展开高度换文案

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) { hide(); return; }

    // 从空闲胶囊长出来（Dimensions 变了所以走动画；第一次出现才直接 setGeometry）
    const QRect target = geometryForForm(m_form);

    if (!isVisible()) {
        setGeometry(target);
        show();
        raise();
    } else {
        animateGeometry(target);
    }

    // 淡入：显式从当前不透明度（首出现是 1）补间到 1，并且先 stop 再 start。
    // ⚠️ 不能只 start()：QPropertyAnimation 会在上一次动画的终点上接着跑，
    //    而 dismiss 把它留在了 0，下次出现就永远是 0（淡入"淡"进去的是透明的）。
    if (auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject()))
        m_fadeAnim->setStartValue(eff->opacity());
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->stop();
    m_fadeAnim->setDirection(QAbstractAnimation::Forward);
    m_fadeAnim->start();

    // 自动收起。
    // ⚠️ 交互态（interactive）**一律不自动关**：这里原来写的是
    //   `durationMs>0 && !(m_interactive && m_expanded)` —— 交互态但还收着的时候
    //   倒计时照样走，6 秒后胶囊自己消失，用户压根没机会点（点了才展开，
    //   可人还没来得及伸手呢）。可点的东西不能自己跑掉，交给 setInteractive(false)
    //   或下次 showIsland 顶掉时收。
    m_autoClose->stop();
    if (durationMs > 0 && !m_interactive)
        m_autoClose->start(durationMs);

    // 新内容把上一条的展开态压平了 —— 状态变了就得发信号（见 enterIdle 里的同款说明）
    if (wasExpanded) emit expandedChanged(false);
    update();
}

void IslandOverlay::setIdleEnabled(bool on)
{
    m_idleEnabled = on;
    if (!on && m_idle && isVisible()) {
        // 关掉空闲态时得把残留状态一起清掉：不然 m_idle 还留着 true、
        // m_interactive 还是 true（一个"隐藏但可交互"的状态，谁也说不清它是什么）。
        m_idle = false;
        m_expanded = false;
        setInteractive(false);
        hide();
    }
}

void IslandOverlay::ensureIdle()
{
    if (!m_idleEnabled) return;
    // ⚠️ 判据是 isVisible() 而不是 `m_idle && isVisible()`：后者在"岛上正显示着一条
    //    真实内容（m_idle=false）"时会走进去把它顶成空闲胶囊 —— 一个叫 ensure 的入口
    //    不该顺手销毁别人的内容态。
    if (isVisible()) return;
    enterIdle();
}

void IslandOverlay::enterIdle()
{
    // ⚠️ 形态必须回到胶囊：空闲半胶囊是**顶部**那条，若沿用上一条的 Centered/Fullscreen，
    //    收回动画的目标几何会是"屏幕正中/全屏"，空闲态就变成一块挡在桌面中间的大板子。
    m_form = Form::Capsule;
    m_dismissGuard->stop();     // 先把上一条的兜底守卫停掉（原来自检点排在 early return 之后，留了窗口）
    m_expandGuard->stop();
    m_closing = false;

    if (!m_idleEnabled) {
        // 关掉空闲态就退回老行为：彻底隐藏
        m_idle = false;
        m_expanded = false;
        m_contentActive = false;
        m_autoClose->stop();
        clearOpenPath();
        setInteractive(false);
        m_closing = true;                 // 这次是淡出，finished 到了才 hide
        m_dismissGuard->start(600);       // 淡出没发 finished 也得收掉
        auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject());
        if (eff) m_fadeAnim->setStartValue(eff->opacity());
        m_fadeAnim->setEndValue(0.0);
        m_fadeAnim->stop();
        m_fadeAnim->setDirection(QAbstractAnimation::Forward);
        m_fadeAnim->start();
        return;
    }

    if (m_idle && isVisible()) return;

    const bool wasExpanded = m_expanded;
    m_idle = true;
    m_expanded = false;
    m_contentActive = false;   // 内容这就算过去了，之后展开的是"最近一条"
    m_autoClose->stop();
    clearOpenPath();
    // 空闲胶囊可点：这将近 120×36 的一小块不再穿透。作为补偿它贴在最顶端（y=10）
    // 且画得比活动态更淡，尽量少占课件的可视注意力。
    // （另一条防线在 setExpanded 里：展开态有硬回收超时，不会长期压着屏幕顶部。）
    setInteractive(true);

    if (!isVisible()) {
        setGeometry(geometryForIdle());
        show();
        raise();
    } else {
        animateGeometry(geometryForIdle());
    }
    update();
    // 状态改了就要发信号：dismiss→enterIdle 是最常见的收起路径，
    // 原来它不发 expandedChanged(false)，对外就出现"isExpanded() 已是 false、
    // 最后一次信号还是 true"的自相矛盾。
    if (wasExpanded) emit expandedChanged(false);
}

void IslandOverlay::setExpanded(bool on)
{
    if (m_expanded == on) return;
    m_expanded = on;

    // 从空闲态展开：得有东西可看。从来没显示过内容就给一句占位话，
    // 否则展开是一块空白的黑底 —— 用户会觉得"点了没反应"。
    if (on && !m_hadContent) {
        m_title = QStringLiteral("暂无进行中的通知");
        m_desc = QStringLiteral("有新的动态会在这里提示");
    }
    if (on) m_idle = false;

    if (!isVisible()) { emit expandedChanged(on); return; }

    // 空闲态展开再收起：直接回空闲半胶囊。
    // 中间别在"展开(168) → 标题胶囊(56) → 空闲(36)"来回三段跳 ——
    // 用户从空闲拉下来看一眼，推回去就该原样回到空闲。
    if (!on && !m_contentActive) {
        enterIdle();
        emit expandedChanged(on);
        return;
    }

    // ⚠️ 展开/收起是**胶囊**那一档的概念（56↔168 高度切换）。
    //    Centered / Fullscreen 形态下点一下不该把它压回顶部胶囊 —— 那会让全屏通知
    //    被误触后瞬间缩成一条，看着像崩了。非胶囊形态：保持原几何，只记状态。
    if (m_form != Form::Capsule) {
        m_expanded = on;
        if (on) m_autoClose->start(qMax(5000, m_durationMs));
        emit expandedChanged(on);
        return;
    }

    if (m_idle || on) {
        // 空闲↔展开：宽度也要跟着变（120 ↔ 内容宽），所以整块 geometry 都要给
        animateGeometry(on ? geometryForContent(int(Expanded)) : geometryForIdle());
    } else {
        animateGeometry(geometryForContent(m_expanded ? int(Expanded) : int(Collapsed)));
    }
    // 展开给足阅读时间
    if (on) m_autoClose->start(qMax(5000, m_durationMs));
    // ⚠️ 展开态必须有一条**硬回收**路径：展开就是 168px 高、最宽 640px 的大块，
    //    而且交互态（不穿透）。原来唯一的出路是"上推 ≥24px"，而 m_autoClose 的
    //    `if (m_interactive && m_expanded) return;` 又会把到点的那次直接吞掉且不再排期 ——
    //    于是空闲态/文件提示一旦被点开，就永远压在屏幕顶部吃点击。
    //    这里给 20 秒硬超时：到点无论如何收回去。
    if (on) m_expandGuard->start(kExpandMaxMs);
    else    m_expandGuard->stop();
    emit expandedChanged(on);
}

void IslandOverlay::animateGeometry(const QRect &target)
{
    m_geoAnim->stop();
    m_geoAnim->setStartValue(geometry());
    m_geoAnim->setEndValue(target);
    m_geoAnim->start();
}

void IslandOverlay::dismiss()
{
    // 空闲态开启时，"收起"不是消失，而是收回成顶部那条半胶囊
    // （2026-10-07 用户要求：无任务、通知则收回顶部，显示半胶囊+横杠箭头）。
    if (m_idleEnabled) {
        if (!isVisible()) { enterIdle(); return; }
        m_dismissGuard->stop();
        m_autoClose->stop();
        enterIdle();
        return;
    }

    // 兜底：淡出动画万一没发 finished（属性/状态异常），也不能让胶囊赖在屏幕上。
    m_dismissGuard->stop();
    m_dismissGuard->start(600);

    // ⚠️ 顺序要是 stop → 设起止 → 设方向 → start 反过来：
    //    直接 start() 时 QPropertyAnimation 会从上一次终点接着跑，永远回不到 0。
    auto *eff = qobject_cast<QGraphicsOpacityEffect *>(m_fadeAnim->targetObject());
    if (eff) m_fadeAnim->setStartValue(eff->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->stop();
    m_fadeAnim->setDirection(QAbstractAnimation::Forward);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        hide();
    }, Qt::UniqueConnection);
    m_fadeAnim->start();
}

void IslandOverlay::setLightMode(bool light)
{
    if (m_light == light) return;
    m_light = light;
    update();
}

void IslandOverlay::setInteractive(bool on)
{
    if (m_interactive == on) return;
    m_interactive = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    if (on) setCursor(Qt::PointingHandCursor);
    else    unsetCursor();
    // 从可点切回纯展示：这条如果还在屏上，就得给它定个消失时间（可点时不定时）。
    if (!on && isVisible() && m_durationMs > 0)
        m_autoClose->start(m_durationMs);
}

void IslandOverlay::setOpenPath(const QString &path)
{
    m_openPath = path;
    update();
}

void IslandOverlay::clearOpenPath()
{
    if (m_openPath.isEmpty()) return;
    m_openPath.clear();
    update();
}

// 打开 m_openPath 所在的文件夹并选中该文件。
// ⚠️ 用 explorer /select 而不是 QDesktopServices::openPath(文件原路径)：后者在 Windows
// 上是 ShellExecute 打开**文件本身**（.exe 就直接跑、.pdf 拉起阅读器），被控端是学生机、
// 下发内容不是我们能担保的，等于替下发的那台设备决定运行什么。只"定位到文件"最稳。
void IslandOverlay::openInShell()
{
    if (m_openPath.isEmpty()) return;
    const QFileInfo fi(m_openPath);
    if (!fi.exists()) return;
    QStringList args;
    args << QStringLiteral("/select,") << QDir::toNativeSeparators(fi.absoluteFilePath());
    QProcess::startDetached(QStringLiteral("explorer.exe"), args);
}

// Centered / Fullscreen 的绘制（#98）。
// ⚠️ 与胶囊绘制**完全分开**：胶囊那套逻辑是按 56/168px 高、顶部居中的假设写的
//    （图标在左、标题描述横排），硬塞进全屏会把文字挤到左上角一小块。
//    这里单独写一条"居中大字"的路径，两边互不干扰。
void IslandOverlay::paintBigForm()
{
    const bool full = (m_form == Form::Fullscreen);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 底：全屏时更不透明（要压住桌面），居中卡片保持半透明（还能看见后面的课件）
    const QColor bg = m_light ? QColor(250, 250, 250, full ? 248 : 235)
                              : QColor(16, 16, 18, full ? 243 : 228);
    const QColor fg = m_light ? QColor(10, 10, 10) : QColor(250, 250, 250);
    const QColor sub = m_light ? QColor(90, 90, 90) : QColor(168, 168, 178);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), full ? 20 : 16, full ? 20 : 16);
    p.fillPath(path, bg);
    p.setPen(m_light ? QColor(210, 210, 210, 180) : QColor(70, 70, 70, 180));
    p.drawPath(path);

    // 图标（可选）：居中偏上，全屏时画得大
    const int iconSize = full ? 56 : 34;
    int cursorY = rect().top() + (full ? 56 : 26);
    if (!m_icon.isEmpty()) {
        const QPixmap ipm = renderSvgIcon(m_icon, iconSize, fg);
        if (!ipm.isNull()) {
            p.drawPixmap(QRect(rect().center().x() - iconSize / 2, cursorY,
                               iconSize, iconSize), ipm);
            cursorY += iconSize + (full ? 28 : 18);
        } else {
            cursorY -= (full ? 28 : 12);   // 图标画不出来就把位置还回去，别留一大块空白
        }
    }

    // 标题：全屏 42px，居中卡片 22px；过长自动换行而不是撑破（全屏时按 90% 宽折行）
    QFont tf = font();
    tf.setPixelSize(full ? 42 : 22);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(fg);
    const int textW = rect().width() - (full ? 120 : 64);
    const QRect titleRect(rect().center().x() - textW / 2, cursorY, textW, 0);
    QRect tb = p.boundingRect(titleRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_title);
    p.drawText(tb, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_title);
    cursorY += tb.height() + (full ? 26 : 16);

    // 描述：次字号，同样居中折行
    if (!m_desc.isEmpty()) {
        QFont df = font();
        df.setPixelSize(full ? 22 : 14);
        p.setFont(df);
        p.setPen(sub);
        const QRect descRect(rect().center().x() - textW / 2, cursorY, textW, 0);
        QRect db = p.boundingRect(descRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_desc);
        p.drawText(db, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_desc);
    }
}

void IslandOverlay::paintEvent(QPaintEvent *)
{
    // #98：居中卡片 / 全屏走独立绘制路径，不吃下面那套胶囊逻辑
    if (m_form != Form::Capsule) {
        paintBigForm();
        return;
    }

    // 圆角半透明底（深浅两套），沿用 NotifyWindow 思路：paintEvent 画，不走 QSS。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ── 空闲态：半胶囊 + 横杠箭头 ──
    if (m_idle) {
        // 画得比活动态更淡（也被理解为"半"胶囊）：退到背景里，别抢课件的注意力。
        QPainterPath ip;
        ip.addRoundedRect(rect().adjusted(1, 1, -1, -1), rect().height() / 2.0,
                          rect().height() / 2.0);
        p.fillPath(ip, m_light ? QColor(250, 250, 250, 150)
                               : QColor(20, 20, 20, 140));
        p.setPen(m_light ? QColor(200, 200, 200, 160) : QColor(58, 58, 58, 160));
        p.drawPath(ip);

        // 横杠（28×4 圆角）+ 下方一个小三角箭头：通行的"往下拉"指示，
        // 不用 emoji/字体字符（U+2304 之类在不少字体里是豆腐块，手绘最稳）。
        // ⚠️ 前景不透明度别跟着底色一起压：浅色态原来是 alpha 170 的深灰叠在 alpha 150 的
        //    白胶囊上，实测糊成一团灰、看不出是"可以拉的把手"。底色退让、符号要清。
        const QColor barCol = m_light ? QColor(0x16, 0x16, 0x16, 215) : QColor(0xFA, 0xFA, 0xFA, 225);
        p.setPen(Qt::NoPen);
        p.setBrush(barCol);
        const int cx = rect().center().x();
        const int cy = rect().center().y() - 3;
        p.drawRoundedRect(QRect(cx - 14, cy - 2, 28, 4), 2, 2);

        QPainterPath tri;
        tri.moveTo(cx - 6, cy + 5);
        tri.lineTo(cx + 6, cy + 5);
        tri.lineTo(cx, cy + 12);
        tri.closeSubpath();
        p.drawPath(tri);
        return;
    }

    // 圆角半透明底（深浅两套），沿用 NotifyWindow 思路：paintEvent 画，不走 QSS。
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 24, 24);
    p.fillPath(path, m_light ? QColor(250, 250, 250, 235)
                            : QColor(20, 20, 20, 225));
    p.setPen(m_light ? QColor(200, 200, 200) : QColor(58, 58, 58));
    p.drawPath(path);

    const QColor fg = m_light ? QColor(0x16, 0x16, 0x16) : QColor(0xFA, 0xFA, 0xFA);
    const QColor sub = m_light ? QColor(0x6E, 0x6E, 0x6E) : QColor(0xC8, 0xC8, 0xC8);

    const bool canOpen = !m_openPath.isEmpty();
    const int hintW = canOpen ? 34 : 0;   // 右侧「▸ 打开」占的宽，正文要给它让位
    QRect body = rect().adjusted(18, 0, -(16 + hintW), 0);
    int x = body.left();
    if (!m_icon.isEmpty()) {
        // 2026-10-08：图标从 emoji 字形改为 lucide(morphicons 同款) SVG，
        // 颜色直接绑前景 fg（深主题白 / 浅主题黑），跨主题稳定不糊。
        const QPixmap ipm = renderSvgIcon(m_icon, 22, fg);
        if (!ipm.isNull()) {
            const int iy = rect().top() + (height() - 22) / 2;
            p.drawPixmap(QRect(x, iy, 22, 22), ipm);
            x += 30;
        }
    }

    if (m_expanded) {
        // 展开态：标题上移，描述换行显示
        QFont tf = font();
        tf.setPixelSize(17);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(fg);
        p.drawText(QRect(x, rect().top() + 18, body.right() - x, 24),
                   Qt::AlignVCenter | Qt::AlignLeft, m_title);
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(13);
            p.setFont(df);
            p.setPen(sub);
            p.drawText(QRect(x, rect().top() + 52, body.right() - x, 70),
                       Qt::AlignLeft | Qt::TextWordWrap, m_desc);
        }
    } else {
        // 收起态：标题左、描述右
        QFont tf = font();
        tf.setPixelSize(15);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(fg);
        p.drawText(QRect(x, rect().top(), body.right() - x, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_title);
        // ⚠️ 描述紧跟标题、按**剩余宽度**省略，且用 ElideRight：
        //    老写法把它固定塞在 x+110 起、110px 宽、右对齐，胶囊只有两三百宽时
        //    实际能显示的只剩两三个字（实测「正在自动重连…」被裁成「连...」，省成「…」）——
        //    提示看不懂，等于没提示。
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(12);
            const int titleW = p.fontMetrics().horizontalAdvance(m_title);
            const int dw = body.right() - (x + titleW) - 14;
            if (dw > 48) {
                const QString shown = QFontMetrics(df).elidedText(m_desc, Qt::ElideRight, dw);
                if (!shown.isEmpty()) {
                    p.setFont(df);
                    p.setPen(sub);
                    p.drawText(QRect(x + titleW + 14, rect().top(), dw, height()),
                               Qt::AlignVCenter | Qt::AlignLeft, shown);
                }
            }
        }
    }

    // 可点提示：设了 openPath 就给个"能点"的信号（右侧一个尖角 + 稍亮的字），
    // 否则学生看见胶囊写着一串路径，根本不知道点它等于打开文件夹。
    if (canOpen) {
        // 「▸ 打开」原本用 Unicode 尖角符号，现改为 lucide chevron-right SVG + 「打开」文字，
        // 与灵动岛主图标统一走 SVG 资源，避免依赖符号字体、跨主题稳定。
        QFont hf = font();
        hf.setPixelSize(12);
        p.setFont(hf);
        p.setPen(m_interactive ? fg : sub);
        const QString label = QStringLiteral("打开");
        const int tw = p.fontMetrics().horizontalAdvance(label);
        const int chevW = 12, gap = 3;
        const int totalW = chevW + gap + tw;
        const int regionRight = body.right() + hintW;        // == rect().right()-16
        const int blockX = regionRight - totalW;              // 整块右对齐
        const int cy = rect().top() + (height() - chevW) / 2;
        const QPixmap cp = renderSvgIcon(QStringLiteral("chevron-right"), chevW,
                                         m_interactive ? fg : sub);
        if (!cp.isNull())
            p.drawPixmap(QRect(blockX, cy, chevW, chevW), cp);
        p.drawText(QRect(blockX + chevW + gap, rect().top(), tw, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
    }
}

// 「▸ 打开」按钮的命中区：贴着右边缘那一小块，右边界 = rect().right()-16，
// 与 paintEvent 里的绘制位置严格对齐（body.right()+hintW == rect().right()-16）。
static const int kOpenBtnW = 64;
bool IslandOverlay::hitOpenButton(const QPoint &pt) const
{
    if (m_openPath.isEmpty()) return false;
    // ⚠️ 命中区就是**窗口内**贴右边缘那一条，不要想着"上下各放宽 8px"：
    //    鼠标事件的局部坐标必然落在 rect() 之内，写成 rect().top()-8 / height()+16
    //    上下那 8px 永远收不到事件（那是窗口外的坐标），所谓"放宽"是假的。
    //    真要更大的触控目标，只能把宽度本身放大（kOpenBtnW 44→64）。
    const QRect btn(rect().right() - 16 - kOpenBtnW, rect().top(), kOpenBtnW, height());
    return btn.contains(pt);
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
    if (!m_interactive) { ev->ignore(); return; }
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }  // 右键/中键抬起不算点击
    if (!m_pressed) { ev->ignore(); return; }                      // 没有对应的按下，不当点击
    m_pressed = false;
    if (m_dragged) {                  // 这是一次拖拽手势，收尾就完，别再当点击
        m_dragged = false;
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
    setExpanded(!m_expanded);
    ev->accept();
}