#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUuid>
#include "profile.h"

// 科目列表模型：供 QML ListModel 使用
class SubjectModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        SimplifiedNameRole,
        TeacherRole,
        RoomRole,
    };

    explicit SubjectModel(QObject* parent = nullptr);
    explicit SubjectModel(const QMap<QString, Subject>& subjects, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    // 获取/设置科目
    Subject getSubject(int row) const;
    bool setSubjectName(int row, const QString& name);
    bool setSubjectTeacher(int row, const QString& teacher);
    bool setSubjectRoom(int row, const QString& room);
    int addSubject(const Subject& subject);
    bool removeSubject(int row);
    // 是否被课表引用（有引用则禁止删除）
    bool isReferenced(const QString& subjectId) const;

    // 绑定档案引用（用于 isReferenced 检查课表引用）
    void setProfileRef(const Profile* profile) { m_profileRef = profile; }

    // 更新整个科目列表
    void setSubjects(const QMap<QString, Subject>& subjects);
    const QList<Subject>& subjects() const { return m_subjects; }

    // ---- QML 可调用 ----
    Q_INVOKABLE int count() const { return m_subjects.size(); }
    Q_INVOKABLE QString nameAt(int row) const {
        return (row >= 0 && row < m_subjects.size()) ? m_subjects[row].name : QString();
    }
    Q_INVOKABLE bool addSubjectQml(const QString& name) {
        Subject s;
        s.id = QUuid::createUuid().toString();
        s.name = name;
        s.simplifiedName = name.left(1);
        return addSubject(s) >= 0;
    }
    Q_INVOKABLE bool removeAt(int row) {
        return removeSubject(row);
    }
    Q_INVOKABLE bool renameAt(int row, const QString& newName) {
        if (newName.trimmed().isEmpty()) return false;
        return setSubjectName(row, newName);
    }

private:
    QList<Subject> m_subjects;
    const Profile* m_profileRef = nullptr;  // 只读引用，用于引用检查
    bool m_isUpdating = false;  // 防重入
};
