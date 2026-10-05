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
};

// 课表：某天的课程安排
struct ClassPlan {
    QString id;
    QString name;          // 如 "周一"
    int weekDay = 1;
    int weekCountDiv = 1;
    int weekCountDivTotal = 1;
    bool isActive = true;
    QList<Lesson> lessons;

    bool matchesWeek(int todayWeek) const {
        if (weekCountDivTotal <= 1) return true;
        return ((todayWeek - 1) % weekCountDivTotal) == (weekCountDiv - 1);
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

        return p;
    }
};
