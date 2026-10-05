#include "cses_importer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

Profile CsesImporter::import(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return Profile();
    }
    QByteArray data = file.readAll();
    file.close();
    return importData(data);
}

Profile CsesImporter::importData(const QByteArray& data) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return Profile();
    }
    return importJson(doc.object());
}

Profile CsesImporter::importJson(const QJsonObject& json) {
    Profile profile;
    profile.name = "CSES Import";
    profile.id = QUuid::createUuid().toString();
    profile.isActive = true;

    // 导入 subjects
    const auto subjects = json["subjects"].toArray();
    for (const auto& s : subjects) {
        Subject sub = importSubject(s.toObject());
        profile.subjects[sub.id] = sub;
    }

    // 导入 time slots (timetable)
    const auto timetable = json["timetable"].toArray();
    for (const auto& ts : timetable) {
        TimeSlot slot = importTimeSlot(ts.toObject());
        profile.timeSlots[slot.id] = slot;
    }

    // 导入 class plans (schedules)
    const auto schedules = json["schedules"].toArray();
    for (const auto& cp : schedules) {
        ClassPlan plan = importClassPlan(cp.toObject());
        profile.classPlans[plan.id] = plan;
    }

    // 创建默认课表群
    if (!profile.classPlans.isEmpty()) {
        ClassPlanGroup grp;
        grp.id = QUuid::createUuid().toString();
        grp.name = "CSES Import";
        grp.isActive = true;
        for (const auto& plan : profile.classPlans) {
            grp.classPlanIds.append(plan.id);
        }
        profile.classPlanGroups[grp.id] = grp;
        profile.selectedClassPlanGroupId = grp.id;
    }

    return profile;
}

Subject CsesImporter::importSubject(const QJsonObject& obj) {
    Subject sub;
    sub.id = obj["id"].toString();
    if (sub.id.isEmpty()) {
        sub.id = QUuid::createUuid().toString();
    }
    sub.name = obj["name"].toString();
    sub.simplifiedName = obj["shortName"].toString();
    return sub;
}

TimeSlot CsesImporter::importTimeSlot(const QJsonObject& obj) {
    TimeSlot ts;
    ts.id = obj["id"].toString();
    if (ts.id.isEmpty()) {
        ts.id = QUuid::createUuid().toString();
    }
    ts.name = obj["name"].toString();
    ts.startTime = QTime::fromString(obj["start"].toString(), "HH:mm");
    ts.endTime = QTime::fromString(obj["end"].toString(), "HH:mm");
    ts.timeType = 0;  // CSES timetable 都是上课
    ts.isActive = true;
    return ts;
}

ClassPlan CsesImporter::importClassPlan(const QJsonObject& obj) {
    ClassPlan cp;
    cp.id = obj["id"].toString();
    if (cp.id.isEmpty()) {
        cp.id = QUuid::createUuid().toString();
    }
    cp.name = obj["name"].toString();
    cp.weekDay = obj["day"].toInt(1);
    cp.weekCountDiv = 1;
    cp.weekCountDivTotal = 1;
    cp.isActive = true;

    // CSES schedule 有 items 数组
    const auto items = obj["items"].toArray();
    int slotIndex = 0;
    for (const auto& item : items) {
        Lesson lesson;
        lesson.subjectId = item.toObject()["subjectId"].toString();
        lesson.weekDay = cp.weekDay;
        lesson.slotIndex = slotIndex++;
        lesson.weekCountDiv = 1;
        lesson.weekCountDivTotal = 1;
        lesson.isActive = true;
        cp.lessons.append(lesson);
    }

    return cp;
}

QByteArray CsesImporter::exportData(const Profile& profile) {
    QJsonObject root;

    // subjects
    QJsonArray subjectsArr;
    for (const auto& sub : profile.subjects) {
        QJsonObject s;
        s["id"] = sub.id;
        s["name"] = sub.name;
        s["shortName"] = sub.simplifiedName;
        subjectsArr.append(s);
    }
    root["subjects"] = subjectsArr;

    // timetable
    QJsonArray timetableArr;
    for (const auto& ts : profile.timeSlots) {
        if (ts.timeType != 0) continue;  // 只导出上课
        QJsonObject t;
        t["id"] = ts.id;
        t["name"] = ts.name;
        t["start"] = ts.startTime.toString("HH:mm");
        t["end"] = ts.endTime.toString("HH:mm");
        timetableArr.append(t);
    }
    root["timetable"] = timetableArr;

    // schedules
    QJsonArray schedulesArr;
    for (const auto& cp : profile.classPlans) {
        QJsonObject cpObj;
        cpObj["id"] = cp.id;
        cpObj["name"] = cp.name;
        cpObj["day"] = cp.weekDay;
        QJsonArray itemsArr;
        for (const auto& lesson : cp.lessons) {
            QJsonObject item;
            item["subjectId"] = lesson.subjectId;
            itemsArr.append(item);
        }
        cpObj["items"] = itemsArr;
        schedulesArr.append(cpObj);
    }
    root["schedules"] = schedulesArr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
