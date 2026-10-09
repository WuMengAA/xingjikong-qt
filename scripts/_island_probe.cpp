// 临时探针（验证完即删）：只链 island.cpp，用来拍灵动岛各态。
// 刻意不链 main.cpp —— 那才是"被控端本体"，按"本机不开刀"的规矩不能起生产进程。
//
// 参数：
//   --light              浅色
//   --expand             直接展开
//   --open <路径>        走"收到文件"那条可点分支（interactive + openPath）
//   --idle               直接进空闲半胶囊（不等通知先出现）
//   --noidle             关掉空闲态（退回"用完隐藏"老行为）
//   --after N            先 showIsland，N 秒后采样（用来看它会不会收回成空闲胶囊）
#include "island.h"

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    IslandOverlay *isl = IslandOverlay::instance();

    bool openMode = false, afterMode = false;
    QString openPath;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--light"))    isl->setLightMode(true);
        if (a == QLatin1String("--expand"))   isl->setExpanded(true);
        if (a == QLatin1String("--noidle"))   isl->setIdleEnabled(false);
        if (a == QLatin1String("--idle"))     { /* 下面统一处理 */ }
        if (a == QLatin1String("--open")) {
            if (i + 1 < argc) openPath = QString::fromLocal8Bit(argv[++i]);
            openMode = true;
        }
    }

    // 空闲态、颜色、展开这些要先于 showIsland 设好，否则会被 showIsland 顶掉
    bool gestureMode = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--gesture")) gestureMode = true;
        if (a == QLatin1String("--idle") || a == QLatin1String("--gesture"))
            isl->ensureIdle();
    }

    if (openMode) {
        isl->setInteractive(true);
        isl->setOpenPath(openPath);
        isl->showIsland(QStringLiteral("收到文件"), openPath,
                        QStringLiteral("\xF0\x9F\x93\xA5"), 6000);
    } else if (!afterMode) {
        for (int i = 1; i < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) != QLatin1String("--after")) continue;
            afterMode = true;
            break;
        }
    }
    if (!openMode && !afterMode && !gestureMode && !isl->isIdle()) {
        isl->showIsland(QStringLiteral("连接中断"),
                        QStringLiteral("正在自动重连…"),
                        QStringLiteral("\xE2\x9A\xA0\xEF\xB8\x8F"), 5000);
    } else if (afterMode) {
        isl->showIsland(QStringLiteral("连接中断"),
                        QStringLiteral("正在自动重连…"),
                        QStringLiteral("\xE2\x9A\xA0\xEF\xB8\x8F"), 5000);
    }

    // ⚠️ 落文件不 qDebug：探针子进程的输出经常被父脚本的管道吞掉，落盘才拿得到。
    QFile logf(QStringLiteral("_island_probe_timeline.txt"));
    logf.open(QIODevice::Append | QIODevice::Text);
    QTextStream out(&logf);
    auto log = [&out, isl](const char *tag) {
        char inst[32];
        qsnprintf(inst, sizeof(inst), "%p", static_cast<const void *>(IslandOverlay::instance()));
        // pid 必须打：多个探针进程会 append 同一个日志文件，
        // 不加 pid 的话两行日志交错看着像"同一个岛自己关了"，能误判成 bug。
        out << QStringLiteral("[pid=%1] %2 instance=%3 visible=%4 interactive=%5 expanded=%6 idle=%7 open=%8\n")
                 .arg(quint64(qApp->applicationPid()))
                 .arg(QString::fromLocal8Bit(tag))
                 .arg(QString::fromLocal8Bit(inst))
                 .arg(isl->isVisible() ? 1 : 0)
                 .arg(isl->interactive() ? 1 : 0)
                 .arg(isl->isExpanded() ? 1 : 0)
                 .arg(isl->isIdle() ? 1 : 0)
                 .arg(isl->openPath().isEmpty() ? 0 : 1);
        out.flush();
    };
    log("t=0");

    // --gesture：进程内注入鼠标事件来验"下拉展开 / 上推收起"。
    // ⚠️ 用 QApplication::sendEvent 直接投递给 widget，**不走系统输入**——
    //    不碰鼠标、不动用户桌面，符合"本机不开刀"。
    bool gesture = false;
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--gesture")) gesture = true;
    if (gesture) {
        QTimer::singleShot(600, [isl, &log]() {
            const QPoint c = isl->rect().center();
            auto send = [isl](QEvent::Type t, QPoint pos) {
                QMouseEvent ev(t, QPointF(pos), QPointF(isl->mapToGlobal(pos)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(isl, &ev);
            };
            log("gesture: 下拉前");
            send(QEvent::MouseButtonPress, c);
            send(QEvent::MouseMove, c + QPoint(0, 40));   // 手指往下拉 40px
            log("gesture: 下拉40px后(已跨阈值)");
            // 方向锁定验证：跨过阈值后再反向挪回阈值以内（模拟窗口自身位移导致的局部坐标漂移），
            // 锁定生效时这里**不能**把已经展开的又收回去。
            send(QEvent::MouseMove, c + QPoint(0, 8));
            log("gesture: 反向挪回8px后(锁定应仍展开)");
            send(QEvent::MouseButtonRelease, c + QPoint(0, 8));
            log("gesture: 释放后");
        });
        QTimer::singleShot(3000, [isl, &log]() {
            const QPoint c = isl->rect().center();
            auto send = [isl](QEvent::Type t, QPoint pos) {
                QMouseEvent ev(t, QPointF(pos), QPointF(isl->mapToGlobal(pos)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(isl, &ev);
            };
            send(QEvent::MouseButtonPress, c);
            send(QEvent::MouseMove, c - QPoint(0, 40));   // 往上推 40px
            log("gesture: 上推40px后");
            send(QEvent::MouseButtonRelease, c - QPoint(0, 40));
            log("gesture: (上推)释放后");
        });
    }

    // 连续采样（每 500ms 一次 tick，每秒记一行）：单点采样会漏中间过程 ——
    // 之前只采 t=0/3/7，一次真实鼠标落点就能把结论搅乱。
    QTimer *sampler = new QTimer(&app);
    int tick = 0;
    QObject::connect(sampler, &QTimer::timeout, [&log, &tick]() {
        ++tick;
        if (tick % 2) return;                       // 每 1 秒一行
        if (tick / 2 > 13) return;                  // 采到 13 秒
        log(QString::number(tick / 2).toLocal8Bit().constData());
    });
    sampler->start(500);

    QTimer::singleShot(14000, &app, &QApplication::quit);
    return app.exec();
}
