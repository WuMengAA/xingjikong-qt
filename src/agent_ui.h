// 星集控被控端 · QML 控制面板（U5「两端同 shell」A-3 · 2026-10-16）
//
// 被控端原本是「托盘 + 7 个 QtWidgets QDialog + 一颗岛」——全项目唯一没走 QML/MD3
// 的界面。本文件把 A-3 的第一步落地：**建一个 QML（MD3）控制面板窗口**，
// 由共享 `ui/UiPageHost.qml` 装页，页声明在 `qml/shell/AgentPages.js`。
// 与 QtWidgets 版对话框的关系：并存过渡 —— 托盘「控制面板…」开新面板，
// 旧 QDialog 菜单项原样保留，逐页迁完一批再撤一批（每批都要可回退）。
//
// ⚠️⚠️ 本文件的结构是**被 moc 逼出来的**（三连坑全部实测）：
//   1. AUTOMOC 会把本头**单独编译成一个 TU**（moc_agent_ui.cpp include 本头）——
//      所以本头里**不能引用 main.cpp 的任何符号**（kAppVersion/g_uid/static 函数…），
//      否则那个 TU 里全是 C2065。原设计是 header-only 直调 main.cpp 的 static 函数，
//      首编即炸（C2065）。
//   2. 头里也不能写 `#include "agent_ui.moc"` —— AUTOMOC 对被源文件 include 的头
//      走「独立编译 moc_<name>.cpp」路线，不生成 <name>.moc ⇒ C1083。
//   3. 头必须**显式列入 CMakeLists 的 add_executable 源列表** —— 只被 main.cpp
//      include 时 AUTOMOC 的 parse 缓存不可靠，moc 产物根本不生成 ⇒ LNK2001。
//   ⇒ 数据面走「快照 + 信号」：AgentUi 只持**纯内存成员**（inline getter/setter
//      都不引用外部符号），真实读写由 main.cpp 的 agentUiWire() 接线完成。
//
// 数据流：
//   QML 读 → inline getter（成员快照，fill 时统一刷新）
//   QML 写（agentUi.xxx = v）→ inline setter（先写成员 + 发 switchChanged）
//                             → main.cpp 接收端真实写（QSettings/岛/提权）
//                             → agentUiFill() 重读快照 + publish()（失败自动回弹）
//   打开面板（openPanel）→ refreshRequested → main.cpp 重读快照（连接/升级状态是
//   "点开那一刻"的，不追信号——main.cpp 里改 g_registered 的点太多，侵入不值）

#pragma once

#include <QObject>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPointer>
#include <QUrl>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QRect>

