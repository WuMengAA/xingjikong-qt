#include "timeslot_model.h"
#include <algorithm>

TimeSlotModel::TimeSlotModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

TimeSlotModel::TimeSlotModel(const QList<TimeSlot>& slots, QObject* parent)
    : QAbstractListModel(parent), m_slots(slots)
{
    // 按开始时间排序
    std::sort(m_slots.begin(), m_slots.end(),
              [](const TimeSlot& a, const TimeSlot& b) {
                  return a.startTime < b.startTime;
              });
}

int TimeSlotModel::rowCount(const QModelIndex&) const {
    return m_slots.size();
}

QVariant TimeSlotModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_slots.size())
        return {};

    const TimeSlot& ts = m_slots[index.row()];
    switch (role) {
    case IdRole:
        return ts.id;
    case NameRole:
        return ts.name;
    case StartTimeRole:
        return ts.startTime.toString("HH:mm");
    case EndTimeRole:
        return ts.endTime.toString("HH:mm");
    case DurationMinutesRole:
        return ts.durationMinutes();
    case TimeTypeRole:
        return ts.timeType;
    case IsActiveRole:
        return ts.isActive;
    case StartSecsRole:
        return ts.startTime.secsTo(QTime(0, 0)) + 86400;  // 从午夜起的秒数
    case EndSecsRole:
        return ts.endTime.secsTo(QTime(0, 0)) + 86400;
    default:
        return {};
    }
}

bool TimeSlotModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_slots.size())
        return false;

    if (m_isUpdating) return true;  // 防重入
    m_isUpdating = true;

    TimeSlot& ts = m_slots[index.row()];
    bool changed = false;

    switch (role) {
    case NameRole:
        ts.name = value.toString();
        changed = true;
        break;
    case StartTimeRole: {
        QTime start = QTime::fromString(value.toString(), "HH:mm");
        QTime end = snapTo5(ts.endTime);
        if (isValidRange(start, end)) {
            ts.startTime = start;
            changed = true;
        }
        break;
    }
    case EndTimeRole: {
        QTime end = snapTo5(QTime::fromString(value.toString(), "HH:mm"));
        if (isValidRange(ts.startTime, end)) {
            ts.endTime = end;
            changed = true;
        }
        break;
    }
    case TimeTypeRole:
        ts.timeType = value.toInt();
        changed = true;
        break;
    case IsActiveRole:
        ts.isActive = value.toBool();
        changed = true;
        break;
    }

    if (changed) {
        emit dataChanged(index, index);
    }

    m_isUpdating = false;
    return changed;
}

TimeSlot TimeSlotModel::getSlot(int row) const {
    if (row < 0 || row >= m_slots.size()) return {};
    return m_slots[row];
}

bool TimeSlotModel::setSlotTime(int row, QTime start, QTime end) {
    if (row < 0 || row >= m_slots.size()) return false;
    if (!isValidRange(start, end)) return false;

    QModelIndex index = createIndex(row, 0);
    m_slots[row].startTime = start;
    m_slots[row].endTime = end;
    emit dataChanged(index, index);
    return true;
}

bool TimeSlotModel::setSlotName(int row, const QString& name) {
    if (row < 0 || row >= m_slots.size()) return false;
    QModelIndex index = createIndex(row, 0);
    m_slots[row].name = name;
    emit dataChanged(index, index);
    return true;
}

bool TimeSlotModel::setSlotType(int row, int timeType) {
    if (row < 0 || row >= m_slots.size()) return false;
    QModelIndex index = createIndex(row, 0);
    m_slots[row].timeType = timeType;
    emit dataChanged(index, index);
    return true;
}

void TimeSlotModel::setSlots(const QList<TimeSlot>& newSlots) {
    beginResetModel();
    m_slots = newSlots;
    std::sort(m_slots.begin(), m_slots.end(),
              [](const TimeSlot& a, const TimeSlot& b) {
                  return a.startTime < b.startTime;
              });
    endResetModel();
}

QTime TimeSlotModel::snapTo5(QTime t) {
    int secs = t.second();
    int roundedSecs = (secs / 300) * 300;  // 5分钟 = 300秒
    if (roundedSecs > 0 && roundedSecs == secs) {
        return t;  // 已经是 5 分钟倍数
    }
    // 向上或向下取整
    int minutes = t.minute() - (secs / 60);  // 去掉秒数
    int remainder = secs % 60;
    if (remainder >= 150) {  // >= 2.5 分钟，向上取整
        return t.addSecs(300 - remainder);
    } else {  // < 2.5 分钟，向下取整
        return t.addSecs(-remainder);
    }
}

bool TimeSlotModel::isValidRange(QTime start, QTime end) {
    if (!start.isValid() || !end.isValid()) return false;
    if (end <= start) return false;  // 结束时间必须晚于开始时间
    if (end.hour() - start.hour() > 12) return false;  // 不允许跨天
    return true;
}
