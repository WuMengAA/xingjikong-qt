#pragma once
#include <QString>
#include "profile.h"

// CSES 格式导入器：将 CSES YAML 转为 Profile
class CsesImporter {
public:
    // 从文件路径导入
    Profile import(const QString& filePath);
    // 从字节数据导入
    Profile importData(const QByteArray& data);
    // 从已解析的 JSON 导入（用于测试）
    Profile importJson(const QJsonObject& json);

    // 导出到 CSES JSON
    QByteArray exportData(const Profile& profile);

private:
    Subject importSubject(const QJsonObject& obj);
    TimeSlot importTimeSlot(const QJsonObject& obj);
    ClassPlan importClassPlan(const QJsonObject& obj);
};
