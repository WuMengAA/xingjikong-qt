#include "schedule_view.h"
#include "schedule/profile_repository.h"
#include <QPainter>
#include <QKeyEvent>
#include <QDateTime>
#include <algorithm>

ScheduleViewWindow::ScheduleViewWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("星集控 · 今日课表"));
    resize(900, 700);
    setAttribute(Qt::WA_DeleteOnClose);

    m_timer.setInterval(30000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ScheduleViewWindow::refresh);
}

void ScheduleViewWindow::setProfilePath(const QString& path)
{
    m_profilePath = path;
    refresh();
    if (!m_timer.isActive()) m_timer.start();
}

void ScheduleViewWindow::toggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    if (m_fullscreen) {
        showFullScreen();
    } else {
        showNormal();
        resize(900, 700);
    }
    update();
}

void ScheduleViewWindow::keyPressEvent(QKeyEvent* ev)
{
    if (ev->key() == Qt::Key_Escape && m_fullscreen) {
        toggleFullscreen();
        return;
    }
    QWidget::keyPressEvent(ev);
}

// 构建今日课程行
QList<ScheduleViewWindow::Row> ScheduleViewWindow::buildRows(
    const Profile& profile, const QDate& today, int currentWeek)
{
    QList<Row> rows;
    const int weekday = today.dayOfWeek();
    const QTime now = QTime::currentTime();

    // 节次时间表：按开始时间排序的上课时间点
    QList<TimeSlot> slotTable;
    for (const TimeSlot& ts : profile.timeSlots) {
        if (ts.timeType == 0 && ts.isActive) slotTable.append(ts);
    }
    std::sort(slotTable.begin(), slotTable.end(),
              [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });

    // 收集选中群今天的所有课程
    struct Item { int slot; QString subject; QTime start, end; };
    QList<Item> items;
    const QList<ClassPlan> plans = profile.activeClassPlans();
    for (const ClassPlan& cp : plans) {
        if (cp.weekDay != weekday || !cp.matchesWeek(currentWeek)) continue;
        for (const Lesson& l : cp.lessons) {
            if (!l.isActive) continue;
            if (l.slotIndex < 0 || l.slotIndex >= slotTable.size()) continue;
            const TimeSlot& ts = slotTable[l.slotIndex];
            const auto subIt = profile.subjects.find(l.subjectId);
            QString sub = (subIt != profile.subjects.end())
                ? (subIt->name.isEmpty() ? subIt->simplifiedName : subIt->name)
                : QStringLiteral("—");
            Item it;
            it.slot = l.slotIndex;
            it.subject = ts.name.isEmpty() ? sub : QStringLiteral("%1 · %2").arg(ts.name, sub);
            it.start = ts.startTime; it.end = ts.endTime;
            items.append(it);
        }
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.start < b.start; });

    for (const Item& it : items) {
        Row r;
        r.timeText = QStringLiteral("%1 - %2").arg(it.start.toString("HH:mm"), it.end.toString("HH:mm"));
        r.subject = it.subject;
        r.isNow = (now >= it.start && now < it.end);
        r.isPast = (now >= it.end);
        rows.append(r);
    }
    return rows;
}

void ScheduleViewWindow::refresh()
{
    m_rows.clear();
    if (!m_profilePath.isEmpty()) {
        ProfileRepository repo;
        Profile profile = repo.load(m_profilePath);
        const QDate today = QDate::currentDate();
        QDate start(today.year(), 9, 1);
        const int currentWeek = (start.daysTo(today) >= 0) ? (start.daysTo(today) / 7) + 1 : 1;
        m_rows = buildRows(profile, today, currentWeek);
    }
    update();
}

void ScheduleViewWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect();
    const int w = r.width(), h = r.height();

    // 深色背景
    p.fillRect(r, QColor(0x0A, 0x0A, 0x0A));

    // 标题
    p.setPen(QColor(0xFA, 0xFA, 0xFA));
    QFont titleFont = font();
    titleFont.setPixelSize(qMax(22, h / 28));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(0, h / 40, w, h / 18), Qt::AlignHCenter,
               QStringLiteral("今日课表 · %1").arg(QDate::currentDate().toString("MM月dd日 dddd")));

    // 列表
    const int top = h * 8 / 40;
    const int rowH = m_rows.isEmpty() ? 0 : qMax(36, (h - top - h / 30) / m_rows.size());
    const int left = w / 16, right = w - w / 16;

    if (m_rows.isEmpty()) {
        p.setPen(QColor(0x9A, 0x9A, 0x9A));
        QFont f = font(); f.setPixelSize(qMax(16, h / 40)); p.setFont(f);
        p.drawText(QRect(0, top, w, rowH), Qt::AlignHCenter,
                   QStringLiteral("今天没有排课，或档案里没有课表"));
        return;
    }

    QFont rowFont = font();
    rowFont.setPixelSize(qMax(16, h / 36));
    QFont timeFont = rowFont;
    timeFont.setPixelSize(qMax(13, h / 44));

    int y = top;
    for (const Row& row : m_rows) {
        const QRect rowRect(left, y, right - left, rowH - 6);
        // 背景：当前节高亮（浅色反白），已过暗淡
        if (row.isNow) {
            p.fillRect(rowRect, QColor(0xF0, 0xF0, 0xF0));
        } else if (row.isPast) {
            p.fillRect(rowRect, QColor(0x14, 0x14, 0x14));
        } else {
            p.fillRect(rowRect, QColor(0x1E, 0x1E, 0x1E));
        }
        p.setPen(row.isNow ? QColor(0x14, 0x14, 0x14)
                 : (row.isPast ? QColor(0x5A, 0x5A, 0x5A) : QColor(0xFA, 0xFA, 0xFA)));
        p.setFont(rowFont);
        p.drawText(rowRect.adjusted(16, 0, -40, 0), Qt::AlignVCenter | Qt::AlignLeft, row.subject);
        p.setFont(timeFont);
        p.drawText(rowRect.adjusted(0, 0, -16, 0), Qt::AlignVCenter | Qt::AlignRight, row.timeText);
        y += rowH;
    }

    // 提示（全屏时 Esc 退出）
    if (m_fullscreen) {
        p.setPen(QColor(0x4E, 0x4E, 0x4E));
        QFont hint = font(); hint.setPixelSize(qMax(11, h / 70)); p.setFont(hint);
        p.drawText(QRect(0, h - h / 30 - 20, w, 20), Qt::AlignHCenter,
                   QStringLiteral("Esc 退出全屏"));
    }
}
