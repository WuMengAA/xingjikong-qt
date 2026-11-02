#include "TodayScheduleModel.h"
#include "profile_repository.h"

#include <QDate>
#include <algorithm>

// 实现说明（与头文件的分工）：头文件讲「为什么」，这里只讲「怎么做」。
//
// 两条铁律贯穿本文件：
//   ① 不产生任何 UI 字面量之外的字符串判断 —— statusText 在这里拼完整句，
//      QML 拿到的就是能直接显示的成品，不做 if。
//   ② 列表重建与秒级走字分开走不同信号，理由见头文件「刷新节奏」。

namespace {

// 第几周：以当年 9 月 1 日（开学日近似）为第 1 周起点。
// 与旧 schedule_view / schedule_today_provider 保持**完全一致**的口径，
// 换实现不改行为 —— 否则同一个档案在托盘大屏和概览卡上会显示不同周的课。
int weekOfYear(const QDate &today)
{
    QDate start(today.year(), 9, 1);
    const int d = start.daysTo(today);
    return d >= 0 ? (d / 7) + 1 : 1;
}

} // namespace

TodayScheduleModel::TodayScheduleModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_dataTimer.setInterval(30000);
    m_dataTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_dataTimer, &QTimer::timeout, this, &TodayScheduleModel::load);

    m_clockTimer.setInterval(1000);
    m_clockTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_clockTimer, &QTimer::timeout, this, &TodayScheduleModel::tick);

    m_dateText = QDate::currentDate().toString(QStringLiteral("M月d日 dddd"));
    recomputeLive();
}

int TodayScheduleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> TodayScheduleModel::roleNames() const
{
    return {
        { SubjectRole,     "subject" },
        { TimeTextRole,    "timeText" },
        { StartMinRole,    "startMin" },
        { EndMinRole,      "endMin" },
        { IsNowRole,       "isNow" },
        { IsPastRole,      "isPast" },
        { ProgressRole,    "progress" },
        { SlotNameRole,    "slotName" },
        { SubjectOnlyRole, "subjectOnly" },
    };
}

QVariant TodayScheduleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case SubjectRole:     return r.subject;
    case TimeTextRole:    return r.timeText;
    case StartMinRole:    return r.startMin;
    case EndMinRole:      return r.endMin;
    case IsNowRole:       return r.progress >= 0.0;
    case IsPastRole:      return r.isPast;
    case ProgressRole:    return r.progress;
    case SlotNameRole:    return r.slotName;
    case SubjectOnlyRole: return r.subject;
    default:              return {};
    }
}

QString TodayScheduleModel::statusText() const
{
    // 一句话说清"现在什么状态"。大屏在教室后面，学生扫一眼就得懂。
    if (m_rows.isEmpty())
        return QStringLiteral("今天没有排课");
    if (m_currentIndex >= 0 && m_currentIndex < m_rows.size()) {
        const Row &r = m_rows.at(m_currentIndex);
        if (m_remainMin > 0) {
            return QStringLiteral("正在上 %1 · 还有 %2 分钟下课")
                .arg(r.subject, QString::number(m_remainMin));
        }
        return QStringLiteral("正在上 %1").arg(r.subject);
    }
    // 没在上课：区分「今天还没开始」和「今天已经上完」
    const QTime now = QTime::currentTime();
    const int nowMin = now.hour() * 60 + now.minute();
    if (!m_rows.isEmpty() && nowMin < m_rows.first().startMin)
        return QStringLiteral("还没开始上课");
    if (!m_rows.isEmpty() && nowMin >= m_rows.last().endMin)
        return QStringLiteral("今天的课已上完");
    return QStringLiteral("课间休息");
}

void TodayScheduleModel::setProfilePath(const QString &path)
{
    if (m_profilePath == path && !m_rows.isEmpty())
        return;
    m_profilePath = path;
    load();
    if (!m_dataTimer.isActive()) m_dataTimer.start();
    if (!m_clockTimer.isActive()) m_clockTimer.start();
    emit rowsChanged();
}

void TodayScheduleModel::reload() { load(); }

void TodayScheduleModel::load()
{
    rebuildRows();
    recomputeLive();
    emit rowsChanged();
}