class AgentUi : public QObject
{
    Q_OBJECT
    // 信息（只读；每次打开面板 / 写完开关后整体重读）
    Q_PROPERTY(QString appVersion   READ appVersion    NOTIFY infoChanged)
    Q_PROPERTY(QString uid          READ uid           NOTIFY infoChanged)
    Q_PROPERTY(QString cloudUrl     READ cloudUrl      NOTIFY infoChanged)
    Q_PROPERTY(QString connState    READ connState     NOTIFY infoChanged)
    Q_PROPERTY(QString dataDir      READ dataDir       NOTIFY infoChanged)
    Q_PROPERTY(QString logPath      READ logPath       NOTIFY infoChanged)
    Q_PROPERTY(QString latestText   READ latestText    NOTIFY infoChanged)
    Q_PROPERTY(bool    hasNewer     READ hasNewer      NOTIFY infoChanged)
    Q_PROPERTY(bool    canUpgrade   READ canUpgrade    NOTIFY infoChanged)
    Q_PROPERTY(QString updateNotes  READ updateNotes   NOTIFY infoChanged)
    Q_PROPERTY(QString otaModeText  READ otaModeText   NOTIFY infoChanged)
    Q_PROPERTY(QString otaAutoText  READ otaAutoText   NOTIFY infoChanged)
    Q_PROPERTY(QString otaLastText  READ otaLastText   NOTIFY infoChanged)
    Q_PROPERTY(QString otaNextText  READ otaNextText   NOTIFY infoChanged)
    Q_PROPERTY(bool    otaPausedNow READ otaPausedNow  NOTIFY infoChanged)
    // ── A-3 第二批：自检 / 插件市场 / 记录 / 播放时段 ──
    Q_PROPERTY(QVariantList selfCheckItems READ selfCheckItems NOTIFY infoChanged)
    Q_PROPERTY(QVariantList plugins        READ plugins        NOTIFY infoChanged)
    // ── 内置模块层（2026-10-16「模块化插件」）：编译进 agent 的官方模块，可启停不可卸载 ──
    Q_PROPERTY(QVariantList modules        READ modules        NOTIFY infoChanged)
    Q_PROPERTY(QString      noiseLogText   READ noiseLogText   NOTIFY infoChanged)
    Q_PROPERTY(QString      studyLogText   READ studyLogText   NOTIFY infoChanged)
    Q_PROPERTY(bool         playScheduleEnabled READ playScheduleEnabled WRITE setPlayScheduleEnabled NOTIFY infoChanged)
    Q_PROPERTY(QString      playWindowsText     READ playWindowsText     WRITE setPlayWindowsText     NOTIFY infoChanged)
    Q_PROPERTY(QString      playStateText       READ playStateText       NOTIFY infoChanged)
    // 开关（QML 写 → switchChanged → main.cpp 真实写 → fill 回弹）
    Q_PROPERTY(bool notifyEnabled   READ notifyEnabled   WRITE setNotifyEnabled   NOTIFY infoChanged)
    Q_PROPERTY(bool voiceEnabled    READ voiceEnabled    WRITE setVoiceEnabled    NOTIFY infoChanged)
    Q_PROPERTY(bool noiseEnabled    READ noiseEnabled    WRITE setNoiseEnabled    NOTIFY infoChanged)
    Q_PROPERTY(bool islandIdle      READ islandIdle      WRITE setIslandIdle      NOTIFY infoChanged)
    Q_PROPERTY(bool lightMode       READ lightMode       WRITE setLightMode       NOTIFY infoChanged)
    Q_PROPERTY(bool autostart       READ autostart       WRITE setAutostart       NOTIFY infoChanged)
    Q_PROPERTY(bool desktopShortcut READ desktopShortcut WRITE setDesktopShortcut NOTIFY infoChanged)

public:
    explicit AgentUi(QObject *parent = nullptr) : QObject(parent) {}

    // ── getter（纯成员；moc 的属性系统会引用它们的地址）──
    QString appVersion() const { return m_appVersion; }
    QString uid() const { return m_uid; }
    QString cloudUrl() const { return m_cloudUrl; }
    QString connState() const { return m_connState; }
    QString dataDir() const { return m_dataDir; }
    QString logPath() const { return m_logPath; }
    QString latestText() const { return m_latestText; }
    bool hasNewer() const { return m_hasNewer; }
    bool canUpgrade() const { return m_canUpgrade; }
    QString updateNotes() const { return m_updateNotes; }
    QString otaModeText() const { return m_otaModeText; }
    QString otaAutoText() const { return m_otaAutoText; }
    QString otaLastText() const { return m_otaLastText; }
    QString otaNextText() const { return m_otaNextText; }
    bool otaPausedNow() const { return m_otaPaused; }
    bool notifyEnabled() const { return m_notifyEnabled; }
    bool voiceEnabled() const { return m_voiceEnabled; }
    bool noiseEnabled() const { return m_noiseEnabled; }
    bool islandIdle() const { return m_islandIdle; }
    bool lightMode() const { return m_lightMode; }
    bool autostart() const { return m_autostart; }
    bool desktopShortcut() const { return m_desktopShortcut; }
    QVariantList selfCheckItems() const { return m_selfCheckItems; }
    QVariantList plugins() const { return m_plugins; }
    QVariantList modules() const { return m_modules; }
    QString noiseLogText() const { return m_noiseLogText; }
    QString studyLogText() const { return m_studyLogText; }
    bool playScheduleEnabled() const { return m_playScheduleEnabled; }
    QString playWindowsText() const { return m_playWindowsText; }
    QString playStateText() const { return m_playStateText; }

