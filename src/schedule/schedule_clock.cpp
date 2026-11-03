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

// 某张 TimeLayout 的「课时表」——ClassPlan.Classes 的下标就是这张表的序号。
//
// ★ 2026-11-03 修的真 bug：原先这里把所有 TimeLayout 的课时**并成一张全局表**，
//   于是「周一 12 节 / 周日 3 节」两份不同作息混在一起，先排出来的表长度只有 3，
//   周一第 4 节往后全部撞 `slot >= slotTimeTable.size()` 被静默丢弃 ——
//   现场表现为「下发 12 节，岛只显示 3 节」且日志无任何异常。
//   正解：每个 ClassPlan 用它自己 TimeLayoutId 指向的那张表（profile.h 里 key = "<layoutId>/<index>"）。
static QList<TimeSlot> slotsForLayout(const Profile& profile, const QString& layoutId)
{
    QList<QPair<int, TimeSlot>> indexed;
    if (!layoutId.isEmpty()) {
        const QString prefix = layoutId + QLatin1Char('/');
        for (auto it = profile.timeSlots.cbegin(); it != profile.timeSlots.cend(); ++it) {
            if (!it.key().startsWith(prefix)) continue;
            bool ok = false;
            const int idx = it.key().mid(prefix.size()).toInt(&ok);
            if (ok) indexed.append({idx, it.value()});
        }
    }
    if (indexed.isEmpty()) {
        // 简化模型（无 layoutId / timeSlots 是平铺的上课时间点）：按开始时间排序，旧行为。
        QList<TimeSlot> all;
        for (const TimeSlot& ts : profile.timeSlots)
            if (ts.timeType == 0 && ts.isActive) all.append(ts);
        std::sort(all.begin(), all.end(),
                  [](const TimeSlot& a, const TimeSlot& b) { return a.startTime < b.startTime; });
        return all;
    }
    // 按 Layouts 数组下标排（**不是**按开始时间）—— Classes 下标跟的是数组序。
    std::sort(indexed.begin(), indexed.end(),
              [](const QPair<int, TimeSlot>& a, const QPair<int, TimeSlot>& b) {
                  return a.first < b.first;
              });
    QList<TimeSlot> out;
    for (const auto& pair : indexed)
        if (pair.second.timeType == 0 && pair.second.isActive) out.append(pair.second);
    return out;
}

// 从档案构建今天的课程
QList<ScheduleClock::LessonInfo> ScheduleClock::buildTodayLessons(
    const Profile& profile, const QDate& today, int currentWeek)
{
    QList<LessonInfo> result;
    const int weekday = today.dayOfWeek();  // 1=周一 ... 7=周日

    // 当前激活课表群里的 ClassPlan（含多周轮换判断）
    QHash<QString, QList<TimeSlot>> slotCache;   // layoutId -> 课时表（同布局的日只算一次）
    const QList<ClassPlan> plans = profile.activeClassPlans();
    for (const ClassPlan& cp : plans) {
        // ★ WeekDay 两套口径必须换算：
        //   · ClassIsland 真机 TimeRule.WeekDay = **0=周日 … 6=周六**；
        //   · 简化模型 / QDate::dayOfWeek() = **1=周一 … 7=周日**。
        //   不换算时「周日的课」会挂到周一（或干脆不匹配），现场就是整周错位一天。
        const int cpWeekday = (cp.weekDay == 0) ? 7 : cp.weekDay;
        if (cpWeekday != weekday) continue;
        if (!cp.matchesWeek(currentWeek)) continue;

        QList<TimeSlot> slotTimeTable;
        if (slotCache.contains(cp.timeLayoutId)) {
            slotTimeTable = slotCache.value(cp.timeLayoutId);
        } else {
            slotTimeTable = slotsForLayout(profile, cp.timeLayoutId);
            slotCache.insert(cp.timeLayoutId, slotTimeTable);
        }
        if (slotTimeTable.isEmpty()) continue;

        // 该天的每一节课（Classes 数组，下标即 slotIndex）
        for (const Lesson& lesson : cp.lessons) {
            if (!lesson.isActive) continue;
            const int slot = lesson.slotIndex;
            if (slot < 0 || slot >= slotTimeTable.size()) continue;
            const TimeSlot& ts = slotTimeTable[slot];

            LessonInfo l;
            l.periodName = ts.name;
            const auto subIt = profile.subjects.find(lesson.subjectId);
            if (subIt != profile.subjects.end()) {
                l.subjectName = subIt->name.isEmpty() ? subIt->simplifiedName : subIt->name;
                // 2026-11-03（m14970 ③④）：简称/全称分流——课表组件用简称、上下课提醒用全称。
                l.subjectAbbr = subIt->simplifiedName.isEmpty() ? l.subjectName : subIt->simplifiedName;
                l.subjectFullName = subIt->name.isEmpty() ? l.subjectName : subIt->name;
            } else {
                l.subjectName = ts.name;
                l.subjectAbbr = ts.name;
                l.subjectFullName = ts.name;
            }
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
                // 2026-11-03（m14970 ④）：上下课提醒**显示全称**。
                // 用户原话「上下课提醒显示全称」——课表面用简称（一格一个汉字），
                // 但弹出来的提醒必须让人一眼认出是哪门课，缩写在这里是负担。
                const QString& fullName = l.subjectFullName.isEmpty() ? l.subjectName : l.subjectFullName;
                const QString periodName = fullName.isEmpty()
                    ? l.periodName : QStringLiteral("%1 · %2").arg(l.periodName, fullName);
                emit periodStarted(periodName, l.startTime);
                emit lessonEntered(l, activeIdx);   // 2026-11-02：结构化版本（字段全、口径统一）
                qInfo().noquote() << "[schedule-clock] 上课提醒: " << periodName;
            }
        } else {
            // 刚下课（从某一节出来）
            if (m_currentSlotIndex >= 0 && m_currentSlotIndex < m_today.size()) {
                const LessonInfo& l = m_today.at(m_currentSlotIndex);
                const QString& fullName = l.subjectFullName.isEmpty() ? l.subjectName : l.subjectFullName;
                emit periodEnded(fullName, l.endTime);   // 2026-11-03（m14970 ④）：下课提醒同样用全称
                emit lessonLeft(l, m_currentSlotIndex);   // 2026-11-02：结构化版本
                qInfo().noquote() << "[schedule-clock] 下课提醒: " << fullName;
            }
            m_currentPeriod.clear();
            m_currentSlotIndex = -1;
        }
    }
}
