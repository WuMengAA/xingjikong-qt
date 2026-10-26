#pragma once
// 2026-10-27：**定时通知域**（F 收尾第 1 项 · 拆分计划第六刀）。
//
// 产品原话：「定时任务里应包括关机、定时闹钟（附有锁屏与相间选时，类似手机闹钟）」
// → 关机已有（schedule_shutdown）、闹钟已有（AlarmDomain），**差的是"定时通知"**。
//
// 与 AlarmDomain 的分工（别混）：
//   · AlarmDomain  = 响铃 / 锁屏 / TTS 播报，是「叫醒」；
//   · NotifyDomain = 走通知体系（岛 / 弹窗 / 跑马灯 / 全屏 + 渠道标签），是「告诉」。
// 两者各自独立文件、各自独立定时器与持久化，互不占用对方的队列。
//
// ⚠️ 触发时**不在本域**渲染通知 —— main.cpp 注入回调（与 AlarmDomain 的 speakText 同款），
//    避免底层域反向依赖主文件（main.cpp 已 8235 行，还在拆）。
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace NotifyDomain {

struct NotifyTask {
    QString id;
    QString time;                 // "HH:mm"（24h）
    int     times = 1;            // 当天触发次数（1 = 一次；>1 按 intervalMin 递进）
    int     intervalMin = 0;      // 次数之间的间隔（分钟）；times <= 1 时无意义
    QString title;                // 通知标题（必填）
    QString content;              // 通知正文
    QString channel;              // 渠道标签（空 = 普通通知；如 "course" = 课程提醒）
    QVector<int> days;            // 1=周一…7=周日；空 = 一次性
    qint64  createdMs = 0;

    // ★ 决策（2026-10-27 00:52 拍板）：times 只对「一次性」有效（当天重复 N 次）；
    //   days 非空时按"每天一次"处理，times 被忽略 —— 否则「每天 × 3 次 × 间隔」的语义
    //   要跨天续算，容易在跨零点时错位。老师的心智模型也就是"今天提醒我三次"。
    //   指令侧校验并在返回里说明，不做静默忽略。
};

// 初始化：持久化文件路径 + 注入触发回调（main.cpp 侧转 islandShow / notifyFromParams）
void init(const QString &file, std::function<void(const NotifyTask &)> fireFn);

QVector<NotifyTask> loadTasks();
void saveTasks(const QVector<NotifyTask> &tasks);

// 下一次触发时刻（epoch ms）；已全部触发完返回 -1。
// firedCount = 该任务已触发的次数（进程内运行时状态，重启后重置）。
qint64 nextFireMs(const NotifyTask &t, qint64 fromMs, int firedCount);

void armTimer(const NotifyTask &t);
void reschedule();                       // 崩溃/重启后重挂
bool cancel(const QString &id);          // 找不到返回 false（调用方据此报 failed）

// 新增或按 id 覆盖；校验失败返回 false 并写 err（调用方据此报 failed，不静默）
bool upsert(const NotifyTask &t, QString *err);

} // namespace NotifyDomain
