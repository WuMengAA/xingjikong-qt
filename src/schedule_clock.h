#pragma once
#include <QObject>
#include <QTimer>
#include <QDate>
#include <QTime>
#include "schedule/profile.h"

// 课表时钟：读 ClassIsland 档案，轮询当前时间，检测「上课/下课」边界并触发提醒。
//
// 设计要点：
//   · 独立 QObject，不依赖被控端 main.cpp 的任何全局 —— 通过信号把边界事件抛出去，
//     main.cpp 只需要 connect 到它的 showNotice（或别处），课表模块本身不碰 UI。
//   · 每 30 秒轮询一次；同一节的「上课提醒」「下课提醒」各只发一次（记忆已触发状态）。
//   · 档案路径可配（默认 %LOCALAPPDATA%\ClassIsland\data\Profiles\Default.json）；
//     档案不存在/无课表时静默（不弹"没课表"的废话，只在日志留一行）。
class ScheduleClock : public QObject {
    Q_OBJECT
public:
    explicit ScheduleClock(const QString& profilePath, QObject* parent = nullptr);

    // 开始轮询（安全可多次调用）
    void start();
    void stop();

    // 手动触发一次检查（测试/立即生效用）
    void tick();

    // 当前状态（QML/诊断可读）
    QString currentPeriod() const { return m_currentPeriod; }   // 正在上的课名，空=课间/无课
    int currentSlotIndex() const { return m_currentSlotIndex; } // -1 = 不在任何课内

signals:
    // 上课提醒：periodName（如「第一节 · 语文」），startTime
    void periodStarted(const QString& periodName, const QTime& startTime);
    // 下课提醒：periodName，endTime
    void periodEnded(const QString& periodName, const QTime& endTime);

private:
    // 从档案构建「今天」的课程时间表
    struct TodayLesson {
        QString subjectName;   // 科目名（无则用时间点名）
        QString periodName;    // 时间点名（如「第一节」）
        QTime startTime;
        QTime endTime;
    };
    QList<TodayLesson> buildTodayLessons(const Profile& profile, const QDate& today, int currentWeek);

    QString m_profilePath;
    QTimer m_timer;
    QList<TodayLesson> m_today;      // 今天的课程（按开始时间排）
    int m_lastEmitSlot = -1;         // 上一次触发的节次
    QString m_currentPeriod;         // 当前正在上的课名
    int m_currentSlotIndex = -1;     // 当前节次
};
