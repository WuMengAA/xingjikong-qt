// Material 3 / 手机端截图探针（一次性，验收用）：加载任意 qml 文件，
// 离屏渲染后 grabToImage 存 PNG。**不链任何业务代码**、不起生产进程。
//
// 为什么走 QQuickWidget + grabToImage（2026-10-11 教训）：
//   PrintWindow 抓的是窗口 DC，QQuickWidget 内容走独立 framebuffer ⇒ 抓到空白会误判。
//   离屏平台下 QFontDatabase::families() 返回 0 ⇒ 中文全 tofu，必须显式注册 msyh。
//
// 手机端页面读 backend.* ⇒ 本文件带 StubBackend（假数据，无网络无真云端）。
// 页面读 md3.* ⇒ 探针把 Md3.qml 实例化后注入 root 的 "md3" 属性。
// 分页/触发态：--page N（bottomNav.currentIndex）/ --dialog / --snack（findChild 按 objectName）。
//
// 用法：_md3_probe.exe [--dark] [--out <png>] [--qml <路径>] [--offset N]
//                      [--page N] [--dialog] [--snack] [--backend-stub]
#include <QApplication>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QFontDatabase>
#include <QCommandLineParser>
#include <QDebug>
#include <QDateTime>
#include <QVariantMap>
#include <QVariantList>

// ── stub backend：手机端页面/shell 读的 backend.* 全在这儿（假数据）────────
class StubBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected CONSTANT)
    Q_PROPERTY(bool loggedIn READ loggedIn CONSTANT)
    Q_PROPERTY(int onlineCount READ onlineCount CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices CONSTANT)
    Q_PROPERTY(QString accountName READ accountName CONSTANT)
    Q_PROPERTY(bool accountBusy READ accountBusy CONSTANT)
    Q_PROPERTY(QString role READ role CONSTANT)
    Q_PROPERTY(int themeMode READ themeMode CONSTANT)
    Q_PROPERTY(bool systemDark READ systemDark CONSTANT)
    Q_PROPERTY(QString currentUid READ currentUid WRITE setCurrentUid NOTIFY currentUidChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariant recordings READ recordings CONSTANT)
    Q_PROPERTY(QVariant events READ events CONSTANT)
public:
    explicit StubBackend(QObject *parent = nullptr) : QObject(parent) {}

    bool connected() const { return true; }
    bool loggedIn() const { return true; }
    int onlineCount() const { return 2; }
    QString accountName() const { return QStringLiteral("张老师"); }
    bool accountBusy() const { return false; }
    QString role() const { return QStringLiteral("teacher"); }
    int themeMode() const { return m_themeMode; }
    bool systemDark() const { return m_dark; }
    QString currentUid() const { return m_currentUid; }
    QString version() const { return QStringLiteral("0.6.30"); }
    void setCurrentUid(const QString &v) { if (m_currentUid != v) { m_currentUid = v; emit currentUidChanged(); } }
    void applyDark(bool d) { m_dark = d; }
    void setThemeMode(int m) { m_themeMode = m; }

    QVariantList devices() const {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVariantList out;
        const QString names[] = { QStringLiteral("高一1班-01"), QStringLiteral("教师机"), QStringLiteral("机房-03") };
        const QString uids[]   = { QStringLiteral("PC-CLASS-01"), QStringLiteral("PC-TEACHER-02"), QStringLiteral("PC-ROOM-03") };
        const bool onl[]       = { true, false, true };
        for (int i = 0; i < 3; ++i) {
            QVariantMap d;
            d.insert(QStringLiteral("name"), names[i]);
            d.insert(QStringLiteral("uid"), uids[i]);
            d.insert(QStringLiteral("version"), QStringLiteral("0.6.30"));
            d.insert(QStringLiteral("online"), onl[i]);
            d.insert(QStringLiteral("lastSeen"), now - i * 60000);
            out.append(d);
        }
        return out;
    }
    QVariant recordings() const {
        QVariantMap out;
        QVariantList files;
        QVariantMap f;
        f.insert(QStringLiteral("name"), QStringLiteral("rec-1013-0930.mp4"));
        f.insert(QStringLiteral("bytes"), 18432000);
        f.insert(QStringLiteral("url"), QStringLiteral(""));
        f.insert(QStringLiteral("mtimeMs"), QDateTime::currentMSecsSinceEpoch() - 3600 * 1000);
        files.append(f);
        out.insert(QStringLiteral("PC-CLASS-01"), files);
        return out;
    }
    QVariant events() const {
        QVariantList out;
        const char *msgs[] = { "PC-CLASS-01 上线", "考试模式已开启（高一1班）", "PC-ROOM-03 心跳超时" };
        const char *lvls[] = { "info", "warn", "error" };
        for (int i = 0; i < 3; ++i) {
            QVariantMap e;
            e.insert(QStringLiteral("id"), i + 1);
            e.insert(QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch() - i * 1800 * 1000);
            e.insert(QStringLiteral("level"), QString::fromLatin1(lvls[i]));
            e.insert(QStringLiteral("msg"), QString::fromUtf8(msgs[i]));
            e.insert(QStringLiteral("uid"), QStringLiteral("PC-CLASS-01"));
            out.append(e);
        }
        return out;
    }