    // ── setter（写成员 + 通知；真实持久化在 main.cpp 的 switchChanged 接收端）──
    void setNotifyEnabled(bool v)   { if (m_notifyEnabled == v) return;   m_notifyEnabled = v;   emit infoChanged(); emit switchChanged(QStringLiteral("notifyEnabled"), v); }
    void setVoiceEnabled(bool v)    { if (m_voiceEnabled == v) return;    m_voiceEnabled = v;    emit infoChanged(); emit switchChanged(QStringLiteral("voiceEnabled"), v); }
    void setNoiseEnabled(bool v)    { if (m_noiseEnabled == v) return;    m_noiseEnabled = v;    emit infoChanged(); emit switchChanged(QStringLiteral("noiseEnabled"), v); }
    void setIslandIdle(bool v)      { if (m_islandIdle == v) return;      m_islandIdle = v;      emit infoChanged(); emit switchChanged(QStringLiteral("islandIdle"), v); }
    void setLightMode(bool v)       { if (m_lightMode == v) return;       m_lightMode = v;       emit infoChanged(); emit switchChanged(QStringLiteral("lightMode"), v); }
    void setAutostart(bool v)       { if (m_autostart == v) return;       m_autostart = v;       emit infoChanged(); emit switchChanged(QStringLiteral("autostart"), v); }
    void setDesktopShortcut(bool v) { if (m_desktopShortcut == v) return; m_desktopShortcut = v; emit infoChanged(); emit switchChanged(QStringLiteral("desktopShortcut"), v); }
    void setPlayScheduleEnabled(bool v) { if (m_playScheduleEnabled == v) return; m_playScheduleEnabled = v; emit infoChanged(); emit switchChanged(QStringLiteral("playScheduleEnabled"), v); }
    void setPlayWindowsText(const QString &v) { if (m_playWindowsText == v) return; m_playWindowsText = v; emit infoChanged(); }

    // 快照写入专用（main.cpp 的 agentUiFill 经 friend 直接改成员，不触发 switchChanged）
    friend void agentUiFill(AgentUi *ui);

public slots:
    /** QML/打开面板时重读快照：发请求，main.cpp 接住后 fill + publish。 */
    void refresh() { emit refreshRequested(); }
    /** 快照已变 → 让 QML 绑定重算。 */
    void publish() { emit infoChanged(); }
    void openLogDir() { emit openLogDirRequested(); }
    void openDataDir() { emit openDataDirRequested(); }
    void upgradeNow() { emit upgradeNowRequested(); }
    void toggleOtaPause() { emit toggleOtaPauseRequested(); }
    // ── A-3 第二批 ──
    void runSelfCheckCheck() { emit selfCheckRequested(); }
    void selfCheckFix() { emit selfCheckFixRequested(); }
    void pluginsRefresh() { emit pluginsRefreshRequested(); }
    void pluginInstall(const QString &id) { emit pluginInstallRequested(id); }
    void pluginEnable(const QString &id, bool enable) { emit pluginEnableRequested(id, enable); }
    void pluginUninstall(const QString &id) { emit pluginUninstallRequested(id); }
    void pluginInstallSample() { emit pluginSampleInstallRequested(); }
    // ── 内置模块层 ──
    void modulesRefresh() { emit modulesRefreshRequested(); }
    void moduleEnable(const QString &id, bool enable) { emit moduleEnableRequested(id, enable); }
    void openNoiseFolder() { emit noiseFolderRequested(); }
    void openStudyFolder() { emit studyFolderRequested(); }
    void savePlaySchedule() { emit playScheduleSaveRequested(); }

signals:
    void infoChanged();
    void refreshRequested();
    void switchChanged(const QString &key, bool value);
    void openLogDirRequested();
    void openDataDirRequested();
    void upgradeNowRequested();
    void toggleOtaPauseRequested();
    // ── A-3 第二批 ──
    void selfCheckRequested();
    void selfCheckFixRequested();
    void pluginsRefreshRequested();
    void pluginInstallRequested(const QString &id);
    void pluginEnableRequested(const QString &id, bool enable);
    void pluginUninstallRequested(const QString &id);
    void pluginSampleInstallRequested();
    // ── 内置模块层 ──
    void modulesRefreshRequested();
    void moduleEnableRequested(const QString &id, bool enable);
    void noiseFolderRequested();
    void studyFolderRequested();
    void playScheduleSaveRequested();

private:
    QString m_appVersion, m_uid, m_cloudUrl, m_connState, m_dataDir, m_logPath;
    QString m_latestText, m_updateNotes, m_otaModeText, m_otaAutoText, m_otaLastText, m_otaNextText;
    bool m_hasNewer = false, m_canUpgrade = false, m_otaPaused = false;
    bool m_notifyEnabled = true, m_voiceEnabled = true, m_noiseEnabled = true;
    bool m_islandIdle = true, m_lightMode = false, m_autostart = false, m_desktopShortcut = false;
    // ── A-3 第二批 ──
    QVariantList m_selfCheckItems, m_plugins, m_modules;
    QString m_noiseLogText, m_studyLogText, m_playWindowsText, m_playStateText;
    bool m_playScheduleEnabled = false;
};

