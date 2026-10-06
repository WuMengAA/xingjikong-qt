#pragma once
#include <QString>
#include <QList>
#include <QMap>
#include <QTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

// 课表条目：某天的某一节课
struct Lesson {
    QString subjectId;     // 科目 UUID
    int weekDay;           // 1=周一 ... 7=周日
    int slotIndex;         // 该天的第几节
    int weekCountDiv;      // 当前是第几周 (1-based)
    int weekCountDivTotal; // 轮换周期 (1=每周都有, 2=双周轮换)
    bool isActive = true;

    bool isActiveOnWeek(int todayWeek) const {
        if (weekCountDivTotal <= 1) return true;
        return ((todayWeek - 1) % weekCountDivTotal) == (weekCountDiv - 1);
    }
};

// 时间点：一节课的开始/结束，或课间休息
struct TimeSlot {
    QString id;
    QString name;          // 如 "第一节"
    QTime startTime;
    QTime endTime;
    int timeType = 0;      // 0=上课, 1=课间休息
    bool isActive = true;

    int durationMinutes() const {
        return startTime.secsTo(endTime) / 60;
    }

    // 序列化
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["Id"] = id;
        obj["Name"] = name;
        obj["StartTime"] = startTime.toString("HH:mm:ss");
        obj["EndTime"] = endTime.toString("HH:mm:ss");
        obj["TimeType"] = timeType;
        obj["IsActive"] = isActive;
        return obj;
    }

    static TimeSlot fromJson(const QJsonObject& obj) {
        TimeSlot ts;
        ts.id = obj["Id"].toString();
        ts.name = obj["Name"].toString();
        ts.startTime = QTime::fromString(obj["StartTime"].toString(), "HH:mm:ss");
        ts.endTime = QTime::fromString(obj["EndTime"].toString(), "HH:mm:ss");
        ts.timeType = obj["TimeType"].toInt();
        ts.isActive = obj["IsActive"].toBool(true);
        return ts;
    }

    // 真机 TimeLayout.Layouts 数组元素解析（下标即节次）
    static TimeSlot fromLayoutItem(int index, const QJsonObject& item) {
        TimeSlot ts;
        ts.id = QStringLiteral("layout-%1").arg(index);
        ts.name = item["BreakName"].toString();  // 上课段通常空，用时间点标记
        if (ts.name.isEmpty()) {
            ts.name = item["TimeType"].toInt() == 0
                ? QStringLiteral("第%1节").arg(index + 1)
                : QStringLiteral("课间%1").arg(index + 1);
        }
        ts.startTime = QTime::fromString(item["StartTime"].toString(), "HH:mm:ss");
        ts.endTime = QTime::fromString(item["EndTime"].toString(), "HH:mm:ss");
        ts.timeType = item["TimeType"].toInt();
        ts.isActive = true;
        return ts;
    }
};

// 课表：某天的课程安排
struct ClassPlan {
    QString id;
    QString name;          // 如 "周一"
    int weekDay = 1;
    int weekCountDiv = 1;
    int weekCountDivTotal = 1;
    bool isActive = true;
    QString timeLayoutId;  // 真机结构：指向 TimeLayout（节次时间表）
    QString associatedGroupId;  // 真机结构：所属课表群（反向关联）
    QList<Lesson> lessons;

    bool matchesWeek(int todayWeek) const {
        if (weekCountDivTotal <= 1) return true;
        return ((todayWeek - 1) % weekCountDivTotal) == (weekCountDiv - 1);
    }

