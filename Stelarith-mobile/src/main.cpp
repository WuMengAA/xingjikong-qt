// 手机端入口（2026-10-09 · Qt Quick 壳）
// Android 打包同用此入口（Qt 工具链自动处理 Android 活动）。
// 2026-10-10：UI 更新——套 Qt 6.12 官方 FluentWinUI3 风格（与 QWinUI3 同思路，
// 零第三方依赖；桌面 + android 套件都自带）。
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>   // 2026-10-10：FluentWinUI3 风格（QQuickStyle::setStyle）

#include "mobilebackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("StelarithMobile"));
    app.setOrganizationName(QStringLiteral("Stelarith"));

    // ── FluentWinUI3 风格（2026-10-10）：标准 Controls 一律 WinUI3 观感 ──
    // 单独 import（不靠 QT_QUICK_CONTROLS_STYLE 全局切）——避免影响其它模块。
    // 手机端自定义组件（MobileCard/StatusDot）仍走 th 令牌，Fluent 管标准控件。
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QQmlApplicationEngine engine;

    MobileBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    QQuickWindow::setDefaultAlphaBuffer(false);

    const QUrl url(QStringLiteral("qrc:/StelarithMobile/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(url);

    if (engine.rootObjects().isEmpty()) return -1;
    backend.start();
    return app.exec();
}