void TodayScheduleModel::rebuildRows()
{
    QList<Row> rows;
    if (!m_profilePath.isEmpty()) {
        ProfileRepository repo;
        const Profile profile = repo.load(m_profilePath);
        const QDate today = QDate::currentDate();
        const int weekday = today.dayOfWeek();
        const int currentWeek = weekOfYear(today);

        // 节次时间表：只取「上课」类型且启用，按开始时间排序（下标即节次序号）
        QList<TimeSlot> slotTable;
        for (const TimeSlot &ts : profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) slotTable.append(ts);
        }
        std::sort(slotTable.begin(), slotTable.end(),
                  [](const TimeSlot &a, const TimeSlot &b) { return a.startTime < b.startTime; });

        struct Item { int slot; QString subject; QString slotName; QTime start, end; };
        QList<Item> items;
        for (const ClassPlan &cp : profile.activeClassPlans()) {
            if (cp.weekDay != weekday || !cp.matchesWeek(currentWeek)) continue;
            for (const Lesson &l : cp.lessons) {
                if (!l.isActive) continue;
                if (l.slotIndex < 0 || l.slotIndex >= slotTable.size()) continue;
                const TimeSlot &ts = slotTable[l.slotIndex];
                const auto subIt = profile.subjects.find(l.subjectId);
                const QString sub = (subIt != profile.subjects.end())
                    ? (subIt->name.isEmpty() ? subIt->simplifiedName : subIt->name)
                    : QStringLiteral("—");
                Item it;
                it.slot = l.slotIndex;
                it.subject = sub;
                it.slotName = ts.name;
                it.start = ts.startTime;
                it.end = ts.endTime;
                items.append(it);
            }
        }
        std::sort(items.begin(), items.end(),
                  [](const Item &a, const Item &b) { return a.start < b.start; });

        for (const Item &it : items) {
            Row r;
            r.subject = it.subject;
            r.slotName = it.slotName.isEmpty()
                ? QStringLiteral("第%1节").arg(it.slot + 1)
                : it.slotName;
            r.timeText = QStringLiteral("%1 - %2")
                .arg(it.start.toString(QStringLiteral("HH:mm")),
                     it.end.toString(QStringLiteral("HH:mm")));
            r.startMin = it.start.hour() * 60 + it.start.minute();
            r.endMin = it.end.hour() * 60 + it.end.minute();
            rows.append(r);
        }
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();
    m_dateText = QDate::currentDate().toString(QStringLiteral("M月d日 dddd"));
}

void TodayScheduleModel::recomputeLive()
{
    const QTime now = QTime::currentTime();
    const int nowMin = now.hour() * 60 + now.minute();
    const int nowSec = now.second();
    m_nowText = now.toString(QStringLiteral("HH:mm:ss"));

    int newIdx = -1;
    int remain = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (nowMin >= r.startMin && nowMin < r.endMin) {
            newIdx = i;
            remain = r.endMin - nowMin;
            // 进度：把当前分钟内的秒也计入，进度条才不会每秒跳一整格
            const int spanSec = (r.endMin - r.startMin) * 60;
            const int elapsedSec = (nowMin - r.startMin) * 60 + nowSec;
            r.progress = spanSec > 0 ? qBound(0.0, qreal(elapsedSec) / qreal(spanSec), 1.0) : 0.0;
            r.isPast = false;
        } else {
            r.progress = -1.0;
            r.isPast = (nowMin >= r.endMin);
        }
    }

    const bool idxChanged = (newIdx != m_currentIndex);
    m_currentIndex = newIdx;
    m_remainMin = remain;

    // 进度条每秒都在动 ⇒ 必须发 dataChanged，但只声明变化的 3 个 role，
    // 且范围覆盖全表（isPast 依赖 m_currentIndex，跨行变化）。
    // 已实测：只发 role 子集时 ListView 不会重建 delegate，滚动位置与动画不受影响。
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1),
                         { ProgressRole, IsNowRole, IsPastRole });
    if (idxChanged) emit currentChanged();
}

void TodayScheduleModel::tick()
{
    recomputeLive();
    emit tickChanged();
}
