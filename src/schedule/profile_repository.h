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
    // 2026-10-13 审计 2.2：生成一份最小示例档案（3 节时间点 + 3 科目 + 2 套课表群）。
    // 原来这段 40 行构造逻辑住在 main.cpp 的 main() 里（与 Profile 强耦合，改示例要
    // 重编整个入口）。挪到仓库类，main() 只留一行调用。
    static Profile makeSample();
    // 保存默认档案
    bool saveDefault(const Profile& profile);
    // 获取默认档案路径
    static QString defaultProfilePath();
    // 合并 ClassPlan 到档案（按 GUID 逐键合并）
    bool mergeClassPlan(Profile& profile, const QJsonObject& classPlanJson);

private:
    QString m_defaultPath;
};
