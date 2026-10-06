// 星集控 · 管理端（Qt Quick 版入口）
//
// 界面在 qml/（依据《星集控-电脑端界面稿集-黑白-2026-10-03.html》），逻辑在 ViewerBackend（C++）。
// 本文件只做四件事：
//   1) 接管日志 —— GUI 程序默认把日志丢给 OutputDebugString，什么也看不到
//   2) 把 backend 挂给 QML（context property，QML 里直接用 `backend`）
//   3) 提供画面帧给 QML 的 Image（image://frames/...）
//   4) 托盘常驻入口（2026-10-06 加）—— 原先后台在收流，界面上没有任何"我还活着"的入口

#include "singleinstance.h"
#include "viewerbackend.h"

// ── 课表编辑器模块（从 schedule-qt 并入，2026-10-06）──
#include "schedule/profile.h"
#include "schedule/profile_repository.h"
#include "schedule/schedule_model.h"
#include "schedule/timeslot_model.h"
#include "schedule/subject_model.h"
#include "schedule/undo_manager.h"
#include "schedule_view.h"          // 2026-10-06：大屏今日课表窗口
#include "schedule_today_provider.h" // 2026-10-06：概览页今日课表数据
#include "file_dialogs.h"            // 2026-10-06：课表编辑器文件选择

#include <QApplication>   // 2026-10-06：要托盘必须 QApplication（QSystemTrayIcon 属 QtWidgets）。
                          // Qt6Widgets 本来就链了（styles/Qt6Widgets.dll 也在绿色包里），
                          // 所以换 app 类型不增加任何打包负担。
#include <QWindow>        // 2026-10-06：托盘点击切换主窗口显示/隐藏
#include <QImage>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QTimer>
#include <QDir>          // 2026-10-06 SPIKE：从磁盘加载 QML 需要定位 qml 目录
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QDesktopServices>
#include <QUrl>
#include <cstdarg>
#include <cstdio>

// ⚠️ 管理端应用版本号的**唯一真源**在下面 kViewerVersion 处（0.6.2，2026-10-06 并入集控页后统一）。
// 合并前两份副本各有一份常量（外层 0.6.0 / 内层 0.5.0），同一文件里出现两次会
// C2374 重定义 —— 现在只留下面那一份。改版本只改那处。
// ⚠️ MSI 那条路（msi/viewer-qt.wxs）里的 Version 还是 0.5.0、长期没跟着走 ——
//    **当前发布走绿色 zip，不经过 MSI**；哪天要出 MSI 必须先补那个版本号。

// ── 日志：落盘 + 打屏（backend 与界面共用这一份）──
// 2026-10-04 换了个新文件名（viewer-run.log）。原因：老 viewer.log 从某次起再也没被写过，
// 而 writeLog 失败时会静默丢日志（fail-silent 红线），现象和"程序没起来"完全一样 ——
// 没法区分就没法排障。换名 + 写不进就喊出来，一次就能分清（见 writeLog）。
// ⚠️ 2026-10-06 修（真机级）：这里原本**硬编码成开发机的工程路径**
//   `D:/Stelarith/Stelarith-viewer-qt/viewer-run.log`。
// 后果：装到老师机/教室机上那个目录**根本不存在** → fopen("w") 直接失败 →
//   绿色包在真机上**一个字的文件日志都不写**（只剩 stdout/stderr，而双击启动时没人看得到）。
//   现场表现就是"程序好像跑了但什么都没记"，比压根没日志还难查。
// 现在：默认跟 exe 走（exe 同目录的 viewer-run.log），并留 STE_VIEWER_LOG 供部署时显式指定。
static QString logFilePath()
{
    const QString env = QString::fromLocal8Bit(qgetenv("STE_VIEWER_LOG")).trimmed();
    if (!env.isEmpty()) return env;
    return QCoreApplication::applicationDirPath() + QStringLiteral("/viewer-run.log");
}

static FILE *g_log = nullptr;
static bool g_logTried = false;