    // 真机 ClassIsland 结构解析：WeekDay/轮换在 TimeRule，Classes 数组下标=节次
    static ClassPlan fromJsonReal(const QJsonObject& obj) {
        ClassPlan cp;
        cp.id = obj["Id"].toString();
        if (cp.id.isEmpty()) cp.id = obj.value("Id").toString();
        cp.name = obj["Name"].toString();
        cp.timeLayoutId = obj["TimeLayoutId"].toString();
        cp.associatedGroupId = obj["AssociatedGroup"].toString();
        cp.isActive = obj["IsActive"].toBool(obj["IsEnabled"].toBool(true));

        // TimeRule: {WeekDay, WeekCountDiv, WeekCountDivTotal, IsActive}
        const QJsonObject tr = obj["TimeRule"].toObject();
        cp.weekDay = tr["WeekDay"].toInt(1);
        // 真机语义：WeekCountDiv=0（0-based）+ WeekCountDivTotal=2 在真机里表示
        // 「每周都生效」（ClassIsland 的 0 表示周次无关，不是"第 1 周"）。
        // 只有 WeekCountDivTotal>1 且 WeekCountDiv>0 才算真正的多周轮换。
        cp.weekCountDivTotal = tr["WeekCountDivTotal"].toInt(1);
        cp.weekCountDiv = tr["WeekCountDiv"].toInt(0) + 1;
        if (cp.weekCountDivTotal <= 1 || tr["WeekCountDiv"].toInt(0) <= 0) {
            // 无轮换：每周都生效
            cp.weekCountDivTotal = 1;
            cp.weekCountDiv = 1;
        }

        // Classes 数组：下标即 slotIndex
        const QJsonArray clsArr = obj["Classes"].toArray();
        int slot = 0;
        for (const QJsonValue& v : clsArr) {
            const QJsonObject l = v.toObject();
            Lesson lesson;
            lesson.subjectId = l["SubjectId"].toString();
            lesson.weekDay = cp.weekDay;
            lesson.slotIndex = slot++;
            lesson.weekCountDiv = cp.weekCountDiv;
            lesson.weekCountDivTotal = cp.weekCountDivTotal;
            lesson.isActive = l["IsEnabled"].toBool(true);
            cp.lessons.append(lesson);
        }
        return cp;
    }
};

// 科目
struct Subject {
    QString id;
    QString name;
    QString simplifiedName;
    QString teacher;
    QString room;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["Id"] = id;
        obj["Name"] = name;
        obj["SimplifiedName"] = simplifiedName;
        obj["Teacher"] = teacher;
        obj["Room"] = room;
        return obj;
    }

    static Subject fromJson(const QJsonObject& obj) {
        Subject s;
        s.id = obj["Id"].toString();
        s.name = obj["Name"].toString();
        s.simplifiedName = obj["SimplifiedName"].toString();
        s.teacher = obj["Teacher"].toString();
        s.room = obj["Room"].toString();
        return s;
    }
};

// 课表群：多个 ClassPlan 的容器
struct ClassPlanGroup {
    QString id;
    QString name;
    QString color;
    QStringList classPlanIds;
    bool isActive = true;
};

// 档案：所有课表数据的容器
struct Profile {
    QString name = "Default";
    QString id;
    bool isActive = true;

    QMap<QString, TimeSlot> timeSlots;       // TimeSlotId -> TimeSlot
    QMap<QString, ClassPlan> classPlans;     // ClassPlanId -> ClassPlan
    QMap<QString, Subject> subjects;         // SubjectId -> Subject
    QMap<QString, ClassPlanGroup> classPlanGroups;  // GroupId -> Group

    QString selectedClassPlanGroupId;        // 当前激活的课表群

    // 获取当前激活课表群的所有 ClassPlan
    QList<ClassPlan> activeClassPlans() const {
        QList<ClassPlan> result;
        if (classPlanGroups.contains(selectedClassPlanGroupId)) {
            const auto& group = classPlanGroups[selectedClassPlanGroupId];
            for (const auto& cpId : group.classPlanIds) {
                if (classPlans.contains(cpId)) {
                    result.append(classPlans[cpId]);
                }
            }
        }
        return result;
    }

