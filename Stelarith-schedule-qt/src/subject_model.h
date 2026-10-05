#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
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
    bool isReferenced(const QString& subjectId) const;

    // 更新整个科目列表
    void setSubjects(const QMap<QString, Subject>& subjects);
    const QList<Subject>& subjects() const { return m_subjects; }

private:
    QList<Subject> m_subjects;
    bool m_isUpdating = false;  // 防重入
};
