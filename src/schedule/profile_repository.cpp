#include "profile_repository.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QTime>       // 2026-10-13：makeSample 里构造时间点
#include <QUuid>       // 2026-10-13：makeSample 里生成对象 id

ProfileRepository::ProfileRepository()
{
    m_defaultPath = defaultProfilePath();
}

ProfileRepository::~ProfileRepository() = default;

QString ProfileRepository::defaultProfilePath() {
    // ClassIsland 的档案路径
    // Windows: %LOCALAPPDATA%/ClassIsland/data/Profiles/Default.json
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        // 回退到 LOCALAPPDATA
        appData = QDir::homePath() + "/AppData/Local/ClassIsland";
    }
    return appData + "/data/Profiles/Default.json";
}

Profile ProfileRepository::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Profile();  // 返回空档案
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return Profile();
    }

    QJsonObject root = doc.object();
    return Profile::fromJson(root);
}

bool ProfileRepository::save(const Profile& profile, const QString& path) {
    // 确保目录存在
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QJsonObject root = profile.toJson();
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

Profile ProfileRepository::loadDefault() {
    return load(m_defaultPath);
}

bool ProfileRepository::saveDefault(const Profile& profile) {
    return save(profile, m_defaultPath);
}

bool ProfileRepository::mergeClassPlan(Profile& profile, const QJsonObject& classPlanJson) {
    // 从 ClassPlan JSON 中提取 ClassPlans 和 ClassPlanGroups
    // 按 GUID 逐键合并，保留档案里其它课表

    // 合并 ClassPlans
    if (classPlanJson.contains("ClassPlans")) {
        const QJsonObject cpObj = classPlanJson["ClassPlans"].toObject();
        for (auto it = cpObj.begin(); it != cpObj.end(); ++it) {
            // 解析并合并单个 ClassPlan
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
            profile.classPlans[it.key()] = cp;  // 覆盖或新增
        }
    }

    // 合并 ClassPlanGroups
    if (classPlanJson.contains("ClassPlanGroups")) {
        const QJsonObject grpObj = classPlanJson["ClassPlanGroups"].toObject();
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
            profile.classPlanGroups[it.key()] = grp;  // 覆盖或新增
        }
    }

    return true;
}

// 2026-10-13 审计 2.2：从 main.cpp 搬来的示例档案构造（3 节时间点 + 3 科目 + 2 套课表群）。
// main() 不再需要知道 Profile 内部结构，改示例只动这里。
Profile ProfileRepository::makeSample()
{
    Profile p;
    p.name = QStringLiteral("示例档案");
    p.id = QUuid::createUuid().toString();
    auto mkSlot = [](const QString& n, int h1, int m1, int h2, int m2) {
        TimeSlot ts; ts.id = QUuid::createUuid().toString(); ts.name = n;
        ts.startTime = QTime(h1, m1); ts.endTime = QTime(h2, m2);
        ts.timeType = 0; ts.isActive = true; return ts;
    };
    TimeSlot ts1 = mkSlot(QStringLiteral("第一节"), 8,0, 8,45);
    TimeSlot ts2 = mkSlot(QStringLiteral("第二节"), 8,55, 9,40);
    TimeSlot ts3 = mkSlot(QStringLiteral("第三节"), 10,10, 10,55);
    p.timeSlots.insert(ts1.id, ts1);
    p.timeSlots.insert(ts2.id, ts2);
    p.timeSlots.insert(ts3.id, ts3);
    auto mkSub = [](const QString& n) { Subject s; s.id = QUuid::createUuid().toString();
        s.name = n; s.simplifiedName = n.left(1); return s; };
    Subject s1 = mkSub(QStringLiteral("语文"));
    Subject s2 = mkSub(QStringLiteral("数学"));
    Subject s3 = mkSub(QStringLiteral("英语"));
    p.subjects.insert(s1.id, s1);
    p.subjects.insert(s2.id, s2);
    p.subjects.insert(s3.id, s3);
    auto mkPlan = [&](const QString& name, int wd, const QList<QString>& subs) {
        ClassPlan cp; cp.id = QUuid::createUuid().toString(); cp.name = name;
        cp.weekDay = wd; cp.weekCountDiv = 1; cp.weekCountDivTotal = 1; cp.isActive = true;
        int i = 0;
        for (const QString& sid : subs) {
            cp.lessons.append(Lesson{sid, wd, i++, 1, 1, true});
        }
        return cp;
    };
    ClassPlan c1 = mkPlan(QStringLiteral("周一"), 1, {s1.id, s2.id, s3.id});
    ClassPlan c2 = mkPlan(QStringLiteral("周二"), 2, {s2.id, s3.id, s1.id});
    p.classPlans.insert(c1.id, c1);
    p.classPlans.insert(c2.id, c2);
    ClassPlanGroup grp;
    grp.id = QUuid::createUuid().toString();
    grp.name = QStringLiteral("默认课表"); grp.isActive = true;
    grp.classPlanIds = {c1.id, c2.id};
    p.classPlanGroups.insert(grp.id, grp);
    return p;
}