    // 序列化为 ClassIsland 官方 Profile JSON 格式
    QJsonObject toJson() const {
        QJsonObject root;
        root["Name"] = name;
        root["Id"] = id;
        root["IsActive"] = isActive;
        root["SelectedClassPlanGroupId"] = selectedClassPlanGroupId;

        // TimeSlots
        QJsonObject tsObj;
        const auto tsKeys = timeSlots.keys();
        for (const QString& k : tsKeys) {
            tsObj[k] = timeSlots.value(k).toJson();
        }
        root["TimeLayouts"] = tsObj;

        // ClassPlans
        QJsonObject cpObj;
        const auto cpKeys = classPlans.keys();
        for (const QString& k : cpKeys) {
            const ClassPlan& cp = classPlans.value(k);
            QJsonObject cpJson;
            cpJson["Id"] = k;
            cpJson["Name"] = cp.name;
            cpJson["WeekDay"] = cp.weekDay;
            cpJson["WeekCountDiv"] = cp.weekCountDiv;
            cpJson["WeekCountDivTotal"] = cp.weekCountDivTotal;
            cpJson["IsActive"] = cp.isActive;
            QJsonArray lessonsArr;
            for (const Lesson& lesson : cp.lessons) {
                QJsonObject l;
                l["SubjectId"] = lesson.subjectId;
                l["WeekDay"] = lesson.weekDay;
                l["SlotIndex"] = lesson.slotIndex;
                l["WeekCountDiv"] = lesson.weekCountDiv;
                l["WeekCountDivTotal"] = lesson.weekCountDivTotal;
                l["IsActive"] = lesson.isActive;
                lessonsArr.append(l);
            }
            cpJson["Classes"] = lessonsArr;
            cpObj[k] = cpJson;
        }
        root["ClassPlans"] = cpObj;

        // Subjects
        QJsonObject subObj;
        const auto subKeys = subjects.keys();
        for (const QString& k : subKeys) {
            subObj[k] = subjects.value(k).toJson();
        }
        root["Subjects"] = subObj;

        // ClassPlanGroups
        QJsonObject grpObj;
        const auto grpKeys = classPlanGroups.keys();
        for (const QString& k : grpKeys) {
            const ClassPlanGroup& grp = classPlanGroups.value(k);
            QJsonObject g;
            g["Id"] = k;
            g["Name"] = grp.name;
            g["Color"] = grp.color;
            QJsonArray idsArr;
            for (const QString& sid : grp.classPlanIds) { idsArr.append(sid); }
            g["ClassPlanIds"] = idsArr;
            g["IsActive"] = grp.isActive;
            grpObj[k] = g;
        }
        root["ClassPlanGroups"] = grpObj;

        return root;
    }

