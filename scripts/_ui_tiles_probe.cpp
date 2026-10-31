// 三件新组件的离屏渲染探针（host）
//
// 与 _agent_ui_probe.cpp 同款做法：一个最小 QGuiApplication，加载 .qml，
// 等 sceneGraph 首帧画完后 grabWindow 存 PNG。
//
// 为什么不用 qml.exe：项目里 qtquickcontrols2 的 style 与 Theme 单例的加载路径
// 按构建期部署走，裸 qml.exe 会报 `module ... plugin not found`（本项目已知环境限制，
// 见 AGENTS.md 关于 QtShadcn static 模块的说明）。用本工程同样的导入路径最接近真机。
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QImage>
#include <QDir>
#include <QDebug>
#include <QQuickStyle>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // 被控端实际用 Basic（QtShadcn 规范要求，见 AGENTS.md / MEMORY.md）
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QString qmlFile = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString outFile = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("probe.png");
    if (qmlFile.isEmpty()) {
        qWarning() << "用法: _ui_tiles_probe <file.qml> <out.png>";
        return 2;
    }

    QQmlApplicationEngine engine;
    // 让 QML 里的相对 import（"../ui"）能解析到 shared-qml
    engine.addImportPath(QCoreApplication::applicationDirPath());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() {
                         qWarning() << "OBJECT CREATION FAILED";
                         QCoreApplication::exit(3);
                     }, Qt::QueuedConnection);

    engine.load(QUrl::fromLocalFile(qmlFile));
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "根对象为空：QML 未加载";
        return 3;
    }

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!win) {
        qWarning() << "根对象不是 Window";
        return 4;
    }

    // 首帧之后再等一会，让 NumberAnimation / Layout 完成一轮布局
    auto *timer = new QTimer(&app);
    timer->setSingleShot(true);
    timer->setInterval(1200);
    QObject::connect(timer, &QTimer::timeout, [&, win]() {
        QImage img = win->grabWindow();
        if (img.isNull()) {
            qWarning() << "grabWindow 返回空图";
            QCoreApplication::exit(5);
            return;
        }
        if (!img.save(outFile)) {
            qWarning() << "保存失败:" << outFile;
            QCoreApplication::exit(6);
            return;
        }
        // 黑占比检查：整屏全黑 / 全同色说明布局塌了（本项目踩过"整页白/黑洞"）
        qint64 sum = 0;
        int dark = 0;
        const int stepX = qMax(1, img.width() / 120);
        const int stepY = qMax(1, img.height() / 120);
        int n = 0;
        for (int y = 0; y < img.height(); y += stepY) {
            for (int x = 0; x < img.width(); x += stepX) {
                const QColor c = img.pixelColor(x, y);
                const int lum = (c.red() + c.green() + c.blue()) / 3;
                sum += lum;
                if (lum < 30) ++dark;
                ++n;
            }
        }
        const double darkPct = n ? 100.0 * dark / n : 0.0;
        const double avg = n ? double(sum) / n : 0.0;
        qInfo().noquote() << QStringLiteral("[probe] %1x%2 黑占比 %3% 平均亮度 %4")
                                 .arg(img.width()).arg(img.height())
                                 .arg(darkPct, 0, 'f', 1).arg(avg, 0, 'f', 1);
        // 全黑 = 没画出来
        if (darkPct > 97.0) {
            qWarning() << "整屏近乎全黑：组件没有渲染出来";
            QCoreApplication::exit(7);
            return;
        }
        QCoreApplication::exit(0);
    });
    timer->start();

    return app.exec();
}
