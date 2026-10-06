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
    Q_INVOKABLE bool importCses(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = f.readAll();
        f.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) return false;
        const QJsonObject root = doc.object();

        // 清空现有
        m_profile.subjects.clear();
        m_profile.timeSlots.clear();
        m_profile.classPlans.clear();
        m_profile.classPlanGroups.clear();

        // 科目
        const auto subArr = root["subjects"].toArray();
        for (const QJsonValue& v : subArr) {
            Subject s;
            s.id = v.toObject()["id"].toString();
            if (s.id.isEmpty()) s.id = QUuid::createUuid().toString();
            s.name = v.toObject()["name"].toString();
            s.simplifiedName = v.toObject()["shortName"].toString();
            m_profile.subjects[s.id] = s;
        }
        // 时间点
        const auto ttArr = root["timetable"].toArray();
        for (const QJsonValue& v : ttArr) {
            TimeSlot ts;
            ts.id = v.toObject()["id"].toString();
            if (ts.id.isEmpty()) ts.id = QUuid::createUuid().toString();
            ts.name = v.toObject()["name"].toString();
            ts.startTime = QTime::fromString(v.toObject()["start"].toString(), "HH:mm");
            ts.endTime = QTime::fromString(v.toObject()["end"].toString(), "HH:mm");
            ts.timeType = 0;
            ts.isActive = true;
            m_profile.timeSlots[ts.id] = ts;
        }
        // 课表
        const auto schedArr = root["schedules"].toArray();
        ClassPlanGroup grp;
        grp.id = QUuid::createUuid().toString();
        grp.name = "导入课表";
        grp.isActive = true;
        for (const QJsonValue& v : schedArr) {
            ClassPlan cp;
            cp.id = v.toObject()["id"].toString();
            if (cp.id.isEmpty()) cp.id = QUuid::createUuid().toString();
            cp.name = v.toObject()["name"].toString();
            cp.weekDay = v.toObject()["day"].toInt(1);
            cp.weekCountDiv = 1; cp.weekCountDivTotal = 1; cp.isActive = true;
            int si = 0;
            const auto items = v.toObject()["items"].toArray();
            for (const QJsonValue& it : items) {
                Lesson l;
                l.subjectId = it.toObject()["subjectId"].toString();
                l.weekDay = cp.weekDay; l.slotIndex = si++;
                l.weekCountDiv = 1; l.weekCountDivTotal = 1; l.isActive = true;
                cp.lessons.append(l);
            }
            m_profile.classPlans[cp.id] = cp;
            grp.classPlanIds.append(cp.id);
        }
        m_profile.classPlanGroups[grp.id] = grp;
        m_profile.selectedClassPlanGroupId = grp.id;

        // 刷新内部缓存 + 通知 UI
        m_timeSlots.clear();
        for (const auto& ts : m_profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) m_timeSlots.append(ts);
        }
        std::sort(m_timeSlots.begin(), m_timeSlots.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
        m_subjectNames.clear();
        const auto subK = m_profile.subjects.keys();
        for (const QString& k : subK) {
            const Subject& s = m_profile.subjects.value(k);
            m_subjectNames[k] = s.name.isEmpty() ? s.simplifiedName : s.name;
        }
        emit fullDataChanged();
        return true;
    }
    // 导出为 CSES JSON
    Q_INVOKABLE bool exportCses(const QString& path) const {
        if (path.isEmpty()) return false;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        QJsonObject root;
        QJsonArray subjectsArr;
        const auto subKeys = m_profile.subjects.keys();
        for (const QString& k : subKeys) {
            const Subject& s = m_profile.subjects.value(k);
            QJsonObject o; o["id"] = s.id; o["name"] = s.name; o["shortName"] = s.simplifiedName;
            subjectsArr.append(o);
        }
        root["subjects"] = subjectsArr;
        QJsonArray ttArr;
        const auto tsKeys = m_profile.timeSlots.keys();
        for (const QString& k : tsKeys) {
            const TimeSlot& ts = m_profile.timeSlots.value(k);
            if (ts.timeType != 0) continue;
            QJsonObject o; o["id"] = ts.id; o["name"] = ts.name;
            o["start"] = ts.startTime.toString("HH:mm"); o["end"] = ts.endTime.toString("HH:mm");
            ttArr.append(o);
        }
        root["timetable"] = ttArr;
        QJsonArray schedArr;
        const auto cpKeys = m_profile.classPlans.keys();
        for (const QString& k : cpKeys) {
            const ClassPlan& cp = m_profile.classPlans.value(k);
            QJsonObject o; o["id"] = cp.id; o["name"] = cp.name; o["day"] = cp.weekDay;
            QJsonArray items;
            for (const Lesson& l : cp.lessons) {
                QJsonObject it; it["subjectId"] = l.subjectId; items.append(it);
            }
            o["items"] = items;
            schedArr.append(o);
        }
        root["schedules"] = schedArr;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
        return true;
    }

    // 导出 CSV 课表（UTF-8 BOM，Excel 可直接打开）
    // 格式: 星期,节次,科目,开始,结束
    Q_INVOKABLE bool exportCsv(const QString& path) const {
        if (path.isEmpty()) return false;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        // UTF-8 BOM（Excel 认）
        f.write("\xEF\xBB\xBF");
        // 节次时间表（上课时间点，按下标）
        QList<TimeSlot> slotTable;
        for (const TimeSlot& ts : m_profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) slotTable.append(ts);
        }
        std::sort(slotTable.begin(), slotTable.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
        static const char* days[] = {"一","二","三","四","五","六","日"};
        QByteArray out = QByteArray("星期,节次,科目,开始,结束\r\n");
        const auto cpKeys = m_profile.classPlans.keys();
        for (const QString& k : cpKeys) {
            const ClassPlan& cp = m_profile.classPlans.value(k);
            if (cp.weekDay < 1 || cp.weekDay > 7) continue;
            for (const Lesson& l : cp.lessons) {
                if (!l.isActive) continue;
                if (l.slotIndex < 0 || l.slotIndex >= slotTable.size()) continue;
                const TimeSlot& ts = slotTable[l.slotIndex];
                const auto subIt = m_profile.subjects.find(l.subjectId);
                QString sub = (subIt != m_profile.subjects.end()) ? subIt->name : QStringLiteral("—");
                out += QByteArray("周") + days[cp.weekDay - 1];
                out += "," + QByteArray::number(l.slotIndex + 1);
                out += "," + sub.toUtf8();
                out += "," + ts.startTime.toString("HH:mm").toUtf8();
                out += "," + ts.endTime.toString("HH:mm").toUtf8();
                out += "\r\n";
            }
        }
        f.write(out);
        f.close();
        return true;
    }

    // 导入 CSV 课表（与 exportCsv 同格式）
    Q_INVOKABLE bool importCsv(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray raw = f.readAll();
        f.close();
        QByteArray data = raw;
        if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);  // 去 BOM
        const QString text = QString::fromUtf8(data);

        // 清空并重建：把 CSV 转成「简化档案」结构
        m_profile.classPlans.clear();
        m_profile.classPlanGroups.clear();
        // 保留科目表不动（CSV 只含课表格子）；无科目时建一个占位
        if (m_profile.subjects.isEmpty()) {
            Subject ph; ph.id = QUuid::createUuid().toString();
            ph.name = QStringLiteral("（未命名）"); ph.simplifiedName = QStringLiteral("?");
            m_profile.subjects[ph.id] = ph;
        }

        // 解析行：星期,节次,科目,开始,结束
        ClassPlanGroup grp;
        grp.id = QUuid::createUuid().toString();
        grp.name = QStringLiteral("CSV 导入");
        grp.isActive = true;

        const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (int li = 1; li < lines.size(); ++li) {  // 跳过表头
            const QStringList cells = lines[li].split(',');
            if (cells.size() < 5) continue;
            QString dayText = cells[0].trimmed();
            dayText.remove(QStringLiteral("周"));
            int weekday = dayText.toInt();
            if (weekday <= 0) {
                // 中文数字
                const QString cn = QStringLiteral("一二三四五六日");
                int idx = cn.indexOf(dayText);
                if (idx >= 0) weekday = idx + 1;
            }
            if (weekday < 1 || weekday > 7) continue;
            const int slot = cells[1].trimmed().toInt() - 1;
            if (slot < 0) continue;
            const QString subName = cells[2].trimmed();
            const QTime start = QTime::fromString(cells[3].trimmed(), "HH:mm");
            const QTime end = QTime::fromString(cells[4].trimmed(), "HH:mm");
            if (!start.isValid() || !end.isValid()) continue;

            // 找到/创建该天的 ClassPlan
            QString cpId;
            for (auto it = m_profile.classPlans.begin(); it != m_profile.classPlans.end(); ++it) {
                if (it->weekDay == weekday) { cpId = it.key(); break; }
            }
            if (cpId.isEmpty()) {
                ClassPlan cp;
                cp.id = QUuid::createUuid().toString();
                cp.name = QStringLiteral("周%1").arg(dayText);
                cp.weekDay = weekday;
                cp.weekCountDiv = 1; cp.weekCountDivTotal = 1; cp.isActive = true;
                m_profile.classPlans[cp.id] = cp;
                cpId = cp.id;
                grp.classPlanIds.append(cpId);
            }
            ClassPlan& cp = m_profile.classPlans[cpId];

            // 科目名 → id（找不到则新建）
            QString sid;
            for (auto it = m_profile.subjects.begin(); it != m_profile.subjects.end(); ++it) {
                if (it->name == subName) { sid = it.key(); break; }
            }
            if (sid.isEmpty()) {
                Subject s; s.id = QUuid::createUuid().toString();
                s.name = subName; s.simplifiedName = subName.left(1);
                m_profile.subjects[s.id] = s;
                sid = s.id;
            }

            Lesson l;
            l.subjectId = sid;
            l.weekDay = weekday;
            l.slotIndex = slot;
            l.weekCountDiv = 1; l.weekCountDivTotal = 1; l.isActive = true;
            cp.lessons.append(l);
        }

        m_profile.classPlanGroups[grp.id] = grp;
        m_profile.selectedClassPlanGroupId = grp.id;

        // 刷新缓存 + 通知 UI
        m_timeSlots.clear();
        for (const auto& ts : m_profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) m_timeSlots.append(ts);
        }
        std::sort(m_timeSlots.begin(), m_timeSlots.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
        m_subjectNames.clear();
        const auto subK = m_profile.subjects.keys();
        for (const QString& k : subK) {
            const Subject& s = m_profile.subjects.value(k);
            m_subjectNames[k] = s.name.isEmpty() ? s.simplifiedName : s.name;
        }
        emit fullDataChanged();
        return true;
    }

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

    Profile& m_profile;
    QList<TimeSlot> m_timeSlots;              // 排序后的时间点列表
    QMap<QString, QString> m_subjectNames;    // SubjectId -> SubjectName
    QMap<QString, int> m_weekDayToCol;         // WeekDay(1-7) -> 列索引(0-6)
    int m_currentWeek = 1;                    // 当前周次（多周轮换）
    QString m_semesterStart = QStringLiteral("09-01");  // 学期起点（MM-dd，QML 可配）
};