    // 从 ClassIsland 官方 Profile JSON 格式反序列化
    static Profile fromJson(const QJsonObject& root) {
        Profile p;
        p.name = root["Name"].toString();
        p.id = root["Id"].toString();
        p.isActive = root["IsActive"].toBool(true);
        p.selectedClassPlanGroupId = root["SelectedClassPlanGroupId"].toString();

        // TimeSlots
        const auto tsObj = root["TimeLayouts"].toObject();
        for (auto it = tsObj.begin(); it != tsObj.end(); ++it) {
            p.timeSlots[it.key()] = TimeSlot::fromJson(it.value().toObject());
        }

        // ClassPlans
        const auto cpObj = root["ClassPlans"].toObject();
        for (auto it = cpObj.begin(); it != cpObj.end(); ++it) {
            const auto& cpJson = it.value().toObject();
            ClassPlan cp;
            cp.id = it.key();
            cp.name = cpJson["Name"].toString();
            cp.weekDay = cpJson["WeekDay"].toInt(1);
            cp.weekCountDiv = cpJson["WeekCountDiv"].toInt(1);
            cp.weekCountDivTotal = cpJson["WeekCountDivTotal"].toInt(1);
            cp.isActive = cpJson["IsActive"].toBool(true);

            const auto lessonsArr = cpJson["Classes"].toArray();
            for (const auto& l : lessonsArr) {
                Lesson lesson;
                lesson.subjectId = l.toObject()["SubjectId"].toString();
                lesson.weekDay = l.toObject()["WeekDay"].toInt(1);
                lesson.slotIndex = l.toObject()["SlotIndex"].toInt(0);
                lesson.weekCountDiv = l.toObject()["WeekCountDiv"].toInt(1);
                lesson.weekCountDivTotal = l.toObject()["WeekCountDivTotal"].toInt(1);
                lesson.isActive = l.toObject()["IsActive"].toBool(true);
                cp.lessons.append(lesson);
            }
            p.classPlans[it.key()] = cp;
        }

        // Subjects
        const auto subObj = root["Subjects"].toObject();
        for (auto it = subObj.begin(); it != subObj.end(); ++it) {
            p.subjects[it.key()] = Subject::fromJson(it.value().toObject());
        }

        // ClassPlanGroups
        const auto grpObj = root["ClassPlanGroups"].toObject();
        for (auto it = grpObj.begin(); it != grpObj.end(); ++it) {
            const auto& g = it.value().toObject();
            ClassPlanGroup grp;
            grp.id = it.key();
            grp.name = g["Name"].toString();
            grp.color = g["Color"].toString();
            QStringList ids;
            const auto idsArr = g["ClassPlanIds"].toArray();
            for (const QJsonValue& v : idsArr) { ids.append(v.toString()); }
            grp.classPlanIds = ids;
            grp.isActive = g["IsActive"].toBool(true);
            p.classPlanGroups[it.key()] = grp;
        }

        // ── 真机 ClassIsland 兼容（2026-10-06）──────────────────────────
        // 检测：任一个 ClassPlan 带 TimeRule（真机结构），或 ClassPlanGroups 无 ids（真机无该字段）
        bool isReal = false;
        const auto realCpObj = root["ClassPlans"].toObject();
        for (auto it = realCpObj.begin(); it != realCpObj.end() && !isReal; ++it) {
            if (it.value().toObject().contains("TimeRule")) isReal = true;
        }
        if (!isReal) {
            for (auto it = grpObj.begin(); it != grpObj.end() && !isReal; ++it) {
                if (!it.value().toObject().contains("ClassPlanIds")) isReal = true;
            }
        }

        if (isReal) {
            p.timeSlots.clear();
            p.classPlans.clear();
            for (auto it = p.classPlanGroups.begin(); it != p.classPlanGroups.end(); ++it)
                it->classPlanIds.clear();

            // 1) TimeLayouts: Layouts[] 展开成 timeSlots（key = layout-N）
            const auto tlObj = root["TimeLayouts"].toObject();
            for (auto it = tlObj.begin(); it != tlObj.end(); ++it) {
                const QJsonArray layouts = it.value().toObject()["Layouts"].toArray();
                for (int i = 0; i < layouts.size(); ++i) {
                    TimeSlot ts = TimeSlot::fromLayoutItem(i, layouts.at(i).toObject());
                    ts.id = QStringLiteral("%1/%2").arg(it.key(), QString::number(i));
                    p.timeSlots[ts.id] = ts;
                }
            }

            // 2) ClassPlans: TimeRule + Classes 下标
            for (auto it = realCpObj.begin(); it != realCpObj.end(); ++it) {
                ClassPlan cp = ClassPlan::fromJsonReal(it.value().toObject());
                if (cp.id.isEmpty()) cp.id = it.key();
                p.classPlans[cp.id] = cp;
            }

            // 3) 课表群: 从 ClassPlan.AssociatedGroup 反向收集 ids
            for (auto it = p.classPlans.begin(); it != p.classPlans.end(); ++it) {
                const QString gid = it->associatedGroupId;
                if (gid.isEmpty()) continue;
                auto g = p.classPlanGroups.find(gid);
                if (g != p.classPlanGroups.end()) {
                    if (!g->classPlanIds.contains(it.key()))
                        g->classPlanIds.append(it.key());
                }
            }
        }

        return p;
    }
};
