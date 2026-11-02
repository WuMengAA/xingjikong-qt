#include "schedule_event_bus.h"
#include "schedule_clock.h"

#include <QDate>
#include <QJsonArray>
#include <QMetaObject>

namespace {
ScheduleEventBus::Sinks g_sinks;
bool g_ready = false;
bool g_attached = false;

// 把"一节课"翻译成契约里那十来个字段。**唯一**的翻译点 —— 四类事件全走这里，
// 所以"上/下课事件的字段形状不一致"这类协议漂移在结构上不可能发生。
QJsonObject lessonPayload(const QString &reason,
                          const QString &subjectName,
                          const QString &periodName,
                          const QTime &start,
                          const QTime &end,
                          int slotIndex,
                          const QDate &date)
{
    QJsonObject p;
    p.insert(QStringLiteral("reason"), reason);
    // lessonContent 用 ClassIsland 的既有拼法「节次 · 科目」：插件作者从 ClassIsland
    // 迁过来时不用改解析代码，这是刻意对齐，不是随手起的名字。
    const QString slot = periodName.trimmed();
    const QString subject = subjectName.trimmed();
    QString content;
    if (!slot.isEmpty() && !subject.isEmpty()) content = slot + QStringLiteral(" · ") + subject;
    else if (!slot.isEmpty()) content = slot;
    else content = subject;
    p.insert(QStringLiteral("lessonContent"), content);
    p.insert(QStringLiteral("subjectName"), subject);
    p.insert(QStringLiteral("periodName"), slot);
    p.insert(QStringLiteral("slotIndex"), slotIndex);
    p.insert(QStringLiteral("startTime"), start.isValid() ? start.toString(QStringLiteral("HH:mm:ss")) : QString());
    p.insert(QStringLiteral("endTime"), end.isValid() ? end.toString(QStringLiteral("HH:mm:ss")) : QString());
    p.insert(QStringLiteral("date"), date.toString(QStringLiteral("yyyy-MM-dd")));
    p.insert(QStringLiteral("week"), ScheduleClock::semesterWeek(date));
    return p;
}

void publish(const QString &reason, const QJsonObject &payload)
{
    if (!g_ready) return;
    const QString name = QStringLiteral("schedule_change");
    if (g_sinks.toPlugins) g_sinks.toPlugins(name, payload);
    if (g_sinks.toModules) g_sinks.toModules(name, payload);
    if (g_sinks.toCloud) g_sinks.toCloud(name, payload);
    Q_UNUSED(reason);
}
} // namespace

namespace ScheduleEventBus {

void init(const Sinks &sinks)
{
    g_sinks = sinks;
    g_ready = true;
}

bool ready() { return g_ready; }

void attachClock(ScheduleClock *clock)
{
    if (!clock || g_attached) return;
    g_attached = true;

    // ── 上下课：走 2026-11-02 新增的结构化信号，不再从老信号里反查 ────────────
    // 老的两个信号第一参口径不一致（started 传「第一节 · 语文」拼接串、ended 传**科目名**），
    // 从它们身上还原字段等于每处订阅方自己 split 一遍中文间隔号 —— 拆错了静默出错。
    // 结构化信号原样交出 LessonInfo（科目/节次/起止全在），翻译只在本文件一处发生。
    QObject::connect(clock, &ScheduleClock::lessonEntered, clock,
        [](const ScheduleClock::LessonInfo &l, int slotIndex) {
            publish(QStringLiteral("period-started"),
                    lessonPayload(QStringLiteral("period-started"), l.subjectName, l.periodName,
                                  l.startTime, l.endTime, slotIndex, QDate::currentDate()));
        });

    QObject::connect(clock, &ScheduleClock::lessonLeft, clock,
        [](const ScheduleClock::LessonInfo &l, int slotIndex) {
            publish(QStringLiteral("period-ended"),
                    lessonPayload(QStringLiteral("period-ended"), l.subjectName, l.periodName,
                                  l.startTime, l.endTime, slotIndex, QDate::currentDate()));
        });
}

void emitTimetableChanged(const QString &source){
    QJsonObject p;
    p.insert(QStringLiteral("reason"), QStringLiteral("timetable-changed"));
    p.insert(QStringLiteral("source"), source);
    p.insert(QStringLiteral("date"), QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    p.insert(QStringLiteral("week"), ScheduleClock::semesterWeek(QDate::currentDate()));
    // 不塞今天的完整课表：payload 会进插件日志与云端事件流水，几十节课全量塞进去
    // 每次换档案就多几 KB。要全表的插件自己调 api `schedule_get`（同一份真源）。
    publish(QStringLiteral("timetable-changed"), p);
}

QJsonObject buildSnapshot(ScheduleClock *clock)
{
    QJsonObject out;
    const QDate today = QDate::currentDate();
    out.insert(QStringLiteral("date"), today.toString(QStringLiteral("yyyy-MM-dd")));
    out.insert(QStringLiteral("week"), ScheduleClock::semesterWeek(today));
    if (!clock) {
        out.insert(QStringLiteral("available"), false);
        out.insert(QStringLiteral("current"), QJsonValue::Null);
        out.insert(QStringLiteral("lessons"), QJsonArray());
        return out;
    }
    const QList<ScheduleClock::LessonInfo> today_ = clock->todayLessons();
    QJsonArray arr;
    for (int i = 0; i < today_.size(); ++i) {
        const auto &l = today_.at(i);
        arr.append(lessonPayload(QStringLiteral("snapshot"), l.subjectName, l.periodName,
                                 l.startTime, l.endTime, i, today));
    }
    out.insert(QStringLiteral("available"), true);
    out.insert(QStringLiteral("lessons"), arr);
    const int cur = clock->currentSlotIndex();
    if (cur >= 0 && cur < today_.size()) {
        const auto &l = today_.at(cur);
        out.insert(QStringLiteral("current"),
                   lessonPayload(QStringLiteral("snapshot"), l.subjectName, l.periodName,
                                 l.startTime, l.endTime, cur, today));
    } else {
        out.insert(QStringLiteral("current"), QJsonValue::Null);
    }
    return out;
}

} // namespace ScheduleEventBus
