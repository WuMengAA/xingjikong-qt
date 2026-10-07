// 星集控被控端 · 灵动岛独立演示（2026-10-07）
// 目的：在 main.cpp 接入之前，独立展示 IslandOverlay 的
//   形状/大小/位置转变 + 弹性缓动 + 信息展示 + 深浅色 + 交互穿透。
// 用法：直接编译 island.cpp + 本文件，链接 Qt6 Widgets 后运行。
//   （这不是产品代码，是给用户过目效果的演示壳。）

#include "island.h"

#include <QApplication>
#include <QTimer>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    IslandOverlay *isl = IslandOverlay::instance();

    // 第 1 阶段（0-3s）：深色 · 收起态 · 语音对讲提示（默认穿透，不挡课件）
    isl->setLightMode(false);
    isl->showIsland(QStringLiteral("正在语音对讲"), QStringLiteral("全班在听"),
                    QStringLiteral("♪"), 3000);

    // 第 2 阶段（3s）：展开态（模拟点开看详情；这版自动走 setExpanded）
    QTimer::singleShot(3000, [isl]() {
        isl->setExpanded(true);
        isl->showIsland(QStringLiteral("正在语音对讲 · 高一1班"),
                        QStringLiteral("时长 02:35\n点「停止」结束讲话\n（本演示无按钮，仅展示展开布局）"),
                        QStringLiteral("♪"), 4000);
    });

    // 第 3 阶段（7s）：切到 屏幕广播（深色收起）
    QTimer::singleShot(7000, [isl]() {
        isl->setExpanded(false);
        isl->setInteractive(false);
        isl->showIsland(QStringLiteral("正在屏幕广播"), QStringLiteral("老师屏幕 → 全部设备"),
                        QStringLiteral("▣"), 3000);
    });

    // 第 4 阶段（10s）：浅色 · 通知提示（演示深浅色切换）
    QTimer::singleShot(10000, [isl]() {
        isl->setLightMode(true);
        isl->showIsland(QStringLiteral("上课啦"), QStringLiteral("第 2 节 · 数学"),
                        QStringLiteral("🔔"), 3000);
    });

    // 第 5 阶段（13s）：进入交互态（点击可展开/收起，演示交互穿透切换）
    QTimer::singleShot(13000, [isl]() {
        isl->setInteractive(true);
        isl->setLightMode(false);
        isl->showIsland(QStringLiteral("可以点我展开/收起"), QStringLiteral("交互态：点击切换形态"),
                        QStringLiteral("◉"), 6000);
    });

    QTimer::singleShot(16000, []() {
        qInfo("演示结束（灵动岛自动淡出）");
    });

    return app.exec();
}