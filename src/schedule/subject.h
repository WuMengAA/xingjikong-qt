// 科目（从 profile.h 抽出，2026-11-04）
//
// 为什么单独一个文件：profile.h 早已在行数台账里（388 行 / 红线 300，只减不增）。
// 这次要给 Subject 补「户外课程」标记与 ClassIsland 原生字段兼容，净增 15 行会触发门禁。
// 抽出来既守住台账，也符合 §10.5「超线文件只许变短」——而 Subject 本身是独立概念，
// 不该跟 Lesson/TimeSlot/ClassPlan 挤在一个头里。
//
// ⚠️ 字段名两套并存（2026-11-04 实测）：
//   · ClassIsland 原生档案（D:\Classlsland\data\Profiles\Default.json）用
//     Name / Initial / TeacherName / IsOutDoor；
//   · 我们云端下发落盘的（%LOCALAPPDATA%\ClassIsland\...）用
//     Id / Name / Room / SimplifiedName / Teacher。
// 读时两套都认（原生优先，缺则回落我们的）；写时两套都写 —— 只写一套会让
// 真机读不到简称，或我们的编辑器读不到户外标记。
#pragma once

#include <QJsonObject>
#include <QString>

struct Subject {
    QString id;
    QString name;
    QString simplifiedName;
    QString teacher;
    QString room;
    bool isOutdoor = false;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["Id"] = id;
        obj["Name"] = name;
        obj["SimplifiedName"] = simplifiedName;
        obj["Teacher"] = teacher;
        obj["Room"] = room;
        obj["Initial"] = simplifiedName;      // ClassIsland 原生
        obj["TeacherName"] = teacher;         // ClassIsland 原生
        obj["IsOutDoor"] = isOutdoor;         // ClassIsland 原生（注意 OutDoor 大写 D）
        return obj;
    }

    static Subject fromJson(const QJsonObject& obj) {
        Subject s;
        s.id = obj["Id"].toString();
        s.name = obj["Name"].toString();
        // 原生 Initial 优先，回落我们的 SimplifiedName；都空由调用方兜底成全称首字
        s.simplifiedName = obj["Initial"].toString();
        if (s.simplifiedName.isEmpty()) s.simplifiedName = obj["SimplifiedName"].toString();
        s.teacher = obj["TeacherName"].toString();
        if (s.teacher.isEmpty()) s.teacher = obj["Teacher"].toString();
        s.room = obj["Room"].toString();
        s.isOutdoor = obj["IsOutDoor"].toBool(false);
        return s;
    }
};
