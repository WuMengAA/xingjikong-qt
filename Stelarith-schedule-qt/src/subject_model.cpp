#include "subject_model.h"
#include <algorithm>

SubjectModel::SubjectModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

SubjectModel::SubjectModel(const QMap<QString, Subject>& subjects, QObject* parent)
    : QAbstractListModel(parent)
{
    for (const auto& s : subjects) {
        m_subjects.append(s);
    }
    // 按名称排序
    std::sort(m_subjects.begin(), m_subjects.end(),
              [](const Subject& a, const Subject& b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
}

int SubjectModel::rowCount(const QModelIndex&) const {
    return m_subjects.size();
}

QVariant SubjectModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_subjects.size())
        return {};

    const Subject& s = m_subjects[index.row()];
    switch (role) {
    case IdRole:
        return s.id;
    case NameRole:
        return s.name;
    case SimplifiedNameRole:
        return s.simplifiedName;
    case TeacherRole:
        return s.teacher;
    case RoomRole:
        return s.room;
    default:
        return {};
    }
}

bool SubjectModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_subjects.size())
        return false;

    if (m_isUpdating) return true;
    m_isUpdating = true;

    Subject& s = m_subjects[index.row()];
    bool changed = false;

    switch (role) {
    case NameRole:
        s.name = value.toString();
        changed = true;
        break;
    case SimplifiedNameRole:
        s.simplifiedName = value.toString();
        changed = true;
        break;
    case TeacherRole:
        s.teacher = value.toString();
        changed = true;
        break;
    case RoomRole:
        s.room = value.toString();
        changed = true;
        break;
    }

    if (changed) {
        emit dataChanged(index, index);
    }

    m_isUpdating = false;
    return changed;
}

Subject SubjectModel::getSubject(int row) const {
    if (row < 0 || row >= m_subjects.size()) return {};
    return m_subjects[row];
}

bool SubjectModel::setSubjectName(int row, const QString& name) {
    if (row < 0 || row >= m_subjects.size()) return false;
    QModelIndex index = createIndex(row, 0);
    m_subjects[row].name = name;
    emit dataChanged(index, index);
    return true;
}

bool SubjectModel::setSubjectTeacher(int row, const QString& teacher) {
    if (row < 0 || row >= m_subjects.size()) return false;
    QModelIndex index = createIndex(row, 0);
    m_subjects[row].teacher = teacher;
    emit dataChanged(index, index);
    return true;
}

bool SubjectModel::setSubjectRoom(int row, const QString& room) {
    if (row < 0 || row >= m_subjects.size()) return false;
    QModelIndex index = createIndex(row, 0);
    m_subjects[row].room = room;
    emit dataChanged(index, index);
    return true;
}

int SubjectModel::addSubject(const Subject& subject) {
    beginInsertRows(QModelIndex(), m_subjects.size(), m_subjects.size());
    m_subjects.append(subject);
    // 按名称排序
    std::sort(m_subjects.begin(), m_subjects.end(),
              [](const Subject& a, const Subject& b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    endInsertRows();
    // 找到新插入项的索引
    for (int i = 0; i < m_subjects.size(); ++i) {
        if (m_subjects[i].id == subject.id) return i;
    }
    return -1;
}

bool SubjectModel::removeSubject(int row) {
    if (row < 0 || row >= m_subjects.size()) return false;
    const auto& sub = m_subjects[row];
    if (isReferenced(sub.id)) {
        return false;  // 被引用的科目不能删除
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_subjects.removeAt(row);
    endRemoveRows();
    return true;
}

bool SubjectModel::isReferenced(const QString& subjectId) const {
    // TODO: 检查是否有 ClassPlan 引用该科目
    // 需要传入 Profile 引用才能检查
    return false;
}

void SubjectModel::setSubjects(const QMap<QString, Subject>& subjects) {
    beginResetModel();
    m_subjects.clear();
    for (const auto& s : subjects) {
        m_subjects.append(s);
    }
    std::sort(m_subjects.begin(), m_subjects.end(),
              [](const Subject& a, const Subject& b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    endResetModel();
}
