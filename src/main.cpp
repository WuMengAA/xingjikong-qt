// 星集控 · 管理端（Qt Quick 版入口）
//
// 界面在 qml/（依据《星集控-电脑端界面稿集-黑白-2026-10-03.html》），逻辑在 ViewerBackend（C++）。
// 本文件只做三件事：
//   1) 接管日志 —— GUI 程序默认把日志丢给 OutputDebugString，什么也看不到
//   2) 把 backend 挂给 QML（context property，QML 里直接用 `backend`）
//   3) 提供画面帧给 QML 的 Image（image://frames/...）

#include "viewerbackend.h"

#include <QGuiApplication>
#include <QApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <cstdarg>
#include <cstdio>

// ── 日志：落盘 + 打屏（backend 与界面共用这一份）──
// 2026-10-04 换了个新文件名（viewer-run.log）。原因：老 viewer.log 从某次起再也没被写过，
// 而 writeLog 失败时会静默丢日志（fail-silent 红线），现象和"程序没起来"完全一样 ——
// 没法区分就没法排障。换名 + 写不进就喊出来，一次就能分清（见 writeLog）。
static const char *kLogFile = "D:/Stelarith/Stelarith-viewer-qt/viewer-run.log";
static FILE *g_log = nullptr;
static bool g_logTried = false;

static void writeLog(const char *s)
{
    if (!g_log && !g_logTried) {
        g_logTried = true;
        g_log = fopen(kLogFile, "w");
        if (!g_log) {
            // 写不进必须**喊出来**：静默丢日志的时候，现场只剩下"好像跑了但什么都没记"，
            // 比压根没日志还贵。控制台跑这一步一定看得见。
            fprintf(stderr, "[viewer] FAIL 日志写不进 %s —— 后面所有日志都会丢，请改用控制台运行\n", kLogFile);
            fflush(stderr);
            return;
        }
        fprintf(g_log, "=== viewer-qt 启动，日志=%s ===\n", kLogFile);
        fflush(g_log);
    }
    if (!g_log) return;
    fprintf(g_log, "%s\n", s);
    fflush(g_log);
}

void logf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    writeLog(buf);
    fprintf(stdout, "%s\n", buf); fflush(stdout);
}

static void installMessageHandler()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        const char *tag = "INFO";
        switch (type) {
        case QtFatalMsg:    tag = "FATAL"; break;
        case QtCriticalMsg: tag = "CRIT";  break;
        case QtWarningMsg:  tag = "WARN";  break;
        default:            tag = "INFO";  break;
        }
        const QByteArray b = msg.toUtf8();
        // QML 加载/绑定错误都会走这里，原样留着 —— 否则界面白屏时无从查起
        fprintf(stdout, "[viewer] %s: %s\n", tag, b.constData());
        fflush(stdout);
        char line[2048];
        snprintf(line, sizeof(line), "[viewer] %s: %s", tag, b.constData());
        writeLog(line);
    });
}

/**
 * 读 exe 同目录的 viewer.env（格式：`set KEY=VALUE`，`rem`/`#` 为注释）。
 *
 * **为什么要程序自己读**：管理端是"双击就用"的软件 —— 老师不该先开个 PowerShell
 * 手设环境变量才能连上（2026-10-03 实测：双击裸跑因缺令牌连不上，会被误判成"坏了"）。
 * 配置跟 exe 放一起，双击即用。做法与被控端 agent.env 完全一致（那边已经跑通）。
 *
 * 只设置**进程内**环境变量，不写系统环境；已存在的真实环境变量优先（便于临时覆盖）。
 * @returns 实际生效的键数
 */
int loadEnvFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray raw = f.readAll();

    // 编码兼容：写它的工具不确定（NSIS/记事本/PowerShell 各写各的），这里三种都认。
    //   UTF-16LE + BOM（FF FE）—— NSIS 的 FileWrite 默认就是这个
    //   UTF-8 + BOM（EF BB BF）
    //   裸 UTF-8 / ASCII
    QString text;
    if (raw.size() >= 2 && (quint8)raw.at(0) == 0xFF && (quint8)raw.at(1) == 0xFE) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                  (raw.size() - 2) / 2);
    } else if (raw.size() >= 3 && (quint8)raw.at(0) == 0xEF && (quint8)raw.at(1) == 0xBB
               && (quint8)raw.at(2) == 0xBF) {
        text = QString::fromUtf8(raw.constData() + 3, raw.size() - 3);
    } else {
        text = QString::fromUtf8(raw);
    }

    int n = 0;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();   // 顺带去掉行尾 \r
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QStringLiteral("rem"), Qt::CaseInsensitive)) continue;
        if (line.startsWith(QStringLiteral("set "), Qt::CaseInsensitive)) line = line.mid(4).trimmed();
        // 与被控端 stelarith-control-qt 同一个 bug、同一处修法（2026-10-04）：
        // `set "KEY=VALUE"` 的引号包住的是整个 KEY=VALUE，不是只包 VALUE。不先剥这层整体引号，
        // key 会解析成 "STE_VIEWER_TOKEN（带前导引号）→ qputenv 设了个没人读的变量名 →
        // 配置静默失效，表现为"能启动但连不上云端，且不报错"。
        if (line.size() >= 2 && line.startsWith(QLatin1Char('"')) && line.endsWith(QLatin1Char('"')))
            line = line.mid(1, line.size() - 2).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (val.size() >= 2 && val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))
            val = val.mid(1, val.size() - 2);
        if (key.isEmpty()) continue;
        if (!qgetenv(key.toUtf8().constData()).isEmpty()) continue;   // 真环境变量优先
        qputenv(key.toUtf8().constData(), val.toUtf8());
        n++;
    }
    return n;
}

/** 画面帧给 QML：Image.source ＝ "image://frames/f?<递增号>"（换个号即重新取帧）。 */
class FrameImageProvider : public QQuickImageProvider
{
public:
    explicit FrameImageProvider(ViewerBackend *b)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(b) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        Q_UNUSED(id);
        Q_UNUSED(requested);
        const QImage img = m_backend->frame();
        if (size) *size = img.size();
        return img;
    }

private:
    ViewerBackend *m_backend;
};

