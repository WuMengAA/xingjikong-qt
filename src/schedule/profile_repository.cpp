#include "profile_repository.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

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
