#pragma once
#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QMap>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QJsonDocument>
#include "profile.h"

// 课表网格模型：行=时间点，列=周几(1-7)
// data() 返回某格子的科目名；支持调课（改科目/交换/清空）；支持多周轮换（currentWeek）
class ScheduleModel : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(int currentWeek READ currentWeek WRITE setCurrentWeek NOTIFY currentWeekChanged)
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

    // 当前周次（多周轮换；默认按日期算，QML 可覆盖）
    int currentWeek() const { return m_currentWeek; }
    void setCurrentWeek(int week) {
        if (week == m_currentWeek) return;
        m_currentWeek = week;
        emit currentWeekChanged();
        emit dataChanged(index(0,0), index(rowCount()-1, columnCount()-1));
    }
    // 按日期算当前周次（学期第一周 = 1）
    static int weekFromDate(const QDate& date) {
        // 简化：9 月 1 日所在周为第 1 周
        QDate start(date.year(), 9, 1);
        int days = start.daysTo(date);
        return (days >= 0) ? (days / 7) + 1 : 1;
    }

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
    // 当前档案 JSON（QML 调试/导出用）
    Q_INVOKABLE QString profileJson() const {
        return QString::fromUtf8(QJsonDocument(m_profile.toJson()).toJson(QJsonDocument::Indented));
    }
    // 保存档案到文件（供 QML 保存按钮调用）
    Q_INVOKABLE bool saveTo(const QString& path) const {
        if (path.isEmpty()) return false;
        QDir dir = QFileInfo(path).absoluteDir();
        if (!dir.exists()) dir.mkpath(".");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(QJsonDocument(m_profile.toJson()).toJson(QJsonDocument::Indented));
        f.close();
        return true;
    }

private:
    // 找到某格子的 ClassPlan 引用（可写）
    bool findLessonRef(int row, int col, ClassPlan** outPlan, Lesson** outLesson);

signals:
    void currentWeekChanged();

private:
    Profile& m_profile;
    QList<TimeSlot> m_timeSlots;              // 排序后的时间点列表
    QMap<QString, QString> m_subjectNames;    // SubjectId -> SubjectName
    QMap<QString, int> m_weekDayToCol;         // WeekDay(1-7) -> 列索引(0-6)
    int m_currentWeek = 1;                    // 当前周次（多周轮换）
};
