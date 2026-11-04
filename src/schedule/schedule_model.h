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
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUuid>
#include "profile.h"

// 课表网格模型：行=时间点，列=周几(1-7)
// data() 返回某格子的科目名；支持调课（改科目/交换/清空）；支持多周轮换（currentWeek）
class ScheduleModel : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(int currentWeek READ currentWeek WRITE setCurrentWeek NOTIFY currentWeekChanged)
    Q_PROPERTY(QString semesterStart READ semesterStart WRITE setSemesterStart NOTIFY currentWeekChanged)
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
        rebuild();
        emit currentWeekChanged();
        emit fullDataChanged();
    }
    // 学期起点（默认 9/1，QML 可配置；格式 "MM-dd"）
    QString semesterStart() const { return m_semesterStart; }
    void setSemesterStart(const QString& mmdd) {
        if (mmdd == m_semesterStart) return;
        m_semesterStart = mmdd;
        emit currentWeekChanged();
        emit fullDataChanged();
    }
    // 按日期算当前周次（学期第一周 = 1）
    static int weekFromDate(const QDate& date, const QString& semesterStart = QStringLiteral("09-01")) {
        const QDate def(date.year(), 9, 1);
        QDate start = def;
        const QStringList mm = semesterStart.split('-');
        if (mm.size() == 2) {
            const int mon = mm[0].toInt(), day = mm[1].toInt();
            if (mon >= 1 && mon <= 12 && day >= 1 && day <= 31) start = QDate(date.year(), mon, day);
        }
        int days = start.daysTo(date);
        return (days >= 0) ? (days / 7) + 1 : 1;
    }
    // QML 可调版本（static 方法 QML 调不了，包一层）
    Q_INVOKABLE int weekFromDateQml(const QDate& date, const QString& semesterStart = QStringLiteral("09-01")) const {
        return weekFromDate(date, semesterStart);
    }

    // ---- 课表群 ----
    // 课表群名列表（QML 下拉用）
    Q_INVOKABLE QStringList groupNames() const {
        QStringList names;
        const auto keys = m_profile.classPlanGroups.keys();
        for (const QString& k : keys) {
            names.append(m_profile.classPlanGroups.value(k).name);
        }
        return names;
    }
    // 当前课表群索引（-1 无群）
    Q_INVOKABLE int currentGroupIndex() const {
        const auto keys = m_profile.classPlanGroups.keys();
        return keys.indexOf(m_profile.selectedClassPlanGroupId);
    }
    // 切换课表群
    Q_INVOKABLE bool selectGroup(int index) {
        const auto keys = m_profile.classPlanGroups.keys();
        if (index < 0 || index >= keys.size()) return false;
        const QString id = keys.at(index);
        if (id == m_profile.selectedClassPlanGroupId) return false;
        m_profile.selectedClassPlanGroupId = id;
        rebuild();              // 换群 ⇒ 各天的 TimeLayout 也可能变，必须重算行数与缓存
        emit fullDataChanged();
        return true;
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
    // 单元格科目 ID（供撤销记录）
    Q_INVOKABLE QString cellSubjectId(int row, int col) const {
        return data(index(row, col), SubjectIdRole).toString();
    }
    // 行头（时间）
    Q_INVOKABLE QString rowHeader(int row) const {
        return headerData(row, Qt::Vertical).toString();
    }
    // ── 按天时间表（2026-11-04）────────────────────────────────────
    // 行 = 本天第几节（与 ClassPlan.Classes[] 下标同义），不是全局时间点序号。
    // 每天可能用不同 TimeLayout（周一~四 12 节、周五 8 节、周日 3 节），
    // 故起止时间必须逐格按「那天自己那张表」取，不能用一张全局行表。
    Q_INVOKABLE QString cellRangeText(int row, int col) const;  // "07:25-08:05"
    Q_INVOKABLE QString cellStartText(int row, int col) const;
    Q_INVOKABLE QString cellEndText(int row, int col) const;
    Q_INVOKABLE QString cellAbbr(int row, int col) const;       // 科目简称（格内显示）
    // 该格的表段落类型：0=上课 1=课间 2=本天无此节
    Q_INVOKABLE int cellStageType(int row, int col) const;

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
    // 从 CSES JSON 导入（替换科目/课表/时间点，重建课表群）
    Q_INVOKABLE bool importCses(const QString& path);

    // 导出为 CSES JSON
    Q_INVOKABLE bool exportCses(const QString& path) const;

    // 导出 CSV 课表（UTF-8 BOM）；格式: 星期,节次,科目,开始,结束
    Q_INVOKABLE bool exportCsv(const QString& path) const;

    // 导入 CSV 课表（与 exportCsv 同格式）
    Q_INVOKABLE bool importCsv(const QString& path);

private:
    // 找到某格子的 ClassPlan 引用（可写）
    bool findLessonRef(int row, int col, ClassPlan** outPlan, Lesson** outLesson);

signals:
    void currentWeekChanged();

private:
    // 整表刷新（周次/课表群切换后调用）
    void fullDataChanged() {
        beginResetModel();
        endResetModel();
    }

    // 重建派生缓存（构造 / 切群 / 切周 / 导入后调用）
    void rebuild();
    // 某天自己时间表里的「上课段」序列（按开始时间排序）；无专属表时回落全局表
    const QList<TimeSlot>& slotsForDay(int weekDay) const;
    // 某天某一节课的表段（越界返回 nullptr）
    const TimeSlot* stageFor(int weekDay, int classIndex) const;

    Profile& m_profile;
    QList<TimeSlot> m_timeSlots;              // 全局上课时间点（无专属表时的回落）
    QList<TimeSlot> m_empty;
    mutable QMap<int, QList<TimeSlot>> m_daySlots;  // WeekDay(1-7) -> 本天上课段
    int m_rowCount = 0;                       // 网格行数 = 各天最大节数
    QMap<QString, QString> m_subjectNames;    // SubjectId -> SubjectName
    QMap<QString, int> m_weekDayToCol;         // WeekDay(1-7) -> 列索引(0-6)
    int m_currentWeek = 1;                    // 当前周次（多周轮换）
    QString m_semesterStart = QStringLiteral("09-01");  // 学期起点（MM-dd，QML 可配）
};
