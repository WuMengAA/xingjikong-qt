// 课表构造探针（2026-11-03 · 不入库）
//
// 为什么要有它：真机下发 6 天 / 59 节之后，教室机只显示「今日课程 3 节」。
// 根因是三处口径错位叠加，且**全部静默**（没有任何日志、没有异常），
// 只能靠肉眼对数字才发现。本探针直接把构造好的 ClassIsland 档案喂给
// ScheduleClock::buildTodayLessons()，逐条断言周一到底该出几节。
//
// 覆盖的四件事：
//   A 真机 TimeLayouts（每张表独立下标）→ 周一 12 节，且第 4 节往后不丢
//   B WeekDay 口径：真机 0=周日 … 6=周六，换成 QDate 的 1=周一 … 7=周日
//   C 多张 TimeLayout 不得混表（12 节的日不受 3 节的日影响）
//   D 简化模型（无 layoutId）仍按开始时间排序，行为不变
//
// 退出码：0=全过 / 1=有 FAIL。
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QFile>
#include "schedule/profile.h"
#include "schedule/schedule_clock.h"

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const QString& what, const QString& detail = QString())
{
    if (ok) {
        ++g_pass;
        QTextStream(stdout) << "  \u2713 " << what << "\n";
    } else {
        ++g_fail;
        QTextStream(stdout) << "  \u2717 " << what
                            << (detail.isEmpty() ? QString() : QStringLiteral("  [") + detail + "]")
                            << "\n";
    }
}

