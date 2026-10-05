#include "schedule_model.h"
#include <algorithm>

ScheduleModel::ScheduleModel(const Profile& profile, QObject* parent)
    : QAbstractTableModel(parent), m_profile(profile)
{
    // 过滤出上课时间点（TimeType==0），按开始时间排序
    for (const auto& ts : m_profile.timeSlots) {
        if (ts.timeType == 0 && ts.isActive) {
            m_timeSlots.append(ts);
        }
    }
    std::sort(m_timeSlots.begin(), m_timeSlots.end(),
              [](const TimeSlot& a, const TimeSlot& b) {
                  return a.startTime < b.startTime;
              });

    // 构建科目名映射
    const auto subKeys = m_profile.subjects.keys();
    for (const QString& k : subKeys) {
        const Subject& sub = m_profile.subjects.value(k);
        m_subjectNames[k] = sub.name.isEmpty() ? sub.simplifiedName : sub.name;
    }

    // 周几 -> 列索引
    for (int d = 1; d <= 7; ++d) {
        m_weekDayToCol[QString::number(d)] = d - 1;
    }
}

int ScheduleModel::rowCount(const QModelIndex&) const {
    return m_timeSlots.size();
}

int ScheduleModel::columnCount(const QModelIndex&) const {
    return 7;  // 周一~周日
}

QVariant ScheduleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    int row = index.row();
    int col = index.column();

    if (row < 0 || row >= rowCount()) return {};
    if (col < 0 || col >= 7) return {};

    // 找到该天该时间点的所有 ClassPlan
    int weekDay = col + 1;  // 1=周一
    const auto& ts = m_timeSlots[row];

    // 遍历当前激活课表群的 ClassPlans
    const auto activePlans = m_profile.activeClassPlans();
    for (const auto& cp : activePlans) {
        if (cp.weekDay != weekDay || !cp.matchesWeek(1)) continue;
        for (const auto& lesson : cp.lessons) {
            if (lesson.slotIndex == row && lesson.isActive) {
                switch (role) {
                case Qt::DisplayRole:
                case SubjectNameRole:
                    return m_subjectNames.value(lesson.subjectId, "—");
                case SubjectIdRole:
                    return lesson.subjectId;
                case TimeSlotIdRole:
                    return ts.id;
                case WeekDayRole:
                    return weekDay;
                case SlotIndexRole:
                    return row;
                case IsMultiWeekRole:
                    return lesson.weekCountDivTotal > 1;
                }
            }
        }
    }
    return {};  // 空格子
}

QVariant ScheduleModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        static const char* days[] = {"一", "二", "三", "四", "五", "六", "日"};
        return QStringLiteral("周%1").arg(days[section]);
    } else {
        if (section < m_timeSlots.size()) {
            return m_timeSlots[section].startTime.toString("HH:mm");
        }
    }
    return {};
}

bool ScheduleModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) return false;
    // TODO: 实现修改科目逻辑
    return true;
}

Qt::ItemFlags ScheduleModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return {};
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

Lesson ScheduleModel::lessonAt(int row, int col) const {
    Lesson result;
    int weekDay = col + 1;
    const auto activePlans = m_profile.activeClassPlans();
    for (const auto& cp : activePlans) {
        if (cp.weekDay != weekDay) continue;
        for (const auto& lesson : cp.lessons) {
            if (lesson.slotIndex == row) {
                return lesson;
            }
        }
    }
    return result;
}

bool ScheduleModel::swap(int rowA, int colA, int rowB, int colB) {
    // TODO: 实现交换逻辑
    return false;
}