static void writeLog(const char *s)
{
    if (!g_log && !g_logTried) {
        g_logTried = true;
        const QByteArray pathLocal = logFilePath().toLocal8Bit();
        g_log = fopen(pathLocal.constData(), "w");
        if (!g_log) {
            // 写不进必须**喊出来**：静默丢日志的时候，现场只剩下"好像跑了但什么都没记"，
            // 比压根没日志还贵。控制台跑这一步一定看得见。
            fprintf(stderr, "[viewer] FAIL 日志写不进 %s —— 后面所有日志都会丢，请改用控制台运行\n",
                    pathLocal.constData());
            fflush(stderr);
            return;
        }
        fprintf(g_log, "=== viewer-qt 启动，日志=%s ===\n", pathLocal.constData());
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

// ── 托盘气泡钩子（2026-10-06）──────────────────────────────────────────────
// 「提醒」那两个开关（操作完成提示 / 设备离线提示）弹气泡走这里。
// 为什么不在 ViewerBackend 里自己 new 一个 QSystemTrayIcon：Qt 6.8 把
// QSystemTrayIcon::find() 拿掉了（只剩 isSystemTrayAvailable / supportsMessages），
// 拿不回 main 建的常驻托盘；再造一个图标，桌面 tray 区就会出现两个星集控图标。
// 所以托盘指针登记在这个文件作用域里，后端只调 stelarithNotifyTray()。
static QSystemTrayIcon *g_notifyTray = nullptr;

void stelarithNotifyTray(const QString &title, const QString &msg)
{
    if (!g_notifyTray) return;
    g_notifyTray->showMessage(title, msg, QSystemTrayIcon::Information, 4000);
}

// ⚠️ 管理端应用版本号的**唯一真源是 CMakeLists.txt 的 VIEWER_VERSION**（2026-10-06 修）。
// 修之前这里也硬编码一份 0.6.2：CMake 那侧已经是 0.6.3 → 日志/托盘/关于页互相打架
// （"关于"显示 0.6.3、首行日志显示 0.6.2），这正是"这软件还没打磨就发出去"的痕迹。
// CMakeLists.txt 在配置阶段会读这一行来校验两处一致，不一致直接 FATAL_ERROR ——
// 别靠人记着同步，靠构建卡住（上面就是没卡住才漂到的）。
// ⚠️ 保持这一行**单行**：跨行写（#ifdef 套宏）会让 CMake 的正则匹配不到，校验就白做了。
static constexpr const char *kViewerVersion = "0.6.9";

int main(int argc, char *argv[])
{
    installMessageHandler();
    // QApplication（不是 QGuiApplication）：托盘 QSystemTrayIcon 属 QtWidgets，
    // 只有 QApplication 才建得起来。Qt6Widgets.dll 与 styles/ 本来就在绿色包里（为了 QFileDialog），
    // 所以这一步不增加任何打包负担、也不需要动 CMakeLists 的依赖表。
    QApplication app(argc, argv);
    // 程序名必须显式定死：凭据（网站会话令牌 / 云端接入票）落在
    // AppLocalDataLocation，Windows 上就是 %LOCALAPPDATA%/<程序名>。
    // 默认程序名是 exe 文件名（viewer-qt），换 exe 名或改名打包就会换一个目录、
    // 凭据读不到（表现为"每次重启都要重新登录"），这里钉死成一眼认得出的名字。
    QCoreApplication::setApplicationName(QStringLiteral("Stelarith Viewer"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kViewerVersion));
    logf("[viewer] viewer-qt 版本 %s（日志：%s）", kViewerVersion, logFilePath().toLocal8Bit().constData());

    // ── 后台托盘常驻（2026-10-06 补）────────────────────────────────────────
    // 关主窗口不退出程序：管理端是常驻后台的工具，主窗口关了进程必须还在（收流/托盘活着），
    // 只有托盘菜单「退出管理端」才真正退出。与被控端 control-qt 同一约定（L3796）。
    app.setQuitOnLastWindowClosed(false);

    // ── 单机只允许一个管理端（2026-10-05 与被控端同款新增）──────────────────
    // 场景：桌面快捷方式 + 登录自启 + 老师又手点一次 → 两个实例抢同一台老师的屏幕，
    // 画面/日志互相覆盖。命名互斥体用 Global\ 优先（跨会话，兼容计划任务 Session 0），
    // 外加一层 %LOCALAPPDATA% 文件锁兜底 —— 只写 Local\ 会失效（见 singleinstance.h）。
    // guard 必须活到 main 结束：析构才放手。
    SingleInstanceGuard guard;
    QString guardWhy;
    if (!guard.acquire(L"StelarithViewerQt_Singleton", L"viewer.lock", &guardWhy)) {
        logf("[viewer] 已有另一个管理端在跑（%s）→ 本次启动退出", guardWhy.toUtf8().constData());
        QMessageBox::information(nullptr, QStringLiteral("星集控 · 管理端"),
                                 QStringLiteral("管理端已经在运行了。\n本次启动自动退出：%1").arg(guardWhy));
        return 0;
    }

    // 双击即用：配置（云端地址/令牌）从 exe 同目录的 viewer.env 读 —— 不要求先设环境变量。
    // 必须在 backend.start() 之前读完：start() 里立刻取 STE_VIEWER_URL / STE_VIEWER_TOKEN。
    const int envN = loadEnvFile(QCoreApplication::applicationDirPath() + QStringLiteral("/viewer.env"));
    if (envN > 0) logf("[viewer] 已从 viewer.env 读入 %d 项配置（exe 同目录）", envN);

    ViewerBackend backend;

    // ── 课表编辑器模型（2026-10-06 并入）──────────────────────────────
    // 加载 ClassIsland 档案（默认 %LOCALAPPDATA%/ClassIsland/data/Profiles/Default.json），
    // 为空则生成示例档案；模型持有 Profile 可变引用，编辑即改内存，点保存落盘。
    ProfileRepository schedRepo;
    Profile schedProfile = schedRepo.loadDefault();
    if (schedProfile.classPlans.isEmpty()) {
        logf("[viewer] 课表档案为空，生成示例档案（%s）",
             qPrintable(schedRepo.defaultProfilePath()));
        // 构造一份最小示例（3 节时间点 + 3 科目 + 2 套课表群）
        schedProfile = Profile();
        schedProfile.name = QStringLiteral("示例档案");
        schedProfile.id = QUuid::createUuid().toString();
        auto mkSlot = [](const QString& n, int h1, int m1, int h2, int m2) {
            TimeSlot ts; ts.id = QUuid::createUuid().toString(); ts.name = n;
            ts.startTime = QTime(h1, m1); ts.endTime = QTime(h2, m2);
            ts.timeType = 0; ts.isActive = true; return ts;
        };
        TimeSlot ts1 = mkSlot(QStringLiteral("第一节"), 8,0, 8,45);
        TimeSlot ts2 = mkSlot(QStringLiteral("第二节"), 8,55, 9,40);
        TimeSlot ts3 = mkSlot(QStringLiteral("第三节"), 10,10, 10,55);
        schedProfile.timeSlots.insert(ts1.id, ts1);
        schedProfile.timeSlots.insert(ts2.id, ts2);
        schedProfile.timeSlots.insert(ts3.id, ts3);
        auto mkSub = [](const QString& n) { Subject s; s.id = QUuid::createUuid().toString();
            s.name = n; s.simplifiedName = n.left(1); return s; };
        Subject s1 = mkSub(QStringLiteral("语文"));
        Subject s2 = mkSub(QStringLiteral("数学"));
        Subject s3 = mkSub(QStringLiteral("英语"));
        schedProfile.subjects.insert(s1.id, s1);
        schedProfile.subjects.insert(s2.id, s2);
        schedProfile.subjects.insert(s3.id, s3);
        auto mkPlan = [&](const QString& name, int wd, const QList<QString>& subs) {
            ClassPlan cp; cp.id = QUuid::createUuid().toString(); cp.name = name;
            cp.weekDay = wd; cp.weekCountDiv = 1; cp.weekCountDivTotal = 1; cp.isActive = true;
            int i = 0;
            for (const QString& sid : subs) {
                cp.lessons.append(Lesson{sid, wd, i++, 1, 1, true});
            }
            return cp;
        };
        ClassPlan c1 = mkPlan(QStringLiteral("周一"), 1, {s1.id, s2.id, s3.id});
        ClassPlan c2 = mkPlan(QStringLiteral("周二"), 2, {s2.id, s3.id, s1.id});
        schedProfile.classPlans.insert(c1.id, c1);
        schedProfile.classPlans.insert(c2.id, c2);
        ClassPlanGroup grp;
        grp.id = QUuid::createUuid().toString();
        grp.name = QStringLiteral("默认课表"); grp.isActive = true;
        grp.classPlanIds = {c1.id, c2.id};
        schedProfile.classPlanGroups.insert(grp.id, grp);
        schedProfile.selectedClassPlanGroupId = grp.id;
    }

    ScheduleModel schedModel(schedProfile);
    schedModel.setCurrentWeek(ScheduleModel::weekFromDate(QDate::currentDate()));
    TimeSlotModel schedTimeSlotModel;
    {
        QList<TimeSlot> active;
        for (const auto& ts : schedProfile.timeSlots) {
            if (ts.isActive) active.append(ts);
        }
        schedTimeSlotModel.setSlots(active);
    }
    SubjectModel schedSubjectModel(schedProfile.subjects);
    schedSubjectModel.setProfileRef(&schedProfile);
    UndoManager schedUndo(&schedModel, &schedTimeSlotModel, &schedSubjectModel);

    // 概览页「今日课表」数据提供者（读真机 ClassIsland 档案）
    ScheduleTodayProvider schedToday;
    schedToday.setProfilePath(schedRepo.defaultProfilePath());

    // ── 控件风格钉成 Basic（2026-10-06 打磨）────────────────────────────
    // 默认风格（Windows 那套）不支持覆写 background，于是运行时刷一堆
    //   "QQuickRectangle: The current style does not support customization of this control"
    // —— 弹窗/日志框里那些自绘背景全被它warn，而界面本身是黑白自绘的，根本用不上默认外观。
    // 顺带 Basic 只需要 QtQuickControls2Basic.dll（现在包里塞了 Basic/Fusion/Imagine/
    // Material/Universal/FluentWinUI3 六套，全是被这个默认风格拖进来的）。
    // 写进进程环境而不是 QQuickStyle::setStyle：这条变量由 QtQuick.Controls 的
    // 风格插件在加载时读，越早设越好（main 开头、任何 QML 加载之前）。
    // 效果同钉死风格，但不额外链接 QtQuickControls2 到本工程 —— 那个模块本来
    // 只是随 QQC2 插件一起部署，没必拉进 exe 的依赖表。
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("frames"), new FrameImageProvider(&backend));
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    // 课表编辑器 context property（2026-10-06 并入）
    engine.rootContext()->setContextProperty(QStringLiteral("scheduleModel"), &schedModel);
    engine.rootContext()->setContextProperty(QStringLiteral("timeSlotModel"), &schedTimeSlotModel);
    engine.rootContext()->setContextProperty(QStringLiteral("subjectModel"), &schedSubjectModel);
    engine.rootContext()->setContextProperty(QStringLiteral("undoManager"), &schedUndo);
    engine.rootContext()->setContextProperty(QStringLiteral("profilePath"), schedRepo.defaultProfilePath());
    engine.rootContext()->setContextProperty(QStringLiteral("scheduleToday"), &schedToday);
    // 课表编辑器文件选择对话框
    FileDialogs fileDlgs;
    engine.rootContext()->setContextProperty(QStringLiteral("fileDialogs"), &fileDlgs);
    // 2026-10-06：QML 从**磁盘**加载，不再从 exe 里的资源读。
    // 为什么改：QML 编进 exe（qt_add_qml_module 的 QML_FILES）意味着改一个按钮文案都要重出整包，
    // 对教室里的管理端基本等于「改不了」。改成读磁盘后，界面层可作为补丁包单独下发（热更）。
    // 实测（2026-10-06 spike）：裸类型名（SoftwareDialog / LogDialog …）在磁盘模式下照样能解析 ——
    // 只要 addImportPath 指向同目录，Qt 就按文件名把 .qml 当类型用；同模块自解析这条没受影响。
    // ⚠️ qml/Stelarith/ 是子目录（qml/ 根留给 windeployqt 的 Qt 插件），构建时由 CMake 拷过去。
    const QString qmlDir = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("qml/Stelarith"));
    engine.addImportPath(qmlDir);
    engine.load(QUrl::fromLocalFile(qmlDir + QStringLiteral("/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        logf("[viewer] FAIL QML 没能加载（qml/Main.qml 有问题？）—— 退出");
        return 1;
    }

    backend.start();

    // ── 托盘常驻（2026-10-06 补）───────────────────────────────────────────
    // 为什么补：管理端在后台收流，任务栏里既没有窗口也没有图标 —— 老师机上"它还在不在"
    // 原先没有任何肉眼可见的入口，出问题只能靠翻日志文件。
    // 图标用 QPainter 现画（不依赖外部 .ico，省一个资源文件，也就少一处"打包漏了"）。
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(QColor(20, 130, 200));
            p.setPen(QPen(QColor(235, 235, 235), 3));
            p.drawRect(6, 6, 20, 20);
        }

        auto *tray = new QSystemTrayIcon(QIcon(pm), &app);
        tray->setToolTip(QStringLiteral("星集控 · 管理端 v%1").arg(QString::fromLatin1(kViewerVersion)));
        g_notifyTray = tray;   // 登记：设置页那两个「提醒」开关的气泡走它

        auto *menu = new QMenu();
        auto *actStatus = menu->addAction(QStringLiteral("状态：运行中"));
        actStatus->setEnabled(false);                       // 只显示，不可点
        menu->addSeparator();
        // 网站账号登录（2026-10-06）：登录入口必须有一个"看得见、点得到"的地方。
        // 管理端常年缩在托盘里，账号过期这种事绝不能只躺在日志里 —— 就挂在这个菜单上。
        auto *actLogin = menu->addAction(QStringLiteral("用网站账号登录"));
        QObject::connect(actLogin, &QAction::triggered, &backend, [&backend] {
            logf("[viewer] 用户点了托盘的「用网站账号登录」");
            backend.loginWithSite();
        });
        auto *actForget = menu->addAction(QStringLiteral("退出网站登录"));
        QObject::connect(actForget, &QAction::triggered, &backend, [&backend] {
            backend.forgetAccount();
        });
        menu->addSeparator();
        // 今日课表（2026-10-06）：老师机也能看全天课程，与被控端同一窗口；
        // 读 ClassIsland 档案（可配 STE_VIEWER_PROFILE，默认 %LOCALAPPDATA%/ClassIsland/...）
        auto *actSched = menu->addAction(QStringLiteral("今日课表…"));
        QObject::connect(actSched, &QAction::triggered, &app, [] {
            const QString profilePath = qEnvironmentVariable(
                "STE_VIEWER_PROFILE",
                QStringLiteral("%1/ClassIsland/data/Profiles/Default.json")
                    .arg(qEnvironmentVariable("LOCALAPPDATA")));
            auto *win = new ScheduleViewWindow();
            win->setAttribute(Qt::WA_DeleteOnClose);
            win->setProfilePath(profilePath);
            win->show();
        });
        menu->addSeparator();
        auto *actLog = menu->addAction(QStringLiteral("打开日志目录"));
        QObject::connect(actLog, &QAction::triggered, &app, [] {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(logFilePath()).absolutePath()));
        });
        menu->addSeparator();
        auto *actQuit = menu->addAction(QStringLiteral("退出管理端"));
        QObject::connect(actQuit, &QAction::triggered, &app, &QApplication::quit);

        tray->setContextMenu(menu);
        // 点击托盘图标：主窗口 显示↔隐藏 切换（常驻托盘的标准交互）
        QObject::connect(tray, &QSystemTrayIcon::activated, &app, [&engine, tray](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                return;
            auto *win = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QWindow *>(engine.rootObjects().first());
            if (!win) return;
            if (win->isVisible()) {
                win->hide();
            } else {
                win->show();
                win->raise();
                win->requestActivate();
            }
            Q_UNUSED(tray);
        });
        tray->show();          // 先设图标再 show，避免 "No Icon set" 警告
        logf("[viewer] 托盘已就绪（v%s）", kViewerVersion);
    } else {
        // 老实说清楚：这不叫"被任务栏溢出区折叠"，是这儿压根建不了（典型 Session0）。
        logf("[viewer] WARN 系统托盘不可用（Session0？）—— 收流不受影响，但没有可见入口");
    }

    return app.exec();
}