signals:
    void recordingsChanged();
    void eventsChanged();
    void rtcFrameReady(const QVariant &img);
    void currentUidChanged();

public slots:
    void sendAction(const QString &, const QVariant &) {}
    void requestDevices() {}
    void fetchRecordings() {}
    void fetchEvents() {}
    void loginWithSite() {}
    void setRole(const QString &) {}
    void checkQmlPatch() {}

private:
    bool m_dark = false;
    int m_themeMode = 2;              // 跟随系统
    QString m_currentUid = QStringLiteral("PC-CLASS-01");
};

#include "_md3_probe.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("md3-probe"));

    // 中文字体：离屏环境不注册就是 tofu（与 _island_qml_probe 同因）
    int fid = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QStringList ffams = (fid >= 0) ? QFontDatabase::applicationFontFamilies(fid) : QStringList();
    if (!ffams.isEmpty())
        app.setFont(QFont(ffams.first()));
    qInfo("font: %s (families registered: %d)", qPrintable(ffams.value(0)), ffams.size());

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption darkOpt(QStringList() << "d" << "dark", "dark theme");
    QCommandLineOption outOpt(QStringList() << "o" << "out", "output png", "out");
    QCommandLineOption qmlOpt(QStringList() << "q" << "qml", "qml file", "qml");
    QCommandLineOption offOpt(QStringList() << "offset", "capture offset (scroll Flickable)", "offset");
    QCommandLineOption pageOpt(QStringList() << "page", "switch bottomNav page index", "page");
    QCommandLineOption dlgOpt(QStringList() << "dialog", "open confirm dialog before grab");
    QCommandLineOption snackOpt(QStringList() << "snack", "show snackbar before grab");
    parser.addOption(darkOpt);
    parser.addOption(outOpt);
    parser.addOption(qmlOpt);
    parser.addOption(offOpt);
    parser.addOption(pageOpt);
    parser.addOption(dlgOpt);
    parser.addOption(snackOpt);
    parser.process(app);
    const bool dark = parser.isSet(darkOpt);
    const QString out = parser.isSet(outOpt) ? parser.value(outOpt)
                                             : QStringLiteral("D:/Stelarith/_probe/shots/md3-light.png");
    const QString qml = parser.isSet(qmlOpt) ? parser.value(qmlOpt)
                                             : QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-viewer-qt/qml/MaterialLab.qml");

    if (!QFile::exists(qml)) {
        qWarning("QML not found: %s", qPrintable(qml));
        return 2;
    }

    // stub backend（页面从 context 读 backend.*）
    auto *stub = new StubBackend();
    stub->applyDark(dark);

    auto *w = new QQuickWidget();
    w->setResizeMode(QQuickWidget::SizeRootObjectToView);
    w->engine()->rootContext()->setContextProperty(QStringLiteral("backend"), stub);
    w->setSource(QUrl::fromLocalFile(qml));
    if (w->status() == QQuickWidget::Error) {
        QString msg;
        for (const auto &e : w->errors())
            msg += e.toString() + QStringLiteral("\n");
        QFile ef(QStringLiteral("D:/Stelarith/_probe/shots/md3-error.txt"));
        QDir().mkpath(QStringLiteral("D:/Stelarith/_probe/shots"));
        if (ef.open(QIODevice::WriteOnly | QIODevice::Truncate))
            ef.write(msg.toUtf8());
        qWarning("QML load errors:\n%s", qPrintable(msg));
        return 3;
    }
    auto *ro = w->rootObject();
    if (!ro) { qWarning("no root object"); return 5; }

    // 老签名兼容（MaterialLab 的 dark / captureOffset）
    ro->setProperty("dark", dark);
    if (parser.isSet(offOpt))
        ro->setProperty("captureOffset", parser.value(offOpt).toInt());

    // Md3 实例注入：页面有 "md3" 属性就塞（shell 自建实例的会静默失败，无妨）
    const QString md3Path = QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-viewer-qt/qml/components/Md3.qml");
    if (QFile::exists(md3Path)) {
        QQmlComponent comp(w->engine(), QUrl::fromLocalFile(md3Path));
        if (comp.status() == QQmlComponent::Ready) {
            QObject *md3Inst = comp.create();
            if (md3Inst) {
                md3Inst->setProperty("dark", dark);
                ro->setProperty("md3", QVariant::fromValue(md3Inst));
            }
        }
    }

    w->resize(390, 844);
    w->show();

    // stub 数据信号：页面在 Component.onCompleted 里连了 recordingsChanged/eventsChanged
    // 再调 fetch*（no-op）——不发信号两块区域就是空的。延迟 300ms 发，模拟首轮拉取回来。
    QTimer::singleShot(300, stub, [stub]() {
        emit stub->recordingsChanged();
        emit stub->eventsChanged();
    });

    // 分页 / 触发态：等界面稳定后再操作，动效刚好在 1200ms 抓图时定格
    if (parser.isSet(pageOpt) || parser.isSet(dlgOpt) || parser.isSet(snackOpt)) {
        QTimer::singleShot(400, [&] {
            if (parser.isSet(pageOpt)) {
                if (auto *nav = ro->findChild<QObject*>(QStringLiteral("bottomNav")))
                    nav->setProperty("currentIndex", parser.value(pageOpt).toInt());
            }
            if (parser.isSet(dlgOpt)) {
                ro->setProperty("confirmTarget", QStringLiteral("PC-CLASS-01"));
                if (auto *dlg = ro->findChild<QObject*>(QStringLiteral("confirmDialog")))
                    dlg->setProperty("opened", true);
            }
            if (parser.isSet(snackOpt)) {
                if (auto *sn = ro->findChild<QObject*>(QStringLiteral("snack")))
                    QMetaObject::invokeMethod(sn, "show", Q_ARG(QVariant, QStringLiteral("已保存到 %LOCALAPPDATA%\\xingjikong")),
                                              Q_ARG(QVariant, QStringLiteral("撤销")));
            }
        });
    }

    // 等渲染稳定再抓（动效 500ms 上限 + 字体/首帧余量）
    QTimer::singleShot(1200, [&] {
        // grabToImage 抓的是 item 自身，比 grabFramebuffer（受 widget viewport 裁切）可靠
        auto grab = ro->grabToImage();   // QSharedPointer<QQuickItemGrabResult>
        if (!grab) { qWarning("grabToImage failed"); app.exit(6); return; }
        QObject::connect(grab.get(), &QQuickItemGrabResult::ready, [&, grab] {
            const QImage pm = grab->image();
            QDir().mkpath(QFileInfo(out).absolutePath());
            const bool ok = pm.save(out);
            qInfo("grab %dx%d -> %s : %s", pm.width(), pm.height(), qPrintable(out), ok ? "OK" : "FAIL");
            app.exit(ok ? 0 : 4);
        });
    });
    return app.exec();
}
