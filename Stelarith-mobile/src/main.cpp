// 手机端入口（2026-10-09 · Qt Quick 壳）
// Android 打包同用此入口（Qt 工具链自动处理 Android 活动）。
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "mobilebackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("StelarithMobile"));
    app.setOrganizationName(QStringLiteral("Stelarith"));

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