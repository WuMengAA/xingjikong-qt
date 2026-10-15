// 星集控管理端 · 互动审批面板「离屏取证探针」（2026-10-15 · 组件6）
//
// 做什么：起一个**离屏** QQuickView，把一份"假桥"（_probe_bridge.qml）挂成上下文
//   属性 `bridge`（真桥也走这个上下文属性，见 src/main.cpp:501），再加载
//   _probe_shell.qml（内含**真** InteractDialog + **真**共享灵动岛 IslandRoot），
//   最后 grabWindow() 存 PNG。取到的是"学生在教室机上会看到的样子"的真实渲染。
//
// 为什么不直接跑 viewer-qt.exe：
//   ① 生产 exe 需要登录态 + 云端在跑，离屏取证环境不具备；
//   ② 本机有生产 viewer 在跑，起第二个实例会抢单实例锁（探针刻意走独立 exe）。
//
// 为什么不链真 InteractBridge：
//   它 include viewerbackend.h 并调用 cloudUrl()/viewerToken() ⇒ 链接要拖进整个
//   ViewerBackend（连带 rtc/voice/file/account…一串），探针会变得极重；
//   而它需要的数据本来就走 HTTP。这里改用等面假桥，只链 svgiconprovider + qrc。
//
// 用法（由 scripts/_interact_probe.py 驱动，别手敲）：
//   _interact_probe.exe --qml <dir> --out <png> [--light] [--tab song|blessing|feedback]
//                       [--sel N] [--kind song|blessing|feedback] [--title T] [--sub S]
//                       [--wait 1400]

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFileInfo>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QUrl>
#include <QScopedPointer>

#include "svgiconprovider.h"

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("_interact_probe"));

    // ⚠️ offscreen 平台不带中文字体：它的字体库是 freetype 版，不做 Windows 字体匹配，
    //    默认族（Sans Serif）里没有 CJK ⇒ **所有中文渲染成 tofu 方块**（只影响取证图，
    //    真实 GUI 平台下用系统字体，产品不受影响）。这里显式挂上系统中文再设默认族。
    for (const char *fp : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf",
                           "C:/Windows/Fonts/Deng.ttf"}) {
        if (QFile::exists(QString::fromLatin1(fp)))
            QFontDatabase::addApplicationFont(QString::fromLatin1(fp));
    }
    {
        QFont base = QGuiApplication::font();
        base.setFamily(QStringLiteral("Microsoft YaHei"));
        base.setPixelSize(13);
        QGuiApplication::setFont(base);
    }

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("InteractDialog offscreen probe"));
    p.addHelpOption();
    QCommandLineOption oQml(QStringLiteral("qml"), QStringLiteral("QML 目录"), QStringLiteral("dir"));
    QCommandLineOption oOut(QStringLiteral("out"), QStringLiteral("输出 PNG"), QStringLiteral("png"));
    QCommandLineOption oLight(QStringLiteral("light"), QStringLiteral("浅色主题"));
    QCommandLineOption oTab(QStringLiteral("tab"), QStringLiteral("页签"), QStringLiteral("k"),
                            QStringLiteral("song"));
    QCommandLineOption oSel(QStringLiteral("sel"), QStringLiteral("预选中行"), QStringLiteral("n"),
                            QStringLiteral("-1"));
    QCommandLineOption oKind(QStringLiteral("kind"), QStringLiteral("岛预览场景"), QStringLiteral("k"),
                             QString());
    QCommandLineOption oTitle(QStringLiteral("title"), QStringLiteral("岛标题"), QStringLiteral("t"),
                              QString());
    QCommandLineOption oSub(QStringLiteral("sub"), QStringLiteral("岛副标题"), QStringLiteral("s"),
                            QString());
    QCommandLineOption oWait(QStringLiteral("wait"), QStringLiteral("抓帧前等待 ms"),
                             QStringLiteral("ms"), QStringLiteral("1400"));
    p.addOptions({oQml, oOut, oLight, oTab, oSel, oKind, oTitle, oSub, oWait});
    p.process(app);

    const QString qmlDir = p.value(oQml);
    const QString outPng = p.value(oOut);
    if (qmlDir.isEmpty() || outPng.isEmpty()) {
        qCritical() << "need --qml and --out";
        return 1;
    }
    const bool   light = p.isSet(oLight);
    const QString tab  = p.value(oTab);
    const int    sel   = p.value(oSel).toInt();
    const QString kind = p.value(oKind);
    const QString ttl  = p.value(oTitle);
    const QString sub  = p.value(oSub);
    const int    waitMs = p.value(oWait).toInt();

    QQuickView view;
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    // image://svgicon/ —— 岛里的矢量图标走它（qrc:/icons 已在 qrc_viewericons.obj 里）
    view.engine()->addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider);

    // 假桥：先用引擎编出来，再挂成上下文属性（必须早于 shell 加载）
    QQmlComponent bcomp(view.engine(),
                        QUrl::fromLocalFile(qmlDir + QStringLiteral("/_probe_bridge.qml")));
    QScopedPointer<QObject> bridge(bcomp.create());
    if (!bridge) {
        qCritical() << "bridge create failed:" << bcomp.errorString();
        return 2;
    }
    view.rootContext()->setContextProperty(QStringLiteral("bridge"), bridge.data());

    // import 路径：qmlDir = <build>/qml/Stelarith；上一层 = <build>/qml
    view.engine()->addImportPath(qmlDir);
    view.engine()->addImportPath(QFileInfo(qmlDir).absolutePath());

    view.setSource(QUrl::fromLocalFile(qmlDir + QStringLiteral("/_probe_shell.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const QQmlError &e : view.errors())
            qCritical() << "QML ERROR:" << e.toString();
        return 3;
    }

    if (QQuickItem *root = view.rootObject()) {
        root->setProperty("probeLight", light);
        root->setProperty("probeTab", tab);
        root->setProperty("probeSel", sel);
        root->setProperty("probeKind", kind);
        root->setProperty("probeTitle", ttl);
        root->setProperty("probeSub", sub);
    }

    view.show();

    QTimer::singleShot(waitMs, &view, [&] {
        const QImage img = view.grabWindow();
        if (img.isNull()) {
            qCritical() << "grabWindow returned null";
            app.exit(4);
            return;
        }
        if (!img.save(outPng)) {
            qCritical() << "save failed:" << outPng;
            app.exit(5);
            return;
        }
        qInfo().noquote() << QStringLiteral("SAVED %1 %2x%3")
                                 .arg(outPng).arg(img.width()).arg(img.height());
        app.quit();
    });

    return app.exec();
}