int main(int argc, char *argv[])
{
    installMessageHandler();
    // QApplication 而不是 QGuiApplication：T-3 的 WebRTC 收流要 QWebEngineView（Widgets 版），
    // 后者必须在 QApplication 上跑。QGuiApplication 下 WebEngine 初始化即崩（不是 warning，是崩）。
    // 副作用只是多链 Qt6::Widgets（本来就链了，为了 QFileDialog），没有额外负担。
    QApplication app(argc, argv);

    // 双击即用：配置（云端地址/令牌）从 exe 同目录的 viewer.env 读 —— 不要求先设环境变量。
    // 必须在 backend.start() 之前读完：start() 里立刻取 STE_VIEWER_URL / STE_VIEWER_TOKEN。
    const int envN = loadEnvFile(QCoreApplication::applicationDirPath() + QStringLiteral("/viewer.env"));
    if (envN > 0) logf("[viewer] 已从 viewer.env 读入 %d 项配置（exe 同目录）", envN);

    ViewerBackend backend;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("frames"), new FrameImageProvider(&backend));
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.loadFromModule(QStringLiteral("Stelarith"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        logf("[viewer] FAIL QML 没能加载（qml/Main.qml 有问题？）—— 退出");
        return 1;
    }

    backend.start();

    // ── T-4 真机验证驱动（仅自检开关，不改协议层）──
    // 设 STE_SELFTEST_RTC=<任意非空> 就在设备上自动订阅第一台在线设备，
    // 用来在无鼠标的环境（脚本/后台）里把「订阅 → 云端 rtc-start → 被控端 offer →
    // 这里 answer → ontrack 出画面」整条链路跑一遍。
    // 只复用 backend 已有的 public API（devices() / setCurrentUid()），一行协议代码都不碰。
    const QString rtcUidEnv = qEnvironmentVariable("STE_SELFTEST_RTC");
    if (!rtcUidEnv.isEmpty()) {
        // 3 秒：本地回环 WS 上，鉴权→拉设备表是毫秒级，3 秒足够稳（设备表到不了就明确报出来，
        // 不静默跳过 —— 静默跳过会让"没验到"被当成"验过了"）
        // STE_SELFTEST_RTC=1 或 =auto → 订阅第一台；=TEST1 这种具体设备号 → 直接订阅它。
        // 为什么要支持指定：同一时刻云端可能有多台在线（旧的截图像轮播那台 + 新的 WebRTC 那台），
        // 只订"第一台"可能订到旧的那台，验出来的是旧通道、不是 WebRTC。
        QTimer::singleShot(3000, &backend, [&backend, rtcUidEnv] {
            QString uid;
            if (rtcUidEnv == QStringLiteral("1") || rtcUidEnv.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
                const QJsonArray devs = backend.devices();
                if (devs.isEmpty()) {
                    logf("[viewer] 自检：设备表是空的，没东西可订阅（云端没在线设备？）");
                    return;
                }
                uid = devs.first().toObject().value(QStringLiteral("uid")).toString();
            } else {
                uid = rtcUidEnv;
            }
            if (uid.isEmpty()) {
                logf("[viewer] 自检：拿不到要订阅的 uid，跳过自动订阅");
                return;
            }
            // 后缀 !reset：先退订再重订一次。
            // 为什么要这个开关：被控端的采集页只会为**一个** RTCPeerConnection 发一次 offer
            // （st.state 一旦是 negotiating/streaming，__startStream 直接 return），
            // 而 offer 可能在"没人订阅这台"的时候就被发出去了，云端那条"没人订阅"直接丢包。
            // 之后再订阅也不会有新的 offer —— 表现就是"订阅成功但永远不来画面"。
            // 退订让云端发 rtc-stop、重订让云端发 rtc-start，被控端才会重建连接、重新 offer。
            bool needReset = false;
            QString uid2 = uid;
            if (uid2.endsWith(QStringLiteral("!reset"), Qt::CaseInsensitive)) {
                uid2 = uid2.left(uid2.size() - 6);
                needReset = true;
            }
            logf("[viewer] 自检：自动订阅 %s（走完整订阅链路%s）",
                 uid2.toUtf8().constData(), needReset ? "，先强制重新协商" : "");
            if (needReset) backend.rtcRenegotiate(uid2);
            else backend.setCurrentUid(uid2);
        });
    }

    // 开机自检图：**三页各抓一张**（0 概览 / 1 控制 / 2 设置）。
    // 界面到底"画出来了没有"要能自证 —— 不能拿"进程活着"当证据。
    // （窗口被别的窗口挡住时，grabWindow 依然抓得到自身内容，所以这条路可用。）
    // 自检图落盘目录。这里曾经硬编码成迁移前的旧路径（D:/Stelarith/Stelarith-viewer-qt），
    // 目录恰好还在，于是图一直被写进那个废弃工程里，当前工程下反而看不到 —— 找图找了半天。
    // 改成相对 exe 工作目录推导，跟着工程走，不再写死盘符路径。
    const QString shotDir = QDir::currentPath() + QStringLiteral("/shots");
    QDir().mkpath(shotDir);
    // 两轮 × 三页 = 6 张：第一轮黑白、第二轮浅色。
    // 深色好看、浅色发花是常见病，所以两种主题**每页都要抓**，不能只看一页。
    for (int round = 0; round < 2; ++round) {
        const bool light = (round == 1);
        for (int p = 0; p < 3; ++p) {
            const int base = 5000 + (round * 3 + p) * 900;
            // 先切页/切主题（渲染是异步的，得等一帧再抓）
            QTimer::singleShot(base, &app, [&engine, p, light] {
                if (engine.rootObjects().isEmpty()) return;
                QObject *r = engine.rootObjects().first();
                r->setProperty("page", p);
                r->setProperty("darkMode", !light);
            });
            QTimer::singleShot(base + 450, &app, [&engine, shotDir, p, light] {
                if (engine.rootObjects().isEmpty()) return;
                auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                if (!w) return;
                const QString path = light
                    ? shotDir + QStringLiteral("/qml-light%1.png").arg(p)
                    : shotDir + QStringLiteral("/qml-page%1.png").arg(p);
                if (w->grabWindow().save(path))
                    logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
                else
                    logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
            });
        }
    }

    // 第 7 张：软件弹窗（它要"触发"才出现，不点按钮就看不到 —— 所以这里替自检发一次请求）。
    // 自检用 setProperty 打开弹窗是空的（没数据），必须真走一次指令链路，抓到的才有内容。
    // 第 7/8/9 张：软件弹窗 / 媒体弹窗 / 定时弹窗。
    //
    // 这三个弹窗都是"不触发不出现"，所以自检时替真人点一下：先切回控制页（page=1），
    // 再真发一次指令（走完整链路）。这样抓到的图里是**真数据**（软件清单/媒体清单/待执行列表），
    // 不是空壳弹窗 —— 顺带也证明了"点按钮 → 指令真发出去 → 回执真把弹窗填出来"这条链路是通的。
    //
    // 【写法铁律】这里**只用单层 lambda**。踩过一次：在 singleShot 的 lambda 里再套一个 lambda、
    // 内层没写默认捕获模式时 MSVC 报 C3493「无法隐式捕获 app」，而诊断行号还指到内层那一行，
    // 看着像内层的问题、其实是嵌套写法本身错。"切页后必须等一帧才抓得稳"又必然要两段，
    // 所以一律拆成 ①发指令+切页 ②延时抓图 —— 每段一个独立单层 lambda，绝不再套。
    // 每段自己显式列出要用的捕获项（engine/app/shotDir/backend），别指望默认捕获。

    // ① 软件弹窗：列候选快捷键 + 列进程（弹窗挂在控制页，先切回来）
    QTimer::singleShot(11000, &backend, [&backend, &engine] {
        QMetaObject::invokeMethod(&backend, "sendAction",
                                  Q_ARG(QString, QStringLiteral("list_shortcut_candidates")),
                                  Q_ARG(QJsonObject, QJsonObject()));
        QMetaObject::invokeMethod(&backend, "sendAction",
                                  Q_ARG(QString, QStringLiteral("process_list")),
                                  Q_ARG(QJsonObject, QJsonObject()));
        if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("page", 1);
    });
    // ② 抓软件弹窗
    QTimer::singleShot(13500, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-softdialog.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // 第 8/9 张：媒体弹窗 + 定时弹窗（批次 2 新上的两个）。
    // 同样"不触发不出现"，所以真发一次 media_list / list_schedules ——
    // 这样抓到的图里是**真数据**（媒体清单、待执行列表），不是空壳弹窗，
    // 也顺带证明"点这些按钮 → 指令真发出去了 → 回执真把弹窗填出来了"这条链路没断。
    // ③ 媒体弹窗（批次 2 新上的）
    QTimer::singleShot(16000, &backend, [&backend, &engine] {
        QMetaObject::invokeMethod(&backend, "sendAction",
                                  Q_ARG(QString, QStringLiteral("media_list")),
                                  Q_ARG(QJsonObject, QJsonObject()));
        if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("page", 1);
    });
    // ④ 抓媒体弹窗
    QTimer::singleShot(17500, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-medialog.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // ⑤ 定时弹窗（批次 2 新上的）
    QTimer::singleShot(20000, &backend, [&backend, &engine] {
        QMetaObject::invokeMethod(&backend, "sendAction",
                                  Q_ARG(QString, QStringLiteral("list_schedules")),
                                  Q_ARG(QJsonObject, QJsonObject()));
        if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("page", 1);
    });
    // ⑥ 抓定时弹窗
    QTimer::singleShot(21500, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-schedialog.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // 第 10 张：文件推送弹窗 + 真推一次文件（**默认关闭**，见下面的 STE_SELFTEST_FILE）。
    //
    // 为什么需要这个开关：文件推送是"往教室机写文件"，比发个指令重得多 ——
    // 不能让每个老师在开机自检时都顺手推一个测试文件过去。所以只有显式设了
    // STE_SELFTEST_FILE=<某个本机文件> 才跑；跑完这块代码不留下任何副作用以外的东西。
    //
    // 这里走的是 pushFile(urlText) 这条和真人点「选文件→开始推送」完全一样的入口，
    // 所以日志里的控制台就是真链路，不是另写一套的假流程。
    // 自检文件：环境变量 → 命令行参数（后者用于临时覆盖，见下）。
    // 命令行那一路是给真机验证用的：某些环境下 $env: 设了子进程也带不进去（踩过），
    // 而真链路验证又必须能选到具体文件，所以留一个必定生效的入口。
    // argv[1]=正例、argv[2]=无扩展名负例，两个都可以单独给。
    QString selfTestFile = qEnvironmentVariable("STE_SELFTEST_FILE");
    QString selfTestNoExt = qEnvironmentVariable("STE_SELFTEST_NOEXT");
    if (argc > 1 && std::strlen(argv[1]) > 0) {
        selfTestFile = QString::fromLocal8Bit(argv[1]);
        logf("[viewer] 自检正例来自命令行 argv[1]=%s", selfTestFile.toUtf8().constData());
    }
    if (argc > 2 && std::strlen(argv[2]) > 0) {
        selfTestNoExt = QString::fromLocal8Bit(argv[2]);
        logf("[viewer] 自检负例来自命令行 argv[2]=%s", selfTestNoExt.toUtf8().constData());
    }
    // argv[3]=大文件：取消路径必须用大文件才验得出（小文件 0.15 秒就推完了，根本没机会取消）
    QString selfTestBig = qEnvironmentVariable("STE_SELFTEST_BIG");
    if (argc > 3 && std::strlen(argv[3]) > 0) {
        selfTestBig = QString::fromLocal8Bit(argv[3]);
        logf("[viewer] 自检大文件（取消路径）来自命令行 argv[3]=%s", selfTestBig.toUtf8().constData());
    }
    if (selfTestFile.isEmpty() && !qEnvironmentVariable("STE_SELFTEST_FILE").isEmpty())
        logf("[viewer] 自检正例来自环境变量 STE_SELFTEST_FILE=%s",
             qEnvironmentVariable("STE_SELFTEST_FILE").toUtf8().constData());
    const bool fileSelfTest = !selfTestFile.isEmpty();

    if (!fileSelfTest)
        logf("[viewer] 文件推送自检没开（没设 STE_SELFTEST_FILE）：只抓一张弹窗空态图，不推任何文件");

    // ⑦ 先开弹窗（走界面自己的互斥 openOnly，顺便证明这个入口是通的）
    QTimer::singleShot(24000, &app, [&engine] {
        if (engine.rootObjects().isEmpty()) return;
        QObject *r = engine.rootObjects().first();
        r->setProperty("page", 1);          // 弹窗挂在控制页，先切回来
        QMetaObject::invokeMethod(r, "openOnly", Q_ARG(QVariant, QVariant(QStringLiteral("file"))));
    });
    // ⑧ 真推一个文件
    QTimer::singleShot(24600, &backend, [&backend, selfTestFile] {
        if (selfTestFile.isEmpty()) return;   // 没开自检就别拿空串去调 pushFile
        logf("[viewer] 自检：pushFile %s", selfTestFile.toUtf8().constData());
        QMetaObject::invokeMethod(&backend, "pushFile", Q_ARG(QString, selfTestFile));
    });
    // ⑨ 抓推送中的弹窗（进度条 + 「正在推：x / y 字节」就靠这张图自证）
    QTimer::singleShot(27000, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-filedialog.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // 负例：一个**没有扩展名**的文件。本机这边不拦（扩展名是教室机那边的规矩），
    // 所以这条应该被被控端拒掉，且原因要原样出现在弹窗上 —— 这才是"该禁的确实被禁"。
    QTimer::singleShot(31000, &backend, [&backend, &engine, selfTestNoExt] {
        if (selfTestNoExt.isEmpty()) return;
        if (!engine.rootObjects().isEmpty())
            engine.rootObjects().first()->setProperty("page", 1);
        QMetaObject::invokeMethod(&backend, "pushFile", Q_ARG(QString, selfTestNoExt));
    });
    QTimer::singleShot(33000, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-filedialog-fail.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // 取消：推一个**大文件**推到一半再取消（走 file_done{total_bytes:0} 让教室机关掉接收会话
    // 并把半截文件删掉）。必须用大文件 —— 小文件回执太快（0.15 秒发完 3 片），
    // 推完才轮到"取消"，那个分支实际走的是"已完成则清空"，根本验不到取消这条路径（踩过）。
    QTimer::singleShot(36000, &backend, [&backend, &engine, selfTestBig] {
        if (selfTestBig.isEmpty()) return;
        if (!engine.rootObjects().isEmpty())
            engine.rootObjects().first()->setProperty("page", 1);
        QMetaObject::invokeMethod(&backend, "pushFile", Q_ARG(QString, selfTestBig));
    });
    // 取消放在 1 秒后（不是 0.2 秒）：本机被控端的回执太快（每片 ~30ms），
    // 文件越小越"推完才轮到取消"，那条分支走的是"已完成则清空"，等于没验取消。
    // 30 MB ≈ 480 片 ≈ 推 6 秒，1 秒时肯定还在半路上，取消才算真的拦在传输中。
    QTimer::singleShot(37000, &backend, [&backend] {
        QMetaObject::invokeMethod(&backend, "cancelPush");
    });
    QTimer::singleShot(39000, &app, [&engine, shotDir] {
        if (engine.rootObjects().isEmpty()) return;
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!w) return;
        const QString path = shotDir + QStringLiteral("/qml-filedialog-cancel.png");
        if (w->grabWindow().save(path))
            logf("[viewer] 界面自检图已存 %s", path.toUtf8().constData());
        else
            logf("[viewer] FAIL 界面自检图存失败 %s", path.toUtf8().constData());
    });

    // 控制页按钮点验：把 14 个**非破坏性**按钮真按一遍（红线的锁屏/关机/重启/定时真排不动，只静态核）。
    // 为什么要点：按钮"看得见点得到"不等于"点了真有反应"。按完要有回执 ——
    // executed / failed 都要写进日志，failed 的原因原样带上。这就是功能验收，
    // 比"编译过了"值钱得多（2026-10-04 才发现的：一批按钮只发指令、回执来了不写日志，
    // 出问题时从日志上看不出到底哪台机器没执行）。
    {
        static const char *kBtnActions[] = {
            "list_shortcut_candidates", "process_list", "list_schedules", "camera_list",
            "camera_snapshot",          "camera_record_start", "camera_record_stop",
            "media_list",               "media_session_start", "media_session_stop",
            "remote_control_start",     "remote_control_stop", "set_volume", "launch_app"
        };
        const int n = (int)(sizeof(kBtnActions) / sizeof(kBtnActions[0]));
        for (int i = 0; i < n; ++i) {
            const QString act = QString::fromLatin1(kBtnActions[i]);
            QTimer::singleShot(42000 + i * 400, &backend, [&backend, act] {
                QJsonObject params;                       // 几个需要参数的按钮单独给
                if (act == QStringLiteral("set_volume")) params.insert(QStringLiteral("value"), 50.0);
                if (act == QStringLiteral("launch_app")) params.insert(QStringLiteral("target"), QStringLiteral("notepad"));
                logf("[viewer] 按钮点验 → %s", act.toUtf8().constData());
                QMetaObject::invokeMethod(&backend, "sendAction",
                                          Q_ARG(QString, act), Q_ARG(QJsonObject, params));
            });
        }
        // 探活不是 sendAction，走它自己的入口（计时 + 状态栏），单独点一次
        QTimer::singleShot(42000 + n * 400, &backend, [&backend] {
            logf("[viewer] 按钮点验 → ping（探活）");
            QMetaObject::invokeMethod(&backend, "sendPing");
        });
    }
    if (fileSelfTest)
        logf("[viewer] 文件推送自检已挂上：正例 → %s；负例（无扩展名）→ %s",
             selfTestFile.toUtf8().constData(), selfTestNoExt.toUtf8().constData());

    return app.exec();
}
