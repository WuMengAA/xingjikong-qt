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
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStyleHints>

static IslandOverlay *g_island = nullptr;

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
    // 先按系统深浅色定一次基调
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    m_light = (scheme == Qt::ColorScheme::Light);

    // 动画器：几何（位置/大小）+ 透明度
    m_geoAnim = new QPropertyAnimation(this, "geometry", this);
    m_geoAnim->setDuration(380);
    m_geoAnim->setEasingCurve(QEasingCurve::OutBack);

    auto *eff = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(eff);
    m_fadeAnim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim->setDuration(220);

    // 自动收起
    m_autoClose = new QTimer(this);
    m_autoClose->setSingleShot(true);
    connect(m_autoClose, &QTimer::timeout, this, [this]() {
        if (m_interactive && m_expanded) return;   // 交互态展开不自动收
        dismiss();
    });

    hide();
}

IslandOverlay::~IslandOverlay()
{
    if (g_island == this) g_island = nullptr;
}

void IslandOverlay::showIsland(const QString &title, const QString &desc,
                               const QString &icon, int durationMs)
{
    m_title = title;
    m_desc = desc;
    m_icon = icon;
    m_durationMs = durationMs;

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) { hide(); return; }

    // 内容决定宽度（标题/描述最长行），上限 60% 屏宽
    const int maxW = qMin(screen->geometry().width() * 6 / 10, 640);
    QFontMetrics fm(font());
    const int iconW = m_icon.isEmpty() ? 0 : 28;
    const int textW = qMax(fm.horizontalAdvance((m_title.isEmpty() ? "" : m_title) + "  "),
                           fm.horizontalAdvance(m_desc));
    const int w = qBound(200, iconW + textW + 48, maxW);

    // 顶部居中（y=24 留出空隙；展开态从当前位置长高，不跳位）
    const QRect target(screen->geometry().x() + (screen->geometry().width() - w) / 2,
                       screen->geometry().y() + 24, w,
                       m_expanded ? int(Expanded) : int(Collapsed));

    if (!isVisible()) {
        setGeometry(target);
        show();
        raise();
    } else {
        animateGeometry(target);
    }

    // 淡入：每次出现都把方向复位为 Forward（dismiss 会置 Backward）
    if (m_fadeAnim->direction() != QAbstractAnimation::Forward)
        m_fadeAnim->setDirection(QAbstractAnimation::Forward);
    if (m_fadeAnim->state() != QAbstractAnimation::Running)
        m_fadeAnim->start();

    // 自动收起（交互态展开不自动关）
    m_autoClose->stop();
    if (durationMs > 0 && !(m_interactive && m_expanded))
        m_autoClose->start(durationMs);

    update();
}

void IslandOverlay::setExpanded(bool on)
{
    if (m_expanded == on) return;
    m_expanded = on;
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen || !isVisible()) { emit expandedChanged(on); return; }
    QRect g = geometry();
    g.setHeight(on ? int(Expanded) : int(Collapsed));
    animateGeometry(g);
    m_autoClose->start(qMax(4000, m_durationMs));   // 展开后给足阅读时间
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
    if (m_fadeAnim->state() == QAbstractAnimation::Running) return;
    m_fadeAnim->setDirection(QAbstractAnimation::Backward);
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
    if (!on && isVisible()) m_autoClose->start(qMax(3000, m_durationMs));
}

void IslandOverlay::paintEvent(QPaintEvent *)
{
    // 圆角半透明底（深浅两套），沿用 NotifyWindow 思路：paintEvent 画，不走 QSS。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 24, 24);
    p.fillPath(path, m_light ? QColor(250, 250, 250, 235)
                            : QColor(20, 20, 20, 225));
    p.setPen(m_light ? QColor(200, 200, 200) : QColor(58, 58, 58));
    p.drawPath(path);

    const QColor fg = m_light ? QColor(0x16, 0x16, 0x16) : QColor(0xFA, 0xFA, 0xFA);
    const QColor sub = m_light ? QColor(0x6E, 0x6E, 0x6E) : QColor(0xC8, 0xC8, 0xC8);

    QRect body = rect().adjusted(18, 0, -16, 0);
    int x = body.left();
    if (!m_icon.isEmpty()) {
        QFont ifont = font();
        ifont.setPixelSize(20);
        p.setFont(ifont);
        p.setPen(fg);
        p.drawText(QRect(x, rect().top(), 26, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_icon);
        x += 30;
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
        if (!m_desc.isEmpty()) {
            QFont df = font();
            df.setPixelSize(12);
            p.setFont(df);
            p.setPen(sub);
            int dw = body.right() >= x + 120 ? qMax(0, body.right() - x - 110) : 0;
            if (dw > 0)
                p.drawText(QRect(x + 110, rect().top(), dw, height()),
                           Qt::AlignVCenter | Qt::AlignRight, m_desc);
        }
    }
}

void IslandOverlay::mousePressEvent(QMouseEvent *ev)
{
    if (!m_interactive) { ev->ignore(); return; }   // 穿透态不消费
    if (ev->button() == Qt::LeftButton) setExpanded(!m_expanded);
    ev->accept();
}