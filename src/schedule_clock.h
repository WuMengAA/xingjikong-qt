#pragma once
#include <QObject>
#include <QTimer>
#include <QDate>
#include <QTime>
#include <QList>
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
    // 2026-10-27（G5 · 课表面板）：今日一节课的**只读视图**。
    // 原来 TodayLesson 是私有嵌套结构（只给 tick() 判定用）；面板要把"今天上什么"
    // 端到端交给 QML，所以提到 public —— 但字段保持最小集（QML 只读这四样）。
    struct LessonInfo {
        QString subjectName;   // 科目名（无则用时间点名）
        QString periodName;    // 时间点名（如「第一节」）
        QTime startTime;
        QTime endTime;
    };

    explicit ScheduleClock(const QString& profilePath, QObject* parent = nullptr);

    // 开始轮询（安全可多次调用）
    void start();
    void stop();

    // 手动触发一次检查（测试/立即生效用）
    void tick();

    // 当前状态（QML/诊断可读）
    QString currentPeriod() const { return m_currentPeriod; }   // 正在上的课名，空=课间/无课
    int currentSlotIndex() const { return m_currentSlotIndex; } // -1 = 不在任何课内

    // ── 2026-10-27（G5 · 课表面板）────────────────────────────────────────
    // 今日课程表（副本；按开始时间已排序）。
    // ⚠️ 只读语义：调用方拿到的是一份拷贝，改它不影响 tick() 的判定。
    //    面板每次打开现取，不做缓存 —— 跨天/换档案后不该拿到昨天的课。
    QList<LessonInfo> todayLessons() const { return m_today; }
    // 今天的"教学周"（9/1 或 2/17 起算，取最近那个开学日；与 tick() 同口径）。
    // 抽成 static 是为了让面板（PanelDomain）与判定（tick）用**同一个**周次算法，
    // 否则会出现"岛上写第 9 周、上下课提醒按第 8 周判"的分叉。
    static int semesterWeek(const QDate &d);

    // 2026-10-27（G3 · 时间同步）：判定用时间的校正量（秒）。
    // 来源 = TimesyncDomain::skewMs()/1000（NTP 差 + 人工偏移），main.cpp 注入。
    // ⚠️ **不改系统钟**：只让"什么时候算上课"跟 NTP/铃声对齐。
    //    0 = 不校正（未注入时的行为与以前逐字一致）。
    void setClockSkewSec(qint64 sec) { m_skewSec = sec; }
    qint64 clockSkewSec() const { return m_skewSec; }

signals:
    // 上课提醒：periodName（如「第一节 · 语文」），startTime
    void periodStarted(const QString& periodName, const QTime& startTime);
    // 下课提醒：periodName，endTime
    void periodEnded(const QString& periodName, const QTime& endTime);

private:
    // 从档案构建「今天」的课程时间表（LessonInfo 已提到 public，见上）
    QList<LessonInfo> buildTodayLessons(const Profile& profile, const QDate& today, int currentWeek);

    QString m_profilePath;
    QTimer m_timer;
    QDate m_builtFor;                // 课表按天重建的"已建日期"（原为函数级 static：多实例会互相干扰、也没法测）
    QList<LessonInfo> m_today;       // 今天的课程（按开始时间排）
    int m_lastEmitSlot = -1;         // 上一次触发的节次
    QString m_currentPeriod;         // 当前正在上的课名
    int m_currentSlotIndex = -1;     // 当前节次
    qint64 m_skewSec = 0;            // 2026-10-27（G3）：判定时间校正量（秒），见 setClockSkewSec
};
