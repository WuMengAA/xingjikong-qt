// 课表网格坐标探针（2026-11-04）
//
// 验的事：ScheduleModel 的 row 必须是「本天第几节」，不是「全局第几个时间点」。
// 背景：生产档案有 3 张 TimeLayout（周一~四 12 节 / 周五 8 节 / 周日 3 节），
// 旧实现把三张表的上课段全展开成 23 行全局表 ⇒ 周一~四的课落偶数行、周五落奇数行，
// 每列一半行是空洞，行头时间在两表之间反复跳。这正是用户报的「课表顺序节次有误」。
//
// 判绿：①行数 = 各天最大节数（12），不是 23；②同一行 row=0 在周一和周五
//       分别取到各自时间表的第 1 节；③周一 12 节全有格，周五第 9 节起是空格。
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QDir>
#include "schedule_model.h"

static int g_pass = 0, g_fail = 0;
static void ok(bool cond, const char* what, const QString& got = {}) {
    if (cond) { ++g_pass; printf("  PASS  %s\n", what); }
    else { ++g_fail; printf("  FAIL  %s   got=%s\n", what, got.toUtf8().constData()); }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { printf("PROBE: 打不开档案 %s\n", path.toUtf8().constData()); return 1; }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    Profile profile = Profile::fromJson(root);

    ScheduleModel m(profile);
    m.setCurrentWeek(10);   // 生产档案 TimeRule 的周次

    printf("== 档案：%s ==\n", path.toUtf8().constData());
    printf("   TimeLayouts 展开后 timeSlots=%d，课表群数=%d\n",
           profile.timeSlots.size(), profile.classPlanGroups.size());

    const int rows = m.rowCount();
    printf("   网格行数 rowCount=%d\n", rows);

    // ① 行数应等于各天最大节数（周一~四 12 节），而不是 55 条时段里的 23 个上课段
    ok(rows == 12, "A1 行数=各天最大节数 12（不是全局上课段数 23）", QString::number(rows));

    // ② 同一 row=0，各天取自己时间表第 1 节
    //    ⚠️ 周一与周五前 8 节在真实档案里时间完全一样（两表同款作息），
    //    所以「周一≠周五」不是有效判据 —— 真差异在周日（18:50 起，晚上才有课）。
    const QString mon0 = m.cellRangeText(0, 0);   // 列 0 = 周一（周1234 表，07:25）
    const QString sun0 = m.cellRangeText(0, 6);   // 列 6 = 周日（周日表，18:50）
    printf("   周一第1节=%s  周日第1节=%s\n",
           mon0.toUtf8().constData(), sun0.toUtf8().constData());
    ok(!mon0.isEmpty(), "A2 周一第1节有时间", mon0);
    ok(!sun0.isEmpty(), "A3 周日第1节有时间", sun0);
    ok(mon0 != sun0, "A4 周一/周日第1节各用自己那张表（07:25 vs 18:50）", mon0 + " vs " + sun0);

    // ③ 周一 12 节全有对应表段；周五第 9 节起没有（周五只有 8 节）
    int monFull = 0;
    for (int r = 0; r < 12; ++r) if (m.cellStageType(r, 0) == 0) ++monFull;
    ok(monFull == 12, "A5 周一 12 节全有表段", QString::number(monFull));

    const int fri8 = m.cellStageType(7, 4);    // 第 8 节（下标 7）
    const int fri9 = m.cellStageType(8, 4);    // 第 9 节（下标 8）—— 周五没有
    ok(fri8 == 0, "A6 周五第 8 节存在", QString::number(fri8));
    ok(fri9 == 2, "A7 周五第 9 节不存在（该天表只有 8 节）", QString::number(fri9));

    // ④ 行头是「第N节」而不是一个对不上任何一天的全局时间
    const QString rh = m.rowHeader(0);
    ok(rh.startsWith(QStringLiteral("第")) && rh.endsWith(QStringLiteral("节")),
       "A8 行头是『第N节』而非全局时间", rh);

    // ⑤ 科目简称（课表页格内显示简称）
    QString abbr;
    for (int r = 0; r < 12 && abbr.isEmpty(); ++r)
        for (int c = 0; c < 7 && abbr.isEmpty(); ++c)
            abbr = m.cellAbbr(r, c);
    printf("   首个非空简称=%s\n", abbr.toUtf8().constData());
    ok(abbr.size() <= 2, "A9 简称是短文本（≤2 字）", abbr);

    printf("\nRESULT: PASS %d / FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 10 : 20;
}
