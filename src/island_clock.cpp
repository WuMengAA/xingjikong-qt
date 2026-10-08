#include "island_clock.h"

#include <QPainter>
#include <QFontMetrics>
#include <QTime>

IslandClock::IslandClock(QWidget *parent) : QWidget(parent)
{
    m_tick.setInterval(500);   // 分钟级刷新足够，半秒一查保证跨分不延迟
    connect(&m_tick, &QTimer::timeout, this, &IslandClock::refresh);
    m_tick.start();
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void IslandClock::setStatus(const QString &status)
{
    m_status = status;
    update();
}

void IslandClock::setLight(bool light)
{
    m_light = light;
    update();
}

QString IslandClock::currentTimeText() const
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm"));
}

void IslandClock::refresh()
{
    update();   // 直接重绘（时间变了才重画；500ms 一次开销可忽略）
}

void IslandClock::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor fg = m_light ? QColor(30, 30, 30) : QColor(245, 245, 245);
    const QColor dim = m_light ? QColor(100, 100, 100) : QColor(140, 140, 140);

    const QString time = currentTimeText();
    QFont tf; tf.setPixelSize(m_timePx); tf.setWeight(QFont::Medium);
    p.setFont(tf);
    p.setPen(fg);
    const int timeAscent = p.fontMetrics().ascent();
    p.drawText(0, timeAscent, time);

    // 状态小字（可选）：放时间下方
    if (!m_status.isEmpty()) {
        QFont sf; sf.setPixelSize(qMax(8, m_timePx / 2));
        p.setFont(sf);
        p.setPen(dim);
        const int lineH = p.fontMetrics().height();
        p.drawText(0, timeAscent + 4 + lineH, m_status);
    }
}