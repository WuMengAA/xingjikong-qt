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

    rebuild();
}

// 重建「按天时间表」缓存 + 行数
void ScheduleModel::rebuild() {
    m_daySlots.clear();
    int maxClasses = 0;
    const auto plans = m_profile.activeClassPlans();
    for (const auto& cp : plans) {
        if (cp.lessons.size() > maxClasses) maxClasses = cp.lessons.size();
    }
    // 无课表群时回落：用全局上课时间点数量兜底，保证网格不塌成 0 行
    if (maxClasses == 0) maxClasses = m_timeSlots.size();
    m_rowCount = maxClasses;
    // daySlots 惰性构建（slotsForDay 里做），这里只清缓存
    m_daySlots.clear();
}

// 某天自己时间表的上课段序列
const QList<TimeSlot>& ScheduleModel::slotsForDay(int weekDay) const {
    auto cached = m_daySlots.find(weekDay);
    if (cached != m_daySlots.end()) return cached.value();

    QList<TimeSlot> out;
    // 找该天在当前群里的 ClassPlan，读它的 TimeLayoutId
    QString layoutId;
    const auto plans = m_profile.activeClassPlans();
    for (const auto& cp : plans) {
        if (cp.weekDay != weekDay) continue;
        layoutId = cp.timeLayoutId;
        if (!layoutId.isEmpty()) break;
    }
    if (!layoutId.isEmpty()) {
        // 真机档案：timeSlots 的 key 形如 "<layoutId>/<i>"
        for (auto it = m_profile.timeSlots.begin(); it != m_profile.timeSlots.end(); ++it) {
            if (!it.key().startsWith(layoutId + QLatin1Char('/'))) continue;
            if (it->timeType == 0 && it->isActive) out.append(it.value());
        }
        std::sort(out.begin(), out.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
    }
    // 无专属表（CSES 导入/简单档案）：回落全局表
    if (out.isEmpty()) out = m_timeSlots;
    m_daySlots.insert(weekDay, out);
    return m_daySlots[weekDay];
}

const TimeSlot* ScheduleModel::stageFor(int weekDay, int classIndex) const {
    // ⚠️ 变量名不能叫 slots —— Qt 的 Q_SLOTS 宏会把它替换掉（C2059）
    const QList<TimeSlot>& dayList = slotsForDay(weekDay);
    if (classIndex < 0 || classIndex >= dayList.size()) return nullptr;
    return &dayList[classIndex];
}

int ScheduleModel::rowCount(const QModelIndex&) const {
    return m_rowCount;
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

    // 找到该天该节次的 ClassPlan（row = 本天第几节，与 Classes[] 下标同义）
    const int weekDay = col + 1;  // 1=周一
    const TimeSlot* stage = stageFor(weekDay, row);

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
                    return stage ? stage->id : QString();
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
        // 行头是「第N节」而不是时间：各天时间表不同，一个全局时间对不上任何一天。
        // 需要具体时间的地方用 cellRangeText(row, col)（按该天自己那张表取）。
        if (section >= 0 && section < m_rowCount) {
            return QStringLiteral("第%1节").arg(section + 1);
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

// ── 以下实现 2026-11-04 从 schedule_model.h 搬来（头文件超 300 行红线）──

// 从 CSES JSON 导入（替换科目/课表/时间点，重建课表群）
bool ScheduleModel::importCses(const QString& path) {
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
    rebuild();
    emit fullDataChanged();
    return true;
}

// 导出为 CSES JSON
bool ScheduleModel::exportCses(const QString& path) const {
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
bool ScheduleModel::exportCsv(const QString& path) const {
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
bool ScheduleModel::importCsv(const QString& path) {
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
    rebuild();
    emit fullDataChanged();
    return true;
}

// ── 按天时间表查询（2026-11-04 加；配合 row = 本天第几节）──────────────


QString ScheduleModel::cellStartText(int row, int col) const {
    if (col < 0 || col > 6) return {};
    const TimeSlot* s = stageFor(col + 1, row);
    return s ? s->startTime.toString("HH:mm") : QString();
}

QString ScheduleModel::cellEndText(int row, int col) const {
    if (col < 0 || col > 6) return {};
    const TimeSlot* s = stageFor(col + 1, row);
    return s ? s->endTime.toString("HH:mm") : QString();
}

QString ScheduleModel::cellRangeText(int row, int col) const {
    const QString a = cellStartText(row, col);
    if (a.isEmpty()) return {};
    return a + QStringLiteral("-") + cellEndText(row, col);
}

QString ScheduleModel::cellAbbr(int row, int col) const {
    const QString sid = data(index(row, col), SubjectIdRole).toString();
    if (sid.isEmpty()) return {};
    const auto it = m_profile.subjects.find(sid);
    if (it == m_profile.subjects.end()) return {};
    return it->simplifiedName.isEmpty() ? it->name.left(1) : it->simplifiedName;
}

int ScheduleModel::cellStageType(int row, int col) const {
    if (col < 0 || col > 6) return 2;
    return stageFor(col + 1, row) ? 0 : 2;
}
