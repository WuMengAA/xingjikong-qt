#include "schedule_today_provider.h"
#include "schedule/profile_repository.h"
#include <QDateTime>
#include <algorithm>

ScheduleTodayProvider::ScheduleTodayProvider(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(30000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ScheduleTodayProvider::load);
}

void ScheduleTodayProvider::setProfilePath(const QString& path)
{
    m_profilePath = path;
    load();
    if (!m_timer.isActive()) m_timer.start();
}

void ScheduleTodayProvider::refresh() { load(); }

void ScheduleTodayProvider::load()
{
    QJsonArray rows;
    if (!m_profilePath.isEmpty()) {
        ProfileRepository repo;
        Profile profile = repo.load(m_profilePath);
        const QDate today = QDate::currentDate();
        const int weekday = today.dayOfWeek();
        QDate start(today.year(), 9, 1);
        const int currentWeek = (start.daysTo(today) >= 0) ? (start.daysTo(today) / 7) + 1 : 1;
        const QTime now = QTime::currentTime();

        // 节次时间表（上课时间点，按下标）
        QList<TimeSlot> slotTable;
        for (const TimeSlot& ts : profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) slotTable.append(ts);
        }
        std::sort(slotTable.begin(), slotTable.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });

        // 选中群今日课程
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
            QJsonObject o;
            o["subject"] = it.subject;
            o["time"] = QStringLiteral("%1-%2").arg(it.start.toString("HH:mm"), it.end.toString("HH:mm"));
            o["startMin"] = it.start.hour() * 60 + it.start.minute();
            o["endMin"] = it.end.hour() * 60 + it.end.minute();
            o["nowMin"] = now.hour() * 60 + now.minute();
            o["isNow"] = (now >= it.start && now < it.end);
            o["isPast"] = (now >= it.end);
            rows.append(o);
        }
    }
    m_rows = rows;
    emit rowsChanged();
}
