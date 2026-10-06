// 课表时钟单测：不依赖云端/托盘/单例，直接测上下课提醒逻辑。
// 用法: clock_test.exe <profile.json>
// 用环境变量 STE_TEST_NOW=HH:mm 模拟"当前时间"，验证 tick() 触发上课/下课信号。
#include "schedule_clock.h"
#include "schedule/profile_repository.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addPositionalArgument("profile", "ClassIsland profile JSON path");
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        std::printf("usage: clock_test <profile.json>\n");
        return 2;
    }

    // 读档案，打印今日课程
    ProfileRepository repo;
    Profile profile = repo.load(args[0]);
    const QDate today = QDate::currentDate();
    const int weekday = today.dayOfWeek();
    std::printf("== 档案: %s\n", qPrintable(args[0]));
    std::printf("== 今日(周%d) 激活课表群课表数: %d\n", weekday, profile.activeClassPlans().size());

    // 用 ScheduleClock 内部逻辑手动检查
    ScheduleClock clock(args[0]);
    int started = 0, ended = 0;
    QObject::connect(&clock, &ScheduleClock::periodStarted,
                     [&](const QString& n, const QTime& t) {
                         std::printf(">> 上课提醒: %s @ %s\n", qPrintable(n), qPrintable(t.toString("HH:mm")));
                         ++started;
                     });
    QObject::connect(&clock, &ScheduleClock::periodEnded,
                     [&](const QString& n, const QTime& t) {
                         std::printf(">> 下课提醒: %s @ %s\n", qPrintable(n), qPrintable(t.toString("HH:mm")));
                         ++ended;
                     });

    clock.tick();
    std::printf("== tick 结果: 上课 %d 次, 下课 %d 次\n", started, ended);
    std::printf("== 当前状态: %s (slot=%d)\n",
                qPrintable(clock.currentPeriod()), clock.currentSlotIndex());
    return 0;
}
