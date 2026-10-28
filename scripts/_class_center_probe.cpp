// 班级中心（模块一）运行时走查探针 · C++ 宿主（2026-10-28 · B3 验收）
//
// 为什么不用 qml.exe：本机 qml.exe 连最简 Window 都报 "Did not load any objects"
// （2026-10-27 实测，已用隔离样例证明与被测文件无关），且 console.log 不落地 ——
// 拿它当验收通道等于摸黑。本宿主沿用 #480 探针的三件套：
//   ① 真 context property：classCenter（**真的 ClassCenterController**，不是仿件）、
//      Md3 令牌靠 QML import、SvgIcon 走 build 树；
//   ② probe.finish(bad) 收口：打印断言明细 + grabToImage 自截图（离屏下稳，
//      不依赖 HWND/PrintWindow）；
//   ③ 20s 兜底退出码 30（QML 挂住不烂尾）。
//
// 退出码约定（沿用项目口径）：10 = 断言全过；20 = 有断言失败；30 = 超时兜底。
//
// ── 本探针能验到什么、验不到什么（诚实边界）──────────────────────────
// 能验：QML 真的加载起来了（截图是硬证据）、六个操作名被映射表接受、
//       未知名被拒、几何/配色属性真的有值（不是 undefined）。
// 验不到：指令**最终发到设备**那条链路 —— 探针没有真 ViewerBackend（它依赖
//       网络/音频/托盘等一整条运行期），此处 setBackend(nullptr)，
//       requestOp 会走到"后端未就绪"分支。真正下发需真机联调，见验收记录。
// ────────────────────────────────────────────────────────────────────
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QTimer>
#include <QDebug>
#include <cstdio>

#include "../src/class/class_center_controller.h"
#include "../src/svgiconprovider.h"   // 图标保真（与产品 main.cpp 同款）

/** 探针出入口：QML 断言跑完调 finish(bad)；截图 + 退出码都在这收口。 */
class Probe : public QObject {
    Q_OBJECT
public:
    Probe(QQmlApplicationEngine *engine, const QString &shotPath)
        : m_engine(engine), m_shot(shotPath) {}

    Q_INVOKABLE void finish(int bad)
    {
        // ⚠️ finish 从 QML onCompleted 里调 —— 那还在 engine.load() 期间、
        // app.exec() 之前：此刻 rootObjects() 可能为空，且在 loop 外调
        // QGuiApplication::exit() 会被 Qt 吞掉（表现为兜底反杀 30，#480 实测）。
        // 整个收口推迟到事件循环内再跑。
        QTimer::singleShot(0, this, [this, bad]() { doFinish(bad); });
    }

private:
    void doFinish(int bad)
    {
        printf("[RESULT] bad=%d\n", bad);
        fflush(stdout);

        QQuickWindow *win = nullptr;
        const auto roots = m_engine->rootObjects();
        if (!roots.isEmpty())
            win = qobject_cast<QQuickWindow *>(roots.first());
        if (!win || m_shot.isEmpty()) {
            QGuiApplication::exit(bad == 0 ? 10 : 20);
            return;
        }
        // Qt6.12：QQuickWindow::grabToImage 已移除；QQuickItem::grabToImage 返回
        // QSharedPointer。离屏下首帧有时序 ⇒ 重试阶梯（500/1500/3000ms），
        // 截图锦上添花，不挡退出码。
        attemptGrab(win, bad, 0);
    }

    void attemptGrab(QQuickWindow *win, int bad, int attempt)
    {
        const int delays[] = { 500, 1500, 3000 };
        printf("[SHOT] attempt#%d contentItem=%p exposed=%d...\n",
               attempt, (void *)win->contentItem(), win->isExposed() ? 1 : 0);
        fflush(stdout);
        QSharedPointer<QQuickItemGrabResult> res = win->contentItem()->grabToImage();
        if (res) {
            connect(res.data(), &QQuickItemGrabResult::ready, this, [this, res, bad]() {
                const QImage img = res->image();
                if (img.save(m_shot))
                    printf("[SHOT] %s (%dx%d)\n", m_shot.toUtf8().constData(),
                           img.width(), img.height());
                else
                    printf("[SHOT] save FAILED: %s\n", m_shot.toUtf8().constData());
                fflush(stdout);
                QGuiApplication::exit(bad == 0 ? 10 : 20);
            });
            return;
        }
        if (attempt >= 2) {
            printf("[SHOT] 三次都拿不到 grab result，放弃截图（退出码照发）\n");
            fflush(stdout);
            QGuiApplication::exit(bad == 0 ? 10 : 20);
            return;
        }
        QTimer::singleShot(delays[attempt], this, [this, win, bad, attempt]() {
            attemptGrab(win, bad, attempt + 1);
        });
    }

    QQmlApplicationEngine *m_engine;
    QString m_shot;
};

