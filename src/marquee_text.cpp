#include "marquee_text.h"

#include <QFontMetrics>
#include <QPainter>

MarqueeText::MarqueeText(QWidget *parent) : QWidget(parent)
{
    m_frame.setInterval(16);   // ~60fps
    connect(&m_frame, &QTimer::timeout, this, &MarqueeText::tick);
    m_clock.invalidate();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void MarqueeText::setText(const QString &text)
{
    m_text = text;
    if (text.isEmpty()) {
        m_frame.stop();
        update();
        return;
    }
    const QFontMetrics fm(font());
    m_textW = fm.horizontalAdvance(text);
    // 从右边缘外进场：width() 变化（父级排布）时重设起点，保证总是"从右侧滚入"
    m_x = width();
    if (!m_paused) m_frame.start();
    update();
}

void MarqueeText::setPaused(bool on)
{
    if (m_paused == on) return;
    m_paused = on;
    if (on) { m_frame.stop(); m_x = 0; update(); }   // 停住：文本回到左侧起点（完整可见）
    else if (!m_text.isEmpty()) { m_clock.restart(); m_frame.start(); }
}

void MarqueeText::setLight(bool light)
{
    m_light = light;
    update();
}

void MarqueeText::tick()
{
    if (m_text.isEmpty() || m_paused) return;
    const qint64 ms = m_clock.isValid() ? qint64(m_clock.elapsed()) : 0;
    m_clock.restart();
    m_x -= int(m_speed * (ms / 1000.0));
    // 右缘滚出左缘 → 从右侧重新进场（循环）
    if (m_x + m_textW < 0) m_x = width();
    update();
}

void MarqueeText::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = m_light ? QColor(30, 30, 30) : QColor(245, 245, 245);
    p.setPen(c);
    p.setFont(font());
    // 垂直居中
    const int y = (height() - p.fontMetrics().height()) / 2 + p.fontMetrics().ascent();
    // 裁到本组件范围（父级负责裁整个岛的范围）
    p.drawText(m_x, y, m_text);
}