#pragma once
#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QMap>
#include "profile.h"

// 课表网格模型：行=时间点，列=周几(1-7)
// data() 返回某格子的科目名；支持调课（改科目/交换/清空）
class ScheduleModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Role {
        SubjectNameRole = Qt::UserRole + 1,
        SubjectIdRole,
        TimeSlotIdRole,
        WeekDayRole,
        SlotIndexRole,
        IsMultiWeekRole,
    };

    explicit ScheduleModel(Profile& profile, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // 获取某格子的课表条目
    Lesson lessonAt(int row, int col) const;
    // 交换两个格子的科目
    bool swap(int rowA, int colA, int rowB, int colB);

    // 获取时间点列表
    const QList<TimeSlot>& timeSlots() const { return m_timeSlots; }
    // 获取科目 ID -> 科目名映射
    const QMap<QString, QString>& subjectNames() const { return m_subjectNames; }

    // ---- QML 可调用（编辑）----
    // 设置某格科目（subjectId 为空 = 清空）
    Q_INVOKABLE bool setCellSubject(int row, int col, const QString& subjectId) {
        if (row < 0 || row >= rowCount()) return false;
        if (col < 0 || col >= columnCount()) return false;
        return setData(index(row, col), subjectId, SubjectIdRole);
    }
    // 交换两格
    Q_INVOKABLE bool swapCells(int rowA, int colA, int rowB, int colB) {
        return swap(rowA, colA, rowB, colB);
    }

    // ---- QML 可调用（显示）----
    // 单元格科目名（col: 0=周一列）
    Q_INVOKABLE QString cellText(int row, int col) const {
        return data(index(row, col), SubjectNameRole).toString();
    }
    // 行头（时间）
    Q_INVOKABLE QString rowHeader(int row) const {
        return headerData(row, Qt::Vertical).toString();
    }
    // 列头（周几）
    Q_INVOKABLE QString colHeader(int col) const {
        return headerData(col, Qt::Horizontal).toString();
    }

private:
    // 找到某格子的 ClassPlan 引用（可写）
    bool findLessonRef(int row, int col, ClassPlan** outPlan, Lesson** outLesson);

    Profile& m_profile;
    QList<TimeSlot> m_timeSlots;              // 排序后的时间点列表
    QMap<QString, QString> m_subjectNames;    // SubjectId -> SubjectName
    QMap<QString, int> m_weekDayToCol;         // WeekDay(1-7) -> 列索引(0-6)
};
