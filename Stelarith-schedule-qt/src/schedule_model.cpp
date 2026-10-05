#include "schedule_model.h"
#include <algorithm>

ScheduleModel::ScheduleModel(Profile& profile, QObject* parent)
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
        if (cp.weekDay != weekDay || !cp.matchesWeek(m_currentWeek)) continue;
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

// 找到某格子的可写 ClassPlan + Lesson
bool ScheduleModel::findLessonRef(int row, int col, ClassPlan** outPlan, Lesson** outLesson) {
    int weekDay = col + 1;
    if (m_profile.selectedClassPlanGroupId.isEmpty()) return false;

    // 直接在可变的 m_profile.classPlans 里找（跳过当前激活课表群内的）
    const auto group = m_profile.classPlanGroups.value(m_profile.selectedClassPlanGroupId);
    for (const QString& cpId : group.classPlanIds) {
        auto it = m_profile.classPlans.find(cpId);
        if (it == m_profile.classPlans.end()) continue;
        ClassPlan& cp = it.value();
        if (cp.weekDay != weekDay || !cp.matchesWeek(m_currentWeek)) continue;
        for (Lesson& lesson : cp.lessons) {
            if (lesson.slotIndex == row && lesson.isActive) {
                *outPlan = &cp;
                *outLesson = &lesson;
                return true;
            }
        }
    }
    return false;
}

bool ScheduleModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) return false;
    int row = index.row();
    int col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= 7) return false;

    QString newSubjectId;
    if (role == SubjectIdRole) {
        newSubjectId = value.toString();
    } else if (role == Qt::EditRole) {
        // 传科目名：先查名→ID 映射
        newSubjectId = value.toString();
        for (auto it = m_subjectNames.begin(); it != m_subjectNames.end(); ++it) {
            if (it.value() == newSubjectId) { newSubjectId = it.key(); break; }
        }
    } else {
        return false;
    }

    // 清空：subjectId 为空串
    ClassPlan* plan = nullptr;
    Lesson* lesson = nullptr;
    if (!findLessonRef(row, col, &plan, &lesson)) {
        // 格子原本是空的，若 newSubjectId 非空则需新建 Lesson
        if (newSubjectId.isEmpty()) return false;
        // 在对应 ClassPlan 里新建条目
        int weekDay = col + 1;
        auto group = m_profile.classPlanGroups.value(m_profile.selectedClassPlanGroupId);
        for (const QString& cpId : group.classPlanIds) {
            auto it = m_profile.classPlans.find(cpId);
            if (it == m_profile.classPlans.end()) continue;
            ClassPlan& cp = it.value();
            if (cp.weekDay != weekDay || !cp.matchesWeek(1)) continue;
            Lesson l;
            l.subjectId = newSubjectId;
            l.weekDay = weekDay;
            l.slotIndex = row;
            l.weekCountDiv = 1;
            l.weekCountDivTotal = 1;
            l.isActive = true;
            cp.lessons.append(l);
            emit dataChanged(index, index);
            return true;
        }
        return false;
    }

    lesson->subjectId = newSubjectId;
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags ScheduleModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return {};
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
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
    if (rowA < 0 || rowA >= rowCount() || colA < 0 || colA >= 7) return false;
    if (rowB < 0 || rowB >= rowCount() || colB < 0 || colB >= 7) return false;

    // 拿两格子的当前科目
    QString a = data(index(rowA, colA), SubjectIdRole).toString();
    QString b = data(index(rowB, colB), SubjectIdRole).toString();

    // 两个空格交换无意义
    if (a.isEmpty() && b.isEmpty()) return false;

    // 先清空 A 再填 B，避免 findLessonRef 找到自己
    setData(index(rowA, colA), a.isEmpty() ? b : a, SubjectIdRole);  // 暂存
    // 正确做法：A 赋 B 的值，B 赋 A 的值
    bool okA = setData(index(rowA, colA), b, SubjectIdRole);
    bool okB = setData(index(rowB, colB), a, SubjectIdRole);
    if (!okA || !okB) {
        // 回滚
        setData(index(rowA, colA), a, SubjectIdRole);
        setData(index(rowB, colB), b, SubjectIdRole);
        return false;
    }
    return true;
}