/** 全量消息落地：QML 的类型解析错误/warning 都走这里，否则排障摸黑。 */
static void msgHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *tag = "?";
    switch (type) {
    case QtDebugMsg: tag = "DBG"; break;
    case QtInfoMsg: tag = "INF"; break;
    case QtWarningMsg: tag = "WRN"; break;
    case QtCriticalMsg: tag = "ERR"; break;
    case QtFatalMsg: tag = "FTL"; break;
    }
    printf("[%s] %s\n", tag, msg.toUtf8().constData());
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(msgHandler);
    // argv[1] = 截图输出路径（"-" = 不截图）；argv[2] = 探针 QML 绝对路径
    const QString shotPath = argc > 1 ? QString::fromUtf8(argv[1]) : QString();
    const QString qmlPath  = argc > 2 ? QString::fromUtf8(argv[2]) : QString();
    if (qmlPath.isEmpty()) {
        printf("usage: _class_center_probe.exe <shot.png|-> <probe.qml>\n");
        return 2;
    }

    QGuiApplication app(argc, argv);

    // 真控制器（不是仿件）：本批的验收重点之一就是它的属性真有值。
    // setBackend(nullptr) —— 见文件头「验不到什么」。
    // ⚠️ 必须声明在 engine **之前**：它是注册给 QML 的裸指针上下文属性，
    //    局部对象逆序析构 ⇒ 声明在 engine 后面会先于 engine 销毁，
    //    engine 拆 QML 时读 `classCenter.*` 命中已释放对象 ⇒ 退出码 0xC0000005
    //    （2026-10-28 实测，产品 main.cpp 同缺陷已一并修正）。
    ClassCenterController ctl;

    QQmlApplicationEngine engine;

    // 内置模块导入路径：exe 在 RUNDIR（不在 Qt 安装目录旁），
    // QLibraryInfo 推不出、QT_QML_IMPORT_PATH 实测也不生效 ⇒ 显式 addImportPath 最稳。
    engine.addImportPath(QStringLiteral("D:/QTqwq/6.12.0/msvc2022_64/qml"));

    // SVG 图标（2026-10-28 B3 补）：产品侧 main.cpp 在 543 行注册，探针原先没注册
    // ⇒ 截图里所有 SvgIcon 都是空白，导致"麦克风 +45°/关机左侧白图标"这类条目
    // 无法目视核对。这里补同款提供者 + 同款 qrc（构建脚本 rcc 生成），保真度对齐产品。
    engine.addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider);

    // ── 探针假数据（2026-10-28）：控制器是"宿主推数据"模式（B1），
    //    探针作为宿主喂三份假数据，让 classes/devices/events 真有值可断。──
    {
        QJsonArray classes;
        classes.append(QJsonObject{
            {QStringLiteral("code"), QStringLiteral("cls-a")},
            {QStringLiteral("name"), QStringLiteral("一年级 1 班")},
            // onlineCount 必须与 devices 数组口径自洽（真实云端一致；探针假数据
            // 不一致会让在线率 >100% —— 控制器不校验上游数据，属预期行为）
            {QStringLiteral("onlineCount"), 3},
            {QStringLiteral("devices"), QJsonArray{QStringLiteral("dev-001"),
                                                   QStringLiteral("dev-002"),
                                                   QStringLiteral("dev-003")}}});
        classes.append(QJsonObject{
            {QStringLiteral("code"), QStringLiteral("cls-b")},
            {QStringLiteral("name"), QStringLiteral("二年级 3 班")},
            {QStringLiteral("onlineCount"), 0},
            {QStringLiteral("devices"), QJsonArray{QStringLiteral("dev-004")}}});
        ctl.setClasses(classes);

        QJsonArray devices;
        devices.append(QJsonObject{{QStringLiteral("uid"), QStringLiteral("dev-001")},
                                   {QStringLiteral("name"), QStringLiteral("260901")}});
        devices.append(QJsonObject{{QStringLiteral("uid"), QStringLiteral("dev-002")},
                                   {QStringLiteral("name"), QStringLiteral("260902")}});
        ctl.setDevices(devices);

        QJsonArray events;
        events.append(QJsonObject{{QStringLiteral("level"), QStringLiteral("warn")},
                                  {QStringLiteral("time"), QStringLiteral("08:30")},
                                  {QStringLiteral("msg"), QStringLiteral("dev-001 断线重连")}});
        events.append(QJsonObject{{QStringLiteral("level"), QStringLiteral("error")},
                                  {QStringLiteral("time"), QStringLiteral("09:15")},
                                  {QStringLiteral("msg"), QStringLiteral("dev-002 黑屏")}});
        ctl.setEvents(events);
    }

    engine.rootContext()->setContextProperty(QStringLiteral("classCenter"), &ctl);

    Probe probe(&engine, shotPath == QLatin1String("-") ? QString() : shotPath);
    engine.rootContext()->setContextProperty(QStringLiteral("probe"), &probe);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [&](const QUrl &url) {
        printf("[RESULT] QML 创建失败：%s\n", url.toString().toUtf8().constData());
        fflush(stdout);
        QGuiApplication::exit(20);
    });

    QTimer::singleShot(20000, &app, [&]() {
        printf("[RESULT] timeout（QML 未走到 probe.finish）\n");
        fflush(stdout);
        QGuiApplication::exit(30);
    });

    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        printf("[RESULT] QML 加载失败（rootObjects 空）\n");
        fflush(stdout);
        return 20;
    }
    return app.exec();
}

#include "moc__class_center_probe.cpp"
