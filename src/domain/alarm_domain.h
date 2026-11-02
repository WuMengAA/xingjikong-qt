#pragma once
// 2026-10-27：从 main.cpp 拆出的**闹钟域**（拆分计划第五刀）。
// 这一块自带 3 个 g_alarm* 变量、零隐式依赖（外部只用到 speakText 与
// QApplication::beep / LockWorkStation），是整份文件里边界最干净的一块。
// ⚠️ speakText（TTS）仍在 main.cpp —— 用 init() 注入回调，不反向依赖主文件。
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace AlarmDomain {

struct AlarmTask {
    QString id;
    QString time;                 // "HH:MM"（24h）
    QString text;                 // TTS 播报文案（空 = 不播报）
    QVector<int> days;            // 1=周一…7=周日；空 = 当天一次
    bool lock = false;            // 到点锁屏
    qint64 createdMs = 0;
};

// 初始化：持久化文件路径 + 注入 TTS 回调（main.cpp 的 speakText）
void init(const QString &file, std::function<void(const QString &)> speakFn);

QVector<AlarmTask> loadAlarms();
void saveAlarms(const QVector<AlarmTask> &tasks);
qint64 nextAlarmMs(const AlarmTask &t, qint64 fromMs);
void armAlarmTimer(const AlarmTask &t);
void rescheduleAlarms();   // 崩溃/重启后重挂
bool cancelAlarm(const QString &id);   // 找不到返回 false（调用方据此报 failed）

} // namespace AlarmDomain