// 由 main.cpp 提供（那里能访问匿名域的 static 自由函数与 AgentSettings）：
// 填快照（agentUiFill，friend）+ 接全部请求信号（agentUiWire）。AgentShell 构造时调用。
void agentUiWire(AgentUi *ui);

// ─────────────────────────────────────────────────────────────────────────────
// AgentShell：QML 控制面板窗口（QQuickWidget 顶格承载；懒建单例，防托盘连点叠窗）
// ─────────────────────────────────────────────────────────────────────────────
class AgentShell : public QQuickWidget
{
    Q_OBJECT
public:
    static AgentShell *ensure()
    {
        static AgentShell *s_inst = nullptr;
        if (!s_inst) s_inst = new AgentShell();
        return s_inst;
    }
    /** 托盘入口：先重读快照再显示（连接/升级状态是"点开那一刻"的）。 */
    static void openPanel()
    {
        AgentShell *w = ensure();
        w->m_ui.refresh();          // → refreshRequested → agentUiWire 接收端同步 fill+publish
        w->show();
        w->raise();
        w->activateWindow();
        w->setWindowState((w->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    }

    /** VoiceHub 联动模块（2026-10-17 #329）注入：设置页直接引用 `voicehub`
     *  上下文属性（与 agentUi 同一口径）。AgentShell 是**懒创建**（托盘点开才
     *  ensure），正常路径下本调用一定早于第一个 ensure；竞态兜底见 if 分支
     *  （窗口已存在时直接给它的 engine 补设 context property）。 */
    static void setVoicehub(QObject *m)
    {
        s_voicehub = m;
        if (s_live)
            s_live->engine()->rootContext()->setContextProperty(QStringLiteral("voicehub"), m);
    }

private:
    // s_live：已创建实例（本类全局最多一个；ensure 的局部 s_inst 管创建，这里管补设）。
    inline static QPointer<QObject> s_voicehub;
    inline static AgentShell *s_live = nullptr;

    explicit AgentShell(QWidget *parent = nullptr)
        : QQuickWidget(parent), m_ui(this)
    {
        s_live = this;
        setWindowTitle(QStringLiteral("星集控 · 被控端"));
        // 尺寸/位置**跟着可用屏幕走**——真机实证（2026-10-16）：固定 880×640 在
        // 1244×701 的桌面上、窗口 y 还被放到 130 ⇒ 一打开底部（含底导航）就落到屏幕外，
        // 用户报的就是「控制面板看不见」。取「不超可用区 86%、也不超设计尺寸」并居中。
        QScreen *sc = QGuiApplication::primaryScreen();
        if (sc) {
            const QRect av = sc->availableGeometry();
            const int w = qMin(880, qMax(560, int(av.width()  * 0.86)));
            const int h = qMin(640, qMax(420, int(av.height() * 0.86)));
            resize(w, h);
            move(av.x() + (av.width()  - w) / 2,
                 av.y() + (av.height() - h) / 2);
        } else {
            resize(880, 640);
        }
        // ⚠️ Qt 6.12 的枚举只有 SizeViewToRootObject / SizeRootObjectToView
        //    （没有 SizeRootObjectToQML —— 首编即抓）；与岛同款用 ToView。
        setResizeMode(QQuickWidget::SizeRootObjectToView);
        engine()->rootContext()->setContextProperty(QStringLiteral("agentUi"), &m_ui);
        // VoiceHub 联动模块（#329）：main.cpp 在注册模块后调 setVoicehub；懒创建保证
        // 正常路径下这里一定拿得到（竞态兜底在 setVoicehub 的 if 分支）。
        if (s_voicehub)
            engine()->rootContext()->setContextProperty(QStringLiteral("voicehub"), s_voicehub);
        // 数据接线在 main.cpp（agentUiWire 定义处能访问全部 static 符号）。
        agentUiWire(&m_ui);
        // 与岛同模式：QML 相对 exe 目录（CMake POST_BUILD 拷到 build/qml/）。
        const QString qml = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/qml/shell/AgentShell.qml");
        setSource(QUrl::fromLocalFile(qml));
        // QML 出错必须可见（fail-silent 是红线）：日志 + 控制台。
        connect(this, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status st) {
            if (st != QQuickWidget::Error) return;
            const auto errs = errors();
            for (const QQmlError &e : errs)
                qWarning("[agent-shell] QML ERROR: %s", qPrintable(e.toString()));
        });
        // 常驻托盘程序不能"最后一个窗口关闭时退出"（main 已全局关掉，这里不额外处理）。
    }

    AgentUi m_ui;
};
