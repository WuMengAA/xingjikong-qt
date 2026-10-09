#include "danmaku.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QScreen>
#include <QStyleHints>

// ── DanmakuQueue（纯逻辑，可单测）────────────────────────────────

DanmakuQueue::DanmakuQueue(QObject* parent) : QObject(parent) {}

int DanmakuQueue::push(const QString& text)
{
    const int idx = int(m_items.size());
    m_items.enqueue(text);
    if (idx == 0) emit itemReady(text);   // 队首立即准备展示
    return idx;
}

QString DanmakuQueue::next()
{
    if (m_items.isEmpty()) return QString();
    const QString t = m_items.dequeue();
    if (!m_items.isEmpty()) emit itemReady(m_items.head());
    return t;
}

// ── DanmakuWidget（无边框置顶滚动窗）────────────────────────────

DanmakuWidget::DanmakuWidget(QWidget* parent) : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    // 弹幕不挡点击：鼠标穿透（教室机大屏，了下课内容还要能点）
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, &DanmakuWidget::advance);

    hide();
}

void DanmakuWidget::push(const QString& text, const QColor& fg, int speedPxPerSec)
{
    const QString t = text.length() > 80 ? text.left(80) + QStringLiteral("…") : text;
    m_queue.enqueue(t);
    if (fg.isValid()) m_fg = fg;
    if (speedPxPerSec > 0) m_speed = speedPxPerSec;
    if (!m_active) startNext();
}

void DanmakuWidget::clearAll()
{
    m_queue.clear();
    m_cur.clear();
    m_active = false;
    m_frame.stop();
    hide();
}

void DanmakuWidget::startNext()
{
    if (m_queue.isEmpty()) { m_active = false; return; }
    m_cur = m_queue.dequeue();
    m_active = true;

    // 定位：屏幕上部三分一（不与顶部胶囊( y≈24, 高≈56 )重叠 → y 从 120 起）
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        const QRect sg = screen->availableGeometry();
        const int h = 40;
        setGeometry(sg.x(), sg.y() + 120, sg.width(), h);
    } else {
        setGeometry(0, 120, 1024, 40);
    }

    QFont f = font();
    f.setPixelSize(22);
    setFont(f);
    const QFontMetrics fm(f);
    m_textW = fm.horizontalAdvance(m_cur);
    m_x = width();                       // 从右侧外进入
    m_clock.restart();

    show();
    raise();
    m_frame.start();
    update();
}

void DanmakuWidget::advance()
{
    if (!m_active) return;
    const qint64 elapsedMs = m_clock.isValid() ? qint64(m_clock.elapsed()) : 0;
    m_clock.restart();
    // 已走过的像素 = 速度 * 时间
    const int dx = int(m_speed * (elapsedMs / 1000.0));
    m_x -= dx;
    // 完全滚出左侧 → 本条结束
    if (m_x + m_textW < 0) { finishCurrent(); return; }
    update();
}

void DanmakuWidget::finishCurrent()
{
    m_frame.stop();
    m_cur.clear();
    m_active = false;
    hide();
    if (!m_queue.isEmpty()) startNext();
}

void DanmakuWidget::paintEvent(QPaintEvent*)
{
    if (m_cur.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(m_fg.isValid() ? m_fg : QColor(255, 255, 255));
    p.setFont(font());
    p.drawText(m_x, 6, m_cur);
}