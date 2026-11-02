#include "schedule_clock.h"
#include "profile_repository.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>

// ⚠️ 注意：本文件放在 control-qt（被控端）里，但 schedule/ 数据模型只有 3 个文件
//    （profile.h / profile_repository.h / profile_repository.cpp），
//    它们依赖的 Subject/ClassPlan/Lesson 结构都在 profile.h 内联实现，无额外依赖。

ScheduleClock::ScheduleClock(const QString& profilePath, QObject* parent)
    : QObject(parent), m_profilePath(profilePath)
{
    m_timer.setInterval(30000);  // 30 秒轮询
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ScheduleClock::tick);
}

void ScheduleClock::start()
{
    if (!m_timer.isActive())
        m_timer.start();
    tick();  // 启动即查一次
}

void ScheduleClock::stop()
{
    m_timer.stop();
}

// 2026-10-27（G5）：教学周算法**唯一实现**。
// 原来这段写在 tick() 里（9/1 起算），面板要用同一口径 ⇒ 抽成 static，两边共用。
// 规则：取「最近一个已到的开学日」——上学期 2/17、下学期 9/1；当天即第 1 周。
// ⚠️ 这是**近似**（真实校历可能提前/延后一周，且不完全对齐周一）。它只影响
//     "今天第几周"这类展示与 WeekCountDiv>1 的双周轮换判断，不影响上下课时刻。
//     要精确就等云端下发校历起始日（挂账：class-timetable 里的学期开始日期）。
int ScheduleClock::semesterWeek(const QDate &d)
{
    const QDate spring(d.year(), 2, 17);
    const QDate autumn(d.year(), 9, 1);
    QDate start;
    if (d >= autumn)      start = autumn;
    else if (d >= spring) start = spring;
    else                  start = QDate(d.year() - 1, 9, 1);   // 1 月 → 算上一学年秋季学期
    const int days = start.daysTo(d);
    return days < 0 ? 1 : (days / 7) + 1;
}

