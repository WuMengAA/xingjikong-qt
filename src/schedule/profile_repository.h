#pragma once
#include <QString>
#include "profile.h"

// 档案仓库：负责读写档案文件
class ProfileRepository {
public:
    ProfileRepository();
    ~ProfileRepository();

    // 从指定路径加载档案
    Profile load(const QString& path);
    // 保存档案到指定路径
    bool save(const Profile& profile, const QString& path);
    // 加载默认档案
    Profile loadDefault();
    // 保存默认档案
    bool saveDefault(const Profile& profile);
    // 获取默认档案路径
    static QString defaultProfilePath();
    // 合并 ClassPlan 到档案（按 GUID 逐键合并）
    bool mergeClassPlan(Profile& profile, const QJsonObject& classPlanJson);

private:
    QString m_defaultPath;
};
