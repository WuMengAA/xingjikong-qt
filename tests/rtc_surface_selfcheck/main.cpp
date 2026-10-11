// 星集控 · #171 管理器侧 sink 自检（Tier0，纯 headless，不依赖真实被控端）
//
// 目的：在不跑被控端 / 不动桌面 / 不重启服务的前提下，验证「WebRTC 帧 → 管理器显示」的
// 管理器侧全链路是否接通。做法：用一个 mock backend 发 rtcFrameReady(QImage) 信号，
// 走与 Main.qml 完全相同的 Connections 接法喂给 RtcVideoSurface.onFrame，断言
// framesReceived>0。这证的是「管理器侧 sink 通」，不证「真实解码帧」（那需另机被控端联调）。
//
// 铁律边界：本程序只起管理器侧 QML 引擎（offscreen），不发任何远程指令、不连真实被控端。
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlError>
#include <QImage>
#include <QObject>
#include <QQuickItem>
#include <QColor>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

class MockBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString frameSource READ frameSource NOTIFY frameSourceChanged)
public:
    explicit MockBackend(QObject *parent = nullptr) : QObject(parent) {}
    QString frameSource() const { return QStringLiteral("rtc"); }
signals:
    void frameSourceChanged();
    // 与 viewerbackend.h:504 同签名：void rtcFrameReady(const QImage &frame)
    void rtcFrameReady(const QImage &frame);
public:
    Q_INVOKABLE void pump() {
        QImage img(64, 64, QImage::Format_ARGB32);
        img.fill(QColor(46, 158, 91)); // ok 绿，纯合成帧
        emit rtcFrameReady(img);
    }
};

int main(int argc, char *argv[]) {
    fprintf(stderr, "[selfcheck] main start\n"); fflush(stderr);
    // headless：不开真实窗口、不碰桌面
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    fprintf(stderr, "[selfcheck] QGuiApplication ok\n"); fflush(stderr);

    MockBackend mock;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mockBackend"), &mock);

    // 夹具放在 qml/ 下（与 Main.qml 同目录）：它能像 Main.qml 一样 import "components"
    // 相对解析到 RtcVideoSurface.qml；此处只需按磁盘路径加载该夹具。
    const QString qmlPath = QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-viewer-qt/qml/_selfcheck_probe.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlPath));
    if (component.status() != QQmlComponent::Ready) {
        fprintf(stderr, "[selfcheck] FATAL: QML not Ready, errors:\n"); fflush(stderr);
        for (const QQmlError &e : component.errors()) {
            fprintf(stderr, "  QML error: %s\n", e.toString().toLocal8Bit().constData());
        }
        fflush(stderr);
        return 2;
    }
    fprintf(stderr, "[selfcheck] QML component ready\n"); fflush(stderr);
    QObject *root = component.create();
    if (!root) {
        fprintf(stderr, "[selfcheck] FATAL: create() returned null\n"); fflush(stderr);
        return 2;
    }

    // QML 侧由 Timer 自驱 pump 一帧（Component.onCompleted 后 50ms）；跑一小段真实事件循环，
    // 让 Connections 绑定 + Timer 触发 + onFrame 全链路走完。
    fprintf(stderr, "[selfcheck] running event loop ~400ms for QML-driven pump...\n"); fflush(stderr);
    {
        QEventLoop loop;
        QTimer::singleShot(400, &loop, &QEventLoop::quit);
        loop.exec();
    }

    QQuickItem *surf = root->findChild<QQuickItem *>(QStringLiteral("vidSurf"));
    if (!surf) {
        fprintf(stderr, "[selfcheck] FATAL: 找不到 RtcVideoSurface(vidSurf)\n"); fflush(stderr);
        return 3;
    }

    const int frames = surf->property("framesReceived").toInt();
    const bool connected = surf->property("connected").toBool();
    fprintf(stderr, "[selfcheck] framesReceived = %d  connected = %d\n", frames, (int)connected); fflush(stderr);

    if (frames > 0) {
        fprintf(stderr, "[selfcheck] PASS: RtcVideoSurface 经 rtcFrameReady 收到合成帧，管理器侧 sink 全链路通\n"); fflush(stderr);
        return 0;
    }
    fprintf(stderr, "[selfcheck] FAIL: framesReceived == 0（Connections 接线或 onFrame 未生效）\n"); fflush(stderr);
    return 1;
}

#include "main.moc"