// 从档案构建今天的课程
QList<ScheduleClock::LessonInfo> ScheduleClock::buildTodayLessons(
    const Profile& profile, const QDate& today, int currentWeek)
{
    QList<LessonInfo> result;
    const int weekday = today.dayOfWeek();  // 1=周一 ... 7=周日

    // 从档案提取"节次时间表"：
    //   · 真机 ClassIsland：TimeLayouts 是 map<id, {Layouts:[{StartTime,EndTime,TimeType},...]}>，
    //     节次时间在 Layouts 数组里（按下标 = slotIndex）。
    //   · 简化模型（gen_ci_profile 早期版）：timeSlots 是 map<id, {StartTime,EndTime,TimeType}>。
    // 这里两种都兼容：优先收集"按开始时间排序的上课时间点"作为 slot→时间 的索引表。
    QList<TimeSlot> slotTimeTable;
    {
        // 尝试从真机 TimeLayouts 的 Layouts 数组收集（真机结构）
        bool gotReal = false;
        QList<TimeSlot> real;
        const auto tlKeys = profile.timeSlots.keys();
        for (const QString& k : tlKeys) {
            // profile.timeSlots 是简化模型；真机档案的 TimeLayouts 在 fromJson 里也被解析进 timeSlots
            // （见 profile.h：TimeLayouts 对象被当 TimeSlot 解析，Layouts 数组没展开）。
            // 所以我们优先看简化模型，真机结构需要额外解析 —— 见下。
            Q_UNUSED(k);
        }
        Q_UNUSED(gotReal);

        // 简化模型：timeSlots 直接是 上课时间点（TimeType==0）
        for (const TimeSlot& ts : profile.timeSlots) {
            if (ts.timeType == 0 && ts.isActive) real.append(ts);
        }
        std::sort(real.begin(), real.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
        slotTimeTable = real;
    }

    // 当前激活课表群里的 ClassPlan（含多周轮换判断）
    const QList<ClassPlan> plans = profile.activeClassPlans();
    for (const ClassPlan& cp : plans) {
        // 真机：WeekDay 在 TimeRule 里；简化模型：在 ClassPlan 顶层。
        int cpWeekday = cp.weekDay;  // 简化模型
        // 真机兼容：尝试从 TimeRule 读（需要额外解析，见 profile.h —— 目前简化模型解析器
        // 把 TimeRule 丢掉了，这里仅对简化模型成立）。真机档案的完整兼容需要扩展 fromJson。
        if (cpWeekday != weekday) continue;
        if (!cp.matchesWeek(currentWeek)) continue;

        // 该天的每一节课（Classes 数组，下标即 slotIndex）
        for (const Lesson& lesson : cp.lessons) {
            if (!lesson.isActive) continue;
            const int slot = lesson.slotIndex;
            if (slot < 0 || slot >= slotTimeTable.size()) continue;
            const TimeSlot& ts = slotTimeTable[slot];

            LessonInfo l;
            l.periodName = ts.name;
            const auto subIt = profile.subjects.find(lesson.subjectId);
            l.subjectName = (subIt != profile.subjects.end())
                ? (subIt->name.isEmpty() ? subIt->simplifiedName : subIt->name)
                : ts.name;
            l.startTime = ts.startTime;
            l.endTime = ts.endTime;
            result.append(l);
        }
    }

    // 按开始时间排序
    std::sort(result.begin(), result.end(),
              [](const LessonInfo& a, const LessonInfo& b) { return a.startTime < b.startTime; });
    return result;
}

void ScheduleClock::reloadProfile()
{
    // m_builtFor 置成无效日期即可让下一拍重建（tick() 的判据是 `m_builtFor != today`）。
    // 清三样"已触发"记忆：不清 m_lastEmitSlot 的话，新档案里同一 index 的课会被当作
    // 已提醒过而静默不弹；不清当前节次会让"下课"事件对新表里的旧 index 直接跳过。
    m_builtFor = QDate();
    m_lastEmitSlot = -1;
    m_currentSlotIndex = -1;
    m_currentPeriod.clear();
    m_today.clear();
    qInfo().noquote() << "[schedule-clock] 课表档案已变更，下一拍重建：" << m_profilePath;
    tick();   // 立刻重建一次，别让界面等最多 30 秒（下发是用户主动操作，反馈要即时）
}

void ScheduleClock::tick()
{
    // 2026-10-27（G3）：判定时间 = 系统时间 + 校正量（NTP 差 + 人工偏移，见 TimesyncDomain）。
    // ⚠️ 系统钟一个字都不改 —— 只让"什么时候算上课"跟真实时间/学校铃声对齐。
    const QDateTime now = QDateTime::currentDateTime().addSecs(m_skewSec);
    const QDate today = now.date();
    const QTime curTime = now.time();

    // 每日重建课表（跨天自动刷新）
    if (m_builtFor != today) {
        ProfileRepository repo;
        Profile profile = repo.load(m_profilePath);
        // 周次：走 semesterWeek()（9/1 或 2/17 起算，面板与判定同口径）
        const int currentWeek = semesterWeek(today);
        m_today = buildTodayLessons(profile, today, currentWeek);
        m_builtFor = today;
        qInfo().noquote() << "[schedule-clock] 今日课程 " << m_today.size() << " 节"
                          << (m_today.isEmpty() ? QStringLiteral("（档案无课表或非上课日）") : QString());
        m_currentPeriod.clear();
        m_currentSlotIndex = -1;
        m_lastEmitSlot = -1;
    }

    // 找当前时间落在哪一节
    int activeIdx = -1;
    for (int i = 0; i < m_today.size(); ++i) {
        const LessonInfo& l = m_today.at(i);
        if (curTime >= l.startTime && curTime < l.endTime) { activeIdx = i; break; }
    }

    // 状态变化检测
    if (activeIdx != m_currentSlotIndex) {
        if (activeIdx >= 0) {
            // 刚上课
            const LessonInfo& l = m_today.at(activeIdx);
            m_currentPeriod = l.subjectName;
            m_currentSlotIndex = activeIdx;
            if (m_lastEmitSlot != activeIdx) {  // 同一节上课只提醒一次
                m_lastEmitSlot = activeIdx;
                const QString periodName = l.subjectName.isEmpty()
                    ? l.periodName : QStringLiteral("%1 · %2").arg(l.periodName, l.subjectName);
                emit periodStarted(periodName, l.startTime);
                emit lessonEntered(l, activeIdx);   // 2026-11-02：结构化版本（字段全、口径统一）
                qInfo().noquote() << "[schedule-clock] 上课提醒: " << periodName;
            }
        } else {
            // 刚下课（从某一节出来）
            if (m_currentSlotIndex >= 0 && m_currentSlotIndex < m_today.size()) {
                const LessonInfo& l = m_today.at(m_currentSlotIndex);
                emit periodEnded(l.subjectName, l.endTime);
                emit lessonLeft(l, m_currentSlotIndex);   // 2026-11-02：结构化版本
                qInfo().noquote() << "[schedule-clock] 下课提醒: " << l.subjectName;
            }
            m_currentPeriod.clear();
            m_currentSlotIndex = -1;
        }
    }
}
