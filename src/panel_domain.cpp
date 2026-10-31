// 2026-10-27 · G4/G5/G6：岛组件面板数据域（实现）
//
// 三面共用一套"算数据"骨架：把 QDateTime / 课表 / 目标时刻换算成 QML 能直接吃的
// QJsonObject。**故意不做任何缓存**：面板是"打开那一刻的快照"，
// 跨天、换档案、改偏移之后再打开必须是新数据（缓存只会制造"显示昨天的课"这种事故）。

#include "panel_domain.h"

#include "island.h"
#include "schedule_clock.h"
#include "timesync_domain.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTime>
#include <QVector>

namespace PanelDomain {
namespace {

ScheduleClock *g_clock = nullptr;

// 星期几（中文）。QDate::dayOfWeek(): 1=周一 … 7=周日。
QString weekdayZh(const QDate &d)
{
    static const char *kNames[] = {"星期一", "星期二", "星期三", "星期四",
                                   "星期五", "星期六", "星期日"};
    const int i = d.dayOfWeek() - 1;
    if (i < 0 || i > 6) return QString();
    return QString::fromUtf8(kNames[i]);
}

// "第 9 周" —— 空格是排版需要（大字号下"第9周"会挤成一坨）。
QString weekText(int week)
{
    return QStringLiteral("第 %1 周").arg(week);
}

// 面的**摘要**（收起态显示的那一行）：标题 + 一行副文案。
// ⚠️ 面板被推回去（收起）时学生仍要能看懂"这块是什么"，所以摘要必须自带信息，
//    不能是"点开看详情"这种废话。
struct Summary {
    QString title;
    QString desc;
    QString icon;
};

QJsonObject withSummary(const Summary &s, QJsonObject data)
{
    data.insert(QStringLiteral("title"), s.title);
    data.insert(QStringLiteral("desc"), s.desc);
    data.insert(QStringLiteral("icon"), s.icon);
    return data;
}

// ── G4 日期 ─────────────────────────────────────────────────────────
QJsonObject buildDate()
{
    const QDateTime now = TimesyncDomain::now();
    const QDate d = now.date();
    const int week = ScheduleClock::semesterWeek(d);

    QJsonObject o;
    o.insert(QStringLiteral("time"), now.toString(QStringLiteral("HH:mm")));
    o.insert(QStringLiteral("date"),
             QStringLiteral("%1年%2月%3日").arg(d.year()).arg(d.month()).arg(d.day()));
    o.insert(QStringLiteral("weekday"), weekdayZh(d));
    o.insert(QStringLiteral("week"), weekText(week));
    o.insert(QStringLiteral("weekNum"), week);
    // 秒级滴答要靠 QML 自己走：给它"打开那一刻的判定时间"与总偏移，
    // QML 用 Date.now() + skewMs 续算（与 C++ 的 now() 同口径）。
    o.insert(QStringLiteral("nowMs"), double(now.toMSecsSinceEpoch()));
    o.insert(QStringLiteral("skewMs"), double(TimesyncDomain::skewMs()));

    Summary s;
    s.title = QStringLiteral("今天");
    s.desc = QStringLiteral("%1 · %2").arg(weekText(week), weekdayZh(d));
    s.icon = QStringLiteral("clock");
    return withSummary(s, o);
}

// ── G5 课表 ─────────────────────────────────────────────────────────
QJsonObject buildTimetable(Result *r)
{
    if (!g_clock) {
        r->ok = false;
        r->error = QStringLiteral("课表时钟未就绪（PanelDomain::init 未调用）");
        return QJsonObject();
    }

    const QDateTime now = TimesyncDomain::now();
    const QTime cur = now.time();
    const QList<ScheduleClock::LessonInfo> lessons = g_clock->todayLessons();
    const int week = ScheduleClock::semesterWeek(now.date());

    QJsonArray arr;
    int nowIndex = -1;
    int nextIndex = -1;
    for (int i = 0; i < lessons.size(); ++i) {
        const auto &l = lessons.at(i);
        // 三态：done（已上完）/ now（正在上）/ next（下一节）/ 空（还没到）
        QString state;
        if (cur >= l.endTime) {
            state = QStringLiteral("done");
        } else if (cur >= l.startTime) {
            state = QStringLiteral("now");
            nowIndex = i;
        } else if (nextIndex < 0) {
            state = QStringLiteral("next");
            nextIndex = i;
        }
        QJsonObject o;
        o.insert(QStringLiteral("period"), l.periodName);
        o.insert(QStringLiteral("subject"), l.subjectName);
        o.insert(QStringLiteral("start"), l.startTime.toString(QStringLiteral("HH:mm")));
        o.insert(QStringLiteral("end"), l.endTime.toString(QStringLiteral("HH:mm")));
        o.insert(QStringLiteral("state"), state);
        arr.append(o);
    }

    QJsonObject o;
    o.insert(QStringLiteral("week"), weekText(week));
    o.insert(QStringLiteral("count"), arr.size());
    o.insert(QStringLiteral("lessons"), arr);
    o.insert(QStringLiteral("nowIndex"), nowIndex);
    o.insert(QStringLiteral("nextIndex"), nextIndex);
    o.insert(QStringLiteral("nowTime"), cur.toString(QStringLiteral("HH:mm")));

    Summary s;
    s.title = QStringLiteral("今日课表");
    s.icon = QStringLiteral("layout-grid");
    // 空课表**不是错误**：周末/假期/档案还没导入都会是空的。
    // 如实写"今天没有课"，别让老师以为功能坏了（也别伪装成 failed —— 那会让
    // 管理端以为指令没送到）。
    s.desc = arr.isEmpty()
        ? QStringLiteral("%1 · 今天没有课").arg(weekText(week))
        : QStringLiteral("%1 · 共 %2 节%3")
              .arg(weekText(week)).arg(arr.size())
              .arg(nowIndex >= 0 && nowIndex < lessons.size()
                       ? QStringLiteral(" · 正在上「%1」").arg(lessons.at(nowIndex).subjectName)
                       : QString());
    r->ok = true;
    return withSummary(s, o);
}

// ── G6 倒计时 ───────────────────────────────────────────────────────
// 目标时刻的来源有两档（按优先级）：
//   ① params.to —— "HH:MM"（今天该时刻，已过则算明天）或 ISO8601 完整时刻；
//   ② 没给 to ⇒ 用课表边界：正在上课 → 距下课；否则 → 距下一节上课。
//      （ClassIsland 的倒计时组件也是这个语义：绑"当前/下一节"而不是死时刻。）
QJsonObject buildCountdown(const QJsonObject &params, Result *r)
{
    const QDateTime now = TimesyncDomain::now();
    const QString to = params.value(QStringLiteral("to")).toString().trimmed();
    QString label = params.value(QStringLiteral("label")).toString().trimmed();

    QDateTime target;
    QString note;

    if (!to.isEmpty()) {
        // 先按 HH:MM 解析（最常见：老师直接说"距 12:30"）
        const QTime t = QTime::fromString(to, QStringLiteral("HH:mm"));
        if (t.isValid()) {
            target = QDateTime(now.date(), t);
            if (target <= now) {
                target = target.addDays(1);
                note = QStringLiteral("该时刻今天已过，按明天算");
            }
        } else {
            target = QDateTime::fromString(to, Qt::ISODateWithMs);
            if (!target.isValid()) target = QDateTime::fromString(to, Qt::ISODate);
            if (!target.isValid()) {
                r->ok = false;
                r->error = QStringLiteral("to 既不是 HH:MM 也不是 ISO8601 时刻：%1").arg(to);
                return QJsonObject();
            }
        }
    } else {
        // 落到课表边界
        if (!g_clock) {
            r->ok = false;
            r->error = QStringLiteral("没给 to，且课表时钟未就绪 —— 无法推算倒计时目标");
            return QJsonObject();
        }
        const QList<ScheduleClock::LessonInfo> lessons = g_clock->todayLessons();
        const QTime cur = now.time();
        for (const auto &l : lessons) {
            if (cur >= l.startTime && cur < l.endTime) {   // 正在上课 → 距下课
                target = QDateTime(now.date(), l.endTime);
                if (label.isEmpty())
                    label = QStringLiteral("距离下课");
                if (note.isEmpty())
                    note = QStringLiteral("%1 下课").arg(l.endTime.toString(QStringLiteral("HH:mm")));
                break;
            }
        }
        if (!target.isValid()) {                            // 没在上课 → 距下一节上课
            for (const auto &l : lessons) {
                if (l.startTime > cur) {
                    target = QDateTime(now.date(), l.startTime);
                    if (label.isEmpty())
                        label = QStringLiteral("距离上课");
                    if (note.isEmpty())
                        note = QStringLiteral("%1 上课").arg(l.startTime.toString(QStringLiteral("HH:mm")));
                    break;
                }
            }
        }
        if (!target.isValid()) {
            r->ok = false;
            r->error = QStringLiteral("今天已没有课表边界（课已上完或今天无课）—— 请显式给 to");
            return QJsonObject();
        }
    }

    const qint64 targetMs = target.toMSecsSinceEpoch();
    if (label.isEmpty()) label = QStringLiteral("倒计时");

    QJsonObject o;
    o.insert(QStringLiteral("label"), label);
    // ⚠️ 用 double 走 JSON：QML 侧数字一律是 IEEE754 double，毫秒级 epoch 在
    //    2^53 以内，精确无损（与 fileTransferBytes 的 qint64→real 同口径）。
    o.insert(QStringLiteral("targetMs"), double(targetMs));
    o.insert(QStringLiteral("nowMs"), double(now.toMSecsSinceEpoch()));
    o.insert(QStringLiteral("skewMs"), double(TimesyncDomain::skewMs()));
    o.insert(QStringLiteral("targetText"), target.toString(QStringLiteral("HH:mm")));
    o.insert(QStringLiteral("targetDay"), target.date() == now.date()
              ? QString() : target.toString(QStringLiteral("M/d")));
    if (!note.isEmpty()) o.insert(QStringLiteral("note"), note);

    Summary s;
    s.title = params.value(QStringLiteral("title")).toString().trimmed();
    if (s.title.isEmpty()) s.title = label;
    s.desc = note.isEmpty()
        ? QStringLiteral("到 %1").arg(target.toString(QStringLiteral("HH:mm")))
        : note;
    s.icon = QStringLiteral("hourglass");
    r->ok = true;
    return withSummary(s, o);
}

}   // namespace

QStringList faces()
{
    return {QStringLiteral("date"), QStringLiteral("timetable"), QStringLiteral("countdown")};
}

void init(ScheduleClock *clock)
{
    g_clock = clock;
}

// 顶栏紧凑课表（2026-11-01）：菜单栏中间那格。逻辑与倒计时面板的"目标推算"同源。
QString barLessonText()
{
    if (!g_clock) return QString();
    const QDateTime now = TimesyncDomain::now();
    const QTime cur = now.time();
    const QList<ScheduleClock::LessonInfo> lessons = g_clock->todayLessons();
    if (lessons.isEmpty()) return QStringLiteral("今天无课");
    for (const auto &l : lessons) {                       // 正在上课 → 距下课
        if (cur >= l.startTime && cur < l.endTime) {
            const int remain = qMax(0, int(cur.secsTo(l.endTime)) / 60);
            return QStringLiteral("%1 · %2 · 剩 %3 分").arg(l.subjectName, l.periodName).arg(remain);
        }
    }
    for (const auto &l : lessons) {                       // 课间 → 下一节
        if (l.startTime > cur)
            return QStringLiteral("下节 %1 · %2").arg(l.subjectName, l.startTime.toString(QStringLiteral("HH:mm")));
    }
    return QStringLiteral("今天已结课");
}

Result build(const QString &face, const QJsonObject &params)
{
    Result r;
    if (face.isEmpty()) {
        r.error = QStringLiteral("缺少 face（可选：%1）").arg(faces().join(QStringLiteral(" / ")));
        return r;
    }
    if (face == QStringLiteral("date")) {
        r.data = buildDate();
        r.ok = true;
        return r;
    }
    if (face == QStringLiteral("timetable")) {
        r.data = buildTimetable(&r);
        return r;
    }
    if (face == QStringLiteral("countdown")) {
        r.data = buildCountdown(params, &r);
        return r;
    }
    r.error = QStringLiteral("未知 face=%1（可选：%2）")
                  .arg(face, faces().join(QStringLiteral(" / ")));
    return r;
}

bool show(const QString &face, const QJsonObject &params, int durationMs,
          QString *error, QJsonObject *data)
{
    const Result r = build(face, params);
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (durationMs < 0) durationMs = 0;
    // 上岛：面板占展开区，display 由 C++ 的 IslandOverlay 统一管（含远控期冻结判断）
    IslandOverlay::instance()->showPanel(face, r.data, durationMs);
    if (data) *data = r.data;
    return true;
}

void close()
{
    IslandOverlay::instance()->closePanel();
}

}   // namespace PanelDomain