// 造一份「真机形态」档案：两张 TimeLayout（全天 12 节 / 半天 3 节），
// 周一挂 12 节的表，周日挂 3 节的表。WeekDay 用真机口径 0=周日…6=周六。
static QJsonObject makeRealProfile()
{
    // 12 个课时时间点（08:00 起，每节 40 分钟、节间隔 20 分钟）
    QJsonArray layouts;
    for (int i = 0; i < 12; ++i) {
        const int h = 8 + i / 2;
        const int m = (i % 2) ? 40 : 0;
        QJsonObject it;
        it["StartTime"] = QStringLiteral("%1:%2:00")
                              .arg(h, 2, 10, QLatin1Char('0'))
                              .arg(m, 2, 10, QLatin1Char('0'));
        const int eh = 8 + (i + 1) / 2;
        const int em = ((i + 1) % 2) ? 40 : 0;
        it["EndTime"] = QStringLiteral("%1:%2:00")
                            .arg(eh, 2, 10, QLatin1Char('0'))
                            .arg(em, 2, 10, QLatin1Char('0'));
        it["TimeType"] = 0;
        layouts.append(it);
    }

    QJsonObject tlFull;
    tlFull["Name"] = QStringLiteral("全天");
    tlFull["Layouts"] = layouts;

    // 半天表：只有前 3 个时间点（模拟「周日只上三节」的另一份作息）
    QJsonArray shortArr;
    for (int i = 0; i < 3; ++i) shortArr.append(layouts.at(i));
    QJsonObject tlShort;
    tlShort["Name"] = QStringLiteral("半天");
    tlShort["Layouts"] = shortArr;

    QJsonObject timeLayouts;
    timeLayouts["layout-full"] = tlFull;
    timeLayouts["layout-short"] = tlShort;

    // 科目
    QJsonObject subjects;
    for (int i = 0; i < 12; ++i) {
        QJsonObject s;
        s["Name"] = QStringLiteral("科目%1").arg(i + 1);
        subjects[QStringLiteral("sub-%1").arg(i)] = s;
    }

    // 周一（真机 WeekDay=1）挂全天表 12 节
    QJsonArray classesMon;
    for (int i = 0; i < 12; ++i) {
        QJsonObject c;
        c["SubjectId"] = QStringLiteral("sub-%1").arg(i);
        c["Index"] = i;
        classesMon.append(c);
    }
    QJsonObject cpMon;
    cpMon["Name"] = QStringLiteral("周一");
    cpMon["TimeRule"] = QJsonObject{{"WeekDay", 1}, {"WeekCountDiv", 1}};
    cpMon["TimeLayoutId"] = QStringLiteral("layout-full");
    cpMon["Classes"] = classesMon;
    cpMon["AssociatedGroup"] = QStringLiteral("grp-default");

    // 周日（真机 WeekDay=0）挂半天表 3 节
    QJsonArray classesSun;
    for (int i = 0; i < 3; ++i) {
        QJsonObject c;
        c["SubjectId"] = QStringLiteral("sub-%1").arg(i);
        c["Index"] = i;
        classesSun.append(c);
    }
    QJsonObject cpSun;
    cpSun["Name"] = QStringLiteral("周日");
    cpSun["TimeRule"] = QJsonObject{{"WeekDay", 0}, {"WeekCountDiv", 1}};
    cpSun["TimeLayoutId"] = QStringLiteral("layout-short");
    cpSun["Classes"] = classesSun;
    cpSun["AssociatedGroup"] = QStringLiteral("grp-default");

    QJsonObject plans;
    plans["plan-mon"] = cpMon;
    plans["plan-sun"] = cpSun;

    QJsonObject grp;
    grp["Name"] = QStringLiteral("默认课表群");
    QJsonObject groups;
    groups["grp-default"] = grp;

    QJsonObject root;
    root["Name"] = QStringLiteral("探针档案");
    root["Id"] = QStringLiteral("probe");
    root["IsActive"] = true;
    root["SelectedClassPlanGroupId"] = QStringLiteral("grp-default");
    root["TimeLayouts"] = timeLayouts;
    root["ClassPlans"] = plans;
    root["Subjects"] = subjects;
    root["ClassPlanGroups"] = groups;
    return root;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    out << "=== 课表构造探针 ===\n";

    // ── 可选：--real <path> 直接读真机落盘档案，逐日打印节数 ──────────────
    // 构造档案能证明算法，但证明不了「云端下发的这份档案」也会算对 ——
    // 现场真正踩的是后者（云端 6 天 59 节，教室机只显示 3 节）。
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--real")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            out << "读不到 " << path << "\n";
            return 1;
        }
        const Profile rp = Profile::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        out << "档案：" << path << "\n";
        int totalLessons = 0;
        for (int dow = 1; dow <= 7; ++dow) {
            // 2026-11-02 是周一，往后推到周日
            const QDate d(2026, 11, 1 + dow);
            const auto list = ScheduleClock::buildTodayLessons(rp, d, 10);
            totalLessons += list.size();
            out << "  周" << dow << " (" << d.toString("MM-dd") << ")  " << list.size() << " 节";
            if (!list.isEmpty())
                out << "  " << list.first().startTime.toString("HH:mm") << "→"
                    << list.last().endTime.toString("HH:mm");
            out << "\n";
            // ⚠️ 不逐日断言"有课"：云端本来就可能只下发 5 天 / 6 天（周六没课是正常假期），
            //    真机档案就是 6 天 59 节、周六 0 节 —— 断言"每天非空"会把正确结果判红。
            Q_UNUSED(dow);
        }
        out << "  七天合计 " << totalLessons << " 节（应与云端下发数一致）\n";
        // 2026-11-02 落盘的这份档案：云端下发 6 天 / 59 节，逐日算出来必须也是 59 节。
        // 少于这个数就说明「下发数 ≠ 能显示的数」—— 正是本次要消灭的静默丢课。
        check(totalLessons == 59, QStringLiteral("逐日合计 = 59 节（云端下发数）"),
              QStringLiteral("实际 %1").arg(totalLessons));
        out << (g_fail == 0 ? "全部通过" : "有失败") << "：PASS " << g_pass << " / FAIL " << g_fail
            << "\n";
        return g_fail == 0 ? 0 : 1;
    }

    // ── 真机档案 ────────────────────────────────────────────────
    const QJsonObject root = makeRealProfile();
    const Profile profile = Profile::fromJson(root);

    out << "\n[A] 真机档案 · 周一（应 12 节）\n";
    // 2026-11-02 是周一
    const QDate monday(2026, 11, 2);
    const QList<ScheduleClock::LessonInfo> mon =
        ScheduleClock::buildTodayLessons(profile, monday, 10);
    check(mon.size() == 12, QStringLiteral("周一课程数 = 12"),
          QStringLiteral("实际 %1").arg(mon.size()));
    if (!mon.isEmpty()) {
        check(mon.first().startTime == QTime(8, 0), QStringLiteral("第一节 08:00"),
              mon.first().startTime.toString());
        check(mon.last().startTime == QTime(13, 40) || mon.last().startTime == QTime(14, 0),
              QStringLiteral("末节是第 12 节的时刻"),
              mon.last().startTime.toString());
        check(mon.first().subjectName == QStringLiteral("科目1"), QStringLiteral("科目名取 Subject"),
              mon.first().subjectName);
    }

    out << "\n[B] WeekDay 口径 · 周日（真机 0 → QDate 7）\n";
    const QDate sunday(2026, 11, 8);   // 2026-11-08 是周日
    check(sunday.dayOfWeek() == 7, QStringLiteral("2026-11-08 确实是周日"),
          QString::number(sunday.dayOfWeek()));
    const QList<ScheduleClock::LessonInfo> sun =
        ScheduleClock::buildTodayLessons(profile, sunday, 10);
    check(sun.size() == 3, QStringLiteral("周日课程数 = 3"),
          QStringLiteral("实际 %1").arg(sun.size()));

    out << "\n[C] 不得混表 · 周一不受半天表影响\n";
    bool allFull = true;
    for (const auto& l : mon)
        if (l.startTime > QTime(14, 0)) allFull = false;
    check(allFull, QStringLiteral("周一各节时刻都在全天表范围内"));
    check(sun.size() != mon.size(), QStringLiteral("两天课时数不同（说明用的是各自那张表）"));

    out << "\n[D] 简化模型（无 TimeLayoutId）仍按开始时间排序\n";
    {
        QJsonObject simple;
        simple["Name"] = QStringLiteral("简化");
        simple["IsActive"] = true;
        QJsonObject ts;
        // 故意乱序写入
        ts["t2"] = QJsonObject{{"StartTime", "10:00:00"}, {"EndTime", "10:40:00"},
                               {"TimeType", 0}, {"Name", "第二节"}};
        ts["t1"] = QJsonObject{{"StartTime", "08:00:00"}, {"EndTime", "08:40:00"},
                               {"TimeType", 0}, {"Name", "第一节"}};
        simple["TimeLayouts"] = ts;
        QJsonObject subjects2;
        subjects2["s1"] = QJsonObject{{"Name", "语文"}};
        subjects2["s2"] = QJsonObject{{"Name", "数学"}};
        simple["Subjects"] = subjects2;
        QJsonArray cls;
        cls.append(QJsonObject{{"SubjectId", "s1"}, {"SlotIndex", 0}, {"WeekDay", 1}});
        cls.append(QJsonObject{{"SubjectId", "s2"}, {"SlotIndex", 1}, {"WeekDay", 1}});
        QJsonObject cp;
        cp["Name"] = QStringLiteral("周一");
        cp["WeekDay"] = 1;
        cp["WeekCountDiv"] = 1;
        cp["Classes"] = cls;
        simple["ClassPlans"] = QJsonObject{{"p1", cp}};
        // 简化模型的 activeClassPlans() 走 ClassPlanGroups 的 ClassPlanIds 列表
        // （真机走 AssociatedGroup 反收集）—— 少了这段就一节都不出，不是 bug 是档案缺件。
        simple["SelectedClassPlanGroupId"] = QStringLiteral("g1");
        QJsonObject g1;
        g1["Name"] = QStringLiteral("默认");
        g1["ClassPlanIds"] = QJsonArray{QStringLiteral("p1")};
        simple["ClassPlanGroups"] = QJsonObject{{"g1", g1}};
        const Profile sp = Profile::fromJson(simple);
        const QList<ScheduleClock::LessonInfo> sm =
            ScheduleClock::buildTodayLessons(sp, monday, 10);
        check(sm.size() == 2, QStringLiteral("简化模型 2 节"),
              QStringLiteral("实际 %1").arg(sm.size()));
        if (sm.size() == 2)
            check(sm.first().startTime < sm.last().startTime,
                  QStringLiteral("按开始时间升序"));
    }

    out << "\n" << (g_fail == 0 ? "全部通过" : "有失败") << "：PASS " << g_pass
        << " / FAIL " << g_fail << "\n";
    return g_fail == 0 ? 0 : 1;
}
