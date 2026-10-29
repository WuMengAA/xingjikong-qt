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
#include <QVariantList>
#include <QVariantMap>
#include <QColor>        // 2026-10-28 模块三：设置页结果属性要给 QColor（connStateColor 等）
#include <QFont>         // 同上：分类行的字重（QFont::Medium/Normal）由 C++ 给

class AgentUi : public QObject
{
    Q_OBJECT
    // 信息（只读；每次打开面板 / 写完开关后整体重读）
    Q_PROPERTY(QString appVersion   READ appVersion    NOTIFY infoChanged)
    Q_PROPERTY(QString uid          READ uid           NOTIFY infoChanged)
    Q_PROPERTY(QString cloudUrl     READ cloudUrl      NOTIFY infoChanged)
    Q_PROPERTY(QString connState    READ connState     NOTIFY infoChanged)
    // 2026-10-25：设备用途（classroom / broadcast）。壳按它决定显示哪些页、标题叫什么。
    // 为什么不放 OobeWizard：面板要在**没有向导**的常态下也知道自己是什么机器。
    Q_PROPERTY(QString deviceRole   READ deviceRole    NOTIFY infoChanged)
    // 归属码：教室机=班级码，广播机=站码（同一命名空间）。广播台要显示它。
    Q_PROPERTY(QString classCode    READ classCode     NOTIFY infoChanged)
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
    /** 2026-10-28：被控端换 shadcn 语言（MD3 仅管理端）后，令牌对象要一份「深色」布尔。
     *  给结果值而不是让 QML 写 `!lightMode` —— 配色判断一律留在 C++（QML 零逻辑铁律）。 */
    Q_PROPERTY(bool darkTheme       READ darkTheme                                NOTIFY infoChanged)
    // P0-0（2026-10-17）：允许第三方（插件）向灵动岛推送。默认 true（见 main.cpp AgentSettings 注释）。
    Q_PROPERTY(bool pluginIslandPush READ pluginIslandPush WRITE setPluginIslandPush NOTIFY infoChanged)
    Q_PROPERTY(bool autostart       READ autostart       WRITE setAutostart       NOTIFY infoChanged)
    Q_PROPERTY(bool desktopShortcut READ desktopShortcut WRITE setDesktopShortcut NOTIFY infoChanged)

    // ── 2026-10-28 B4：更新渠道（临时测试开关）────────────────────────────
    // 用户点单原文：「被控端设置界面添加临时的测试更新渠道切换按钮」+「切换逻辑在 C++，
    // QML 只绑定」。为什么需要它：云端灰度目前只有 channel 一个维度（见
    // docs/OTA灰度-按机型-方案-2026-10-28.md），运维要在机器上把某一台切到 rc/gray
    // 才能做"单机自助灰度"。**标注「临时」**：档 A 服务端 targeting 上线后本按钮退役。
    // 生效面：g_channel（register 上报 + otaSelfCheck 查询） + 落盘 channel.txt（重启/升级后仍生效）。
    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel NOTIFY infoChanged)
    /** 可选渠道清单 [{key,label,note,variant}]：选项与"选中态样式"都由 C++ 算好，
     *  QML 只绑 `variant: modelData.variant` —— 否则 QML 里就要写
     *  `current === key ? "filled" : "tonal"`，正是铁律禁止的三元配色判断。 */
    Q_PROPERTY(QVariantList channelOptions READ channelOptions NOTIFY infoChanged)
    /** 渠道说明（生效来源 / 落盘位置 / 切换后行为），由 main.cpp 填。 */
    Q_PROPERTY(QString channelNoteText READ channelNoteText NOTIFY infoChanged)

    // ══ 设置页分类（2026-10-28 模块三：界面按参考重做）════════════════════
    // 参考原型是「左侧分类菜单 + 右侧内容区」。分类切换属于**状态**，
    // 按铁律归 C++：QML 只绑 `settingsSections`（含 active）与 7 个布尔，
    // 里面不出现 `tab === "general"` 这种比较。
    Q_PROPERTY(QString settingsTab READ settingsTab WRITE setSettingsTab NOTIFY settingsTabChanged)
    /** 左侧菜单项 [{key,label,icon,active}]：active = 选中态，样式与判断都在 C++。 */
    Q_PROPERTY(QVariantList settingsSections READ settingsSections NOTIFY settingsTabChanged)
    // 右侧每个区块是否可见（一区块一个布尔，宁可属性多几句，也不让 QML 写比较）
    Q_PROPERTY(bool tabGeneral READ tabGeneral NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabInterface READ tabInterface NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabNotify READ tabNotify NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabAutomation READ tabAutomation NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabUpdate READ tabUpdate NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabPlugin READ tabPlugin NOTIFY settingsTabChanged)
    Q_PROPERTY(bool tabAbout READ tabAbout NOTIFY settingsTabChanged)

    // ══ 设置页用的「结果属性」（2026-10-28 模块三）════════════════════════
    // 这些原先写在 QML 里（`connState.indexOf("已连接") === 0 ? cOk : cWarn` 之类），
    // 按「QML 不写逻辑」铁律全部搬到 C++。深浅色也在 C++ 判 —— agentUi 本来就有
    // lightMode，不缺这个信息（QML 那边若自己判主题就又变成三元了）。
    Q_PROPERTY(QColor connStateColor READ connStateColor NOTIFY infoChanged)
    Q_PROPERTY(QColor latestColor READ latestColor NOTIFY infoChanged)
    Q_PROPERTY(QColor otaModeColor READ otaModeColor NOTIFY infoChanged)
    Q_PROPERTY(QColor otaAutoColor READ otaAutoColor NOTIFY infoChanged)
    /** 「暂停自动升级 / 恢复自动升级」按钮文案（当前状态决定，C++ 给）。 */
    Q_PROPERTY(QString pauseBtnText READ pauseBtnText NOTIFY infoChanged)
    /** 用途的中文名：classroom⇒「教室端」/ broadcast⇒「广播端」（QML 不做映射）。 */
    Q_PROPERTY(QString deviceRoleText READ deviceRoleText NOTIFY infoChanged)
    /** 下面三条同样是"把数据翻成文案"（空值回落 / 加版本前缀），一律 C++ 做。 */
    Q_PROPERTY(QString versionText READ versionText NOTIFY infoChanged)
    Q_PROPERTY(QString uidText READ uidText NOTIFY infoChanged)
    Q_PROPERTY(QString classCodeText READ classCodeText NOTIFY infoChanged)

    // ══ 2026-10-28 模块三 · 通用分类（严格按蓝图原文六项）══════════════════
    // 蓝图原文（docs/产品蓝图-三模块…2026-10-27.md 模块三 §2）：
    //   「通用设置：开机自启、url 配置、安全模式（意外崩溃防护）、时间配置、
    //     存储与备份、高级选项。」
    // 已有：开机自启（autostart）。本批补其余五项 —— 全部真实现，不留占位。
    //
    // ① url 配置：可编辑并即时重连（写 agent.env + 覆盖进程环境 + 断线回调立刻重连）
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY infoChanged)
    /** 输入框的 placeholder：把"当前值"拼进去，免得 QML 里写 `"当前：" + ...`（那是逻辑）。 */
    Q_PROPERTY(QString serverUrlPlaceholder READ serverUrlPlaceholder NOTIFY infoChanged)
    /** 展示用：空值回落「（未配置）」，QML 不写三元。 */
    Q_PROPERTY(QString serverUrlText READ serverUrlText NOTIFY infoChanged)
    // ② 安全模式（意外崩溃防护）
    Q_PROPERTY(bool safeModeAuto READ safeModeAuto WRITE setSafeModeAuto NOTIFY infoChanged)
    /** 本次是否真的运行在安全模式（上次异常退出 + 开关打开，或本次手动进入）。 */
    Q_PROPERTY(bool safeModeNow READ safeModeNow NOTIFY infoChanged)
    Q_PROPERTY(QString lastExitText READ lastExitText NOTIFY infoChanged)
    /** 安全模式的一句话现状说明（给 QML 直接上屏，QML 不拼条件文案）。 */
    Q_PROPERTY(QString safeModeStateText READ safeModeStateText NOTIFY infoChanged)
    // ③ 时间配置（TimesyncDomain 既有能力，这里只翻成文案）
    Q_PROPERTY(QString timeNowText READ timeNowText NOTIFY infoChanged)
    Q_PROPERTY(QString timeStateText READ timeStateText NOTIFY infoChanged)
    Q_PROPERTY(QString timeServerText READ timeServerText NOTIFY infoChanged)
    Q_PROPERTY(QString timeSkewText READ timeSkewText NOTIFY infoChanged)
    Q_PROPERTY(QString timeOffsetText READ timeOffsetText NOTIFY infoChanged)
    Q_PROPERTY(QString timeLastSyncText READ timeLastSyncText NOTIFY infoChanged)
    Q_PROPERTY(bool timeAutoSync READ timeAutoSync WRITE setTimeAutoSync NOTIFY infoChanged)
    // ④ 存储与备份（用量在 refreshStorage() 里现算，不随每次 fill 递归扫盘）
    Q_PROPERTY(QString storageConfigText READ storageConfigText NOTIFY storageChanged)
    Q_PROPERTY(QString storageDataText READ storageDataText NOTIFY storageChanged)
    Q_PROPERTY(QString storageLogText READ storageLogText NOTIFY storageChanged)
    Q_PROPERTY(QString storageTotalText READ storageTotalText NOTIFY storageChanged)
    /** 存储卡的副标题（"还没统计过，点刷新" / "统计于 HH:mm:ss"）。 */
    Q_PROPERTY(QString storageHintText READ storageHintText NOTIFY storageChanged)
    // ⑤ 高级选项：重置的影响范围说明（C++ 写死口径，避免 QML 里另写一段会和实现分叉的话）
    Q_PROPERTY(QString resetScopeText READ resetScopeText CONSTANT)
    /** 通用分类里"动作类"操作的统一结果条（保存地址 / 对时 / 备份 / 清理 / 重置）。
     *  空串 = 还没操作过（QML 据此隐藏结果条，不写 `=== ""` 判断…… 用 visible: 绑布尔）。 */
    Q_PROPERTY(QString actionResultText READ actionResultText NOTIFY infoChanged)
    Q_PROPERTY(bool actionResultVisible READ actionResultVisible NOTIFY infoChanged)

public:
    explicit AgentUi(QObject *parent = nullptr) : QObject(parent) {}

    // ── getter（纯成员；moc 的属性系统会引用它们的地址）──
    QString appVersion() const { return m_appVersion; }
    QString uid() const { return m_uid; }
    QString deviceRole() const { return m_deviceRole; }
    QString classCode() const { return m_classCode; }
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
    bool darkTheme() const { return !m_lightMode; }
    bool pluginIslandPush() const { return m_pluginIslandPush; }
    bool autostart() const { return m_autostart; }
    bool desktopShortcut() const { return m_desktopShortcut; }
    QString updateChannel() const { return m_updateChannel; }
    QString channelNoteText() const { return m_channelNoteText; }
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
    void setPluginIslandPush(bool v) { if (m_pluginIslandPush == v) return; m_pluginIslandPush = v; emit infoChanged(); emit switchChanged(QStringLiteral("pluginIslandPush"), v); }
    void setAutostart(bool v)       { if (m_autostart == v) return;       m_autostart = v;       emit infoChanged(); emit switchChanged(QStringLiteral("autostart"), v); }
    void setDesktopShortcut(bool v) { if (m_desktopShortcut == v) return; m_desktopShortcut = v; emit infoChanged(); emit switchChanged(QStringLiteral("desktopShortcut"), v); }
    // 通用分类的两个开关（2026-10-28 模块三）：走与既有开关同一条 switchChanged 通路，
    // main.cpp 落盘 AgentSettings 之后 fill + publish 回弹（不假勾）。
    void setSafeModeAuto(bool v) { if (m_safeModeAuto == v) return; m_safeModeAuto = v; emit infoChanged(); emit switchChanged(QStringLiteral("safeModeAuto"), v); }
    void setTimeAutoSync(bool v) { if (m_timeAutoSync == v) return; m_timeAutoSync = v; emit infoChanged(); emit switchChanged(QStringLiteral("timeAutoSync"), v); }
    void setPlayScheduleEnabled(bool v) { if (m_playScheduleEnabled == v) return; m_playScheduleEnabled = v; emit infoChanged(); emit switchChanged(QStringLiteral("playScheduleEnabled"), v); }
    void setPlayWindowsText(const QString &v) { if (m_playWindowsText == v) return; m_playWindowsText = v; emit infoChanged(); }
    // 渠道切换：只写成员 + 上抛请求；真实落盘/重查在 main.cpp（那里才有 g_channel 与 channel.txt）
    void setUpdateChannel(const QString &v)
    {
        if (m_updateChannel == v) return;
        m_updateChannel = v;
        emit infoChanged();
        emit channelChangedRequested(v);
    }

    // 渠道选项：常量表，唯一真源在 C++（QML 里不许出现数组字面量）
    /** 生成选项清单；current 命中哪一项，那一项的 variant = "filled"，其余 "tonal"。 */
    static QVariantList channelOptionList(const QString &current);
    QVariantList channelOptions() const { return m_channelOptions; }

    // ── 设置页分类 getter（见上方 Q_PROPERTY 注释）──────────────────────
    QString settingsTab() const { return m_settingsTab; }
    QVariantList settingsSections() const;
    bool tabGeneral() const    { return m_settingsTab == QLatin1String("general"); }
    bool tabInterface() const  { return m_settingsTab == QLatin1String("interface"); }
    bool tabNotify() const     { return m_settingsTab == QLatin1String("notify"); }
    bool tabAutomation() const { return m_settingsTab == QLatin1String("automation"); }
    bool tabUpdate() const     { return m_settingsTab == QLatin1String("update"); }
    bool tabPlugin() const     { return m_settingsTab == QLatin1String("plugin"); }
    bool tabAbout() const      { return m_settingsTab == QLatin1String("about"); }
    /** 切分类（QML 只报 key，合法性由 C++ 判；未知 key 忽略并回原值）。 */
    void setSettingsTab(const QString &key);

    // ── 结果属性 getter（配色与文案见上方 Q_PROPERTY 注释）──────────────
    // 深浅两套色值直接取自 Md3 令牌的同一组十六进制（cOk/cWarn/cError 的明暗值），
    // 这样 C++ 给的色与 QML 令牌在两种主题下完全一致，不会出现"同一状态两种绿"。
    QColor connStateColor() const;
    QColor latestColor() const;
    QColor otaModeColor() const;
    QColor otaAutoColor() const;
    QString pauseBtnText() const
    {
        return m_otaPaused ? QStringLiteral("恢复自动升级")
                           : QStringLiteral("暂停自动升级");
    }
    QString deviceRoleText() const
    {
        return m_deviceRole == QLatin1String("broadcast") ? QStringLiteral("广播端")
                                                          : QStringLiteral("教室端");
    }
    QString versionText() const { return QStringLiteral("v") + m_appVersion; }
    QString uidText() const
    {
        return m_uid.isEmpty() ? QStringLiteral("（未注册）") : m_uid;
    }
    QString classCodeText() const
    {
        return m_classCode.isEmpty() ? QStringLiteral("（未绑定）") : m_classCode;
    }

    // ── 通用分类（2026-10-28 模块三）getter ──
    QString serverUrl() const { return m_serverUrl; }
    QString serverUrlText() const
    {
        return m_serverUrl.isEmpty() ? QStringLiteral("（未配置）") : m_serverUrl;
    }
    QString serverUrlPlaceholder() const
    {
        if (m_serverUrl.isEmpty())
            return QStringLiteral("未配置（例：wss://control.example.com/ws/agent）");
        return QStringLiteral("当前：%1（输入新地址后点保存）").arg(m_serverUrl);
    }
    bool safeModeAuto() const { return m_safeModeAuto; }
    bool safeModeNow() const { return m_safeModeNow; }
    QString lastExitText() const { return m_lastExitText; }
    QString safeModeStateText() const
    {
        if (!m_safeModeAuto && !m_safeModeNow)
            return QStringLiteral("已关闭：异常退出后照常加载全部插件");
        if (m_safeModeNow)
            return QStringLiteral("本次已在安全模式：第三方插件未加载（改完重启即可恢复）");
        return QStringLiteral("已开启：上次异常退出后，下次启动不加载第三方插件");
    }
    QString timeNowText() const { return m_timeNowText; }
    QString timeStateText() const { return m_timeStateText; }
    QString timeServerText() const { return m_timeServerText; }
    QString timeSkewText() const { return m_timeSkewText; }
    QString timeOffsetText() const { return m_timeOffsetText; }
    QString timeLastSyncText() const { return m_timeLastSyncText; }
    bool timeAutoSync() const { return m_timeAutoSync; }
    QString storageConfigText() const { return m_storageConfigText; }
    QString storageDataText() const { return m_storageDataText; }
    QString storageLogText() const { return m_storageLogText; }
    QString storageTotalText() const { return m_storageTotalText; }
    QString storageHintText() const { return m_storageHintText; }
    QString actionResultText() const { return m_actionResultText; }
    bool actionResultVisible() const { return !m_actionResultText.isEmpty(); }

    // ── 给 main.cpp 的接线 lambda 用的写入口 ──────────────────────────────
    // 为什么不用 friend：friend 只对 `agentUiFill` 那**一个函数**有效，
    // 而动作结果是在 agentUiWire 的 lambda 里写的（lambda 的 operator() 不是 friend，
    // 直接摸 m_* 会 C2248）。QML 侧对这俩仍只有只读属性。
    void setActionResultText(const QString &t) { m_actionResultText = t; }
    void setStorageTexts(const QString &cfg, const QString &data, const QString &logs,
                         const QString &total, const QString &hint)
    {
        m_storageConfigText = cfg;
        m_storageDataText   = data;
        m_storageLogText    = logs;
        m_storageTotalText  = total;
        m_storageHintText   = hint;
    }
    QString resetScopeText() const
    {
        return QStringLiteral("会删掉云端地址与令牌、时间/时段/自动化配置（相当于回到「未配置」，"
                              "需要重新填地址）。不会删设备身份（用途/归属码）与日志，"
                              "所以设备不用重新绑定。改完必须重启被控端才生效。");
    }

    // 快照写入专用（main.cpp 的 agentUiFill 经 friend 直接改成员，不触发 switchChanged）
    friend void agentUiFill(AgentUi *ui);

public slots:
    /** QML/打开面板时重读快照：发请求，main.cpp 接住后 fill + publish。 */
    void refresh() { emit refreshRequested(); }
    /** 快照已变 → 让 QML 绑定重算。 */
    void publish() { emit infoChanged(); }
    void openLogDir() { emit openLogDirRequested(); }
    void openDataDir() { emit openDataDirRequested(); }
    /** 打开配置目录（2026-10-28 模块三：从托盘右键菜单移植进设置页）。
     *  托盘的「打开配置目录」与设置页的「通用 › 高级」是同一个动作，都走这里。 */
    void openConfigDir() { emit openConfigDirRequested(); }
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

    // ── 通用分类（2026-10-28 模块三·按蓝图原文）────────────────────────
    /** url 配置：写 agent.env + 覆盖进程环境 + 断线回调立刻重连（不等退避）。 */
    void applyServerUrl(const QString &url) { emit serverUrlApplyRequested(url.trimmed()); }
    /** 时间配置：立即与 NTP 对一次（异步，结果回来由 onChanged 刷界面）。 */
    void syncTimeNow() { emit timeSyncRequested(); }
    /** 人工偏移步进（分钟，正 = 判定时间往前拨）。范围由 TimesyncDomain 校验。 */
    void adjustTimeOffset(int deltaMinutes) { emit timeOffsetAdjustRequested(deltaMinutes); }
    void resetTimeOffset() { emit timeOffsetResetRequested(); }
    /** 存储与备份：现算三个目录的占用（不随 infoChanged 自动跑）。 */
    void refreshStorage() { emit storageRefreshRequested(); }
    void backupConfigNow() { emit backupRequested(); }
    void clearOldLogsNow() { emit clearLogsRequested(); }
    /** 高级选项：重置设置（QML 侧必须二次确认；本函数只管发请求）。 */
    void resetAllSettings() { emit resetAllRequested(); }

signals:
    void infoChanged();
    void refreshRequested();
    void switchChanged(const QString &key, bool value);
    void openLogDirRequested();
    void openDataDirRequested();
    /** 打开配置目录（托盘右键同款动作，2026-10-28 移植进设置页）。 */
    void openConfigDirRequested();
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
    /** 渠道切换请求（main.cpp 接：写 channel.txt → 重查更新 → fill+publish 回弹）。 */
    void channelChangedRequested(const QString &channel);
    /** 设置页分类变了（右侧区块可见性靠它驱动）。 */
    void settingsTabChanged();

    // ── 通用分类（2026-10-28 模块三）──────────────────────────────────
    void serverUrlApplyRequested(const QString &url);
    void timeSyncRequested();
    void timeOffsetAdjustRequested(int deltaMinutes);
    void timeOffsetResetRequested();
    void storageRefreshRequested();
    void backupRequested();
    void clearLogsRequested();
    void resetAllRequested();
    /** 存储用量重算完成（fill 之后发，避免 infoChanged 也被扫盘触发）。 */
    void storageChanged();

private:
    QString m_appVersion, m_uid, m_cloudUrl, m_connState, m_dataDir, m_logPath;
    QString m_deviceRole = QStringLiteral("classroom");   // 快照：用途（默认教室机）
    QString m_classCode;                                   // 快照：归属码（班码/站码）
    QString m_latestText, m_updateNotes, m_otaModeText, m_otaAutoText, m_otaLastText, m_otaNextText;
    bool m_hasNewer = false, m_canUpgrade = false, m_otaPaused = false;
    bool m_notifyEnabled = true, m_voiceEnabled = true, m_noiseEnabled = true;
    bool m_islandIdle = true, m_lightMode = false, m_autostart = false, m_desktopShortcut = false;
    bool m_pluginIslandPush = true;   // P0-0：与 main.cpp AgentSettings::pluginIslandPush() 默认同值
    // ── A-3 第二批 ──
    QVariantList m_selfCheckItems, m_plugins, m_modules;
    QString m_noiseLogText, m_studyLogText, m_playWindowsText, m_playStateText;
    bool m_playScheduleEnabled = false;
    // 2026-10-28 B4：更新渠道快照 + 说明（fill 时从 main.cpp 的 g_channel / channel.txt 重读）
    QString m_updateChannel = QStringLiteral("stable");
    QString m_channelNoteText;
    QVariantList m_channelOptions;
    // 2026-10-28 模块三：设置页当前分类（默认「通用」—— 用户点开设置最先想知道"这台是谁"）
    QString m_settingsTab = QStringLiteral("general");

    // 通用分类（2026-10-28 模块三）：全部是"填好就上屏"的结果值，QML 不做判断。
    QString m_serverUrl;
    bool    m_safeModeAuto = true;
    bool    m_safeModeNow = false;
    QString m_lastExitText;
    QString m_timeNowText, m_timeStateText, m_timeServerText, m_timeSkewText;
    QString m_timeOffsetText, m_timeLastSyncText;
    bool    m_timeAutoSync = true;
    QString m_storageConfigText, m_storageDataText, m_storageLogText;
    QString m_storageTotalText, m_storageHintText;
    /** ⚠️ agentUiFill **不清它**：这是"刚才那个动作的结果"，不是快照。 */
    QString m_actionResultText;
};

// 由 main.cpp 提供（那里能访问匿名域的 static 自由函数与 AgentSettings）：
// 填快照（agentUiFill，friend）+ 接全部请求信号（agentUiWire）。AgentShell 构造时调用。
void agentUiWire(AgentUi *ui);

// 渠道清单（2026-10-28 B4）：云端按 channel 分文件存清单 ——
//   stable → ota.json；rc → ota-rc.json；gray → ota-gray.json（见 cloud/src/ota.js manifestFileFor）。
// 三个渠道名都过云端白名单 /^[a-z0-9_-]{1,16}$/，不会参与文件名拼接（防路径穿越）。
inline QVariantList AgentUi::channelOptionList(const QString &current)
{
    QVariantList out;
    auto add = [&out, &current](const QString &key, const QString &label, const QString &note) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        m.insert(QStringLiteral("label"), label);
        m.insert(QStringLiteral("note"), note);
        // 选中态样式也由 C++ 定：QML 侧不多写一个三元
        m.insert(QStringLiteral("variant"),
                 key == current ? QStringLiteral("filled") : QStringLiteral("tonal"));
        out.append(m);
    };
    add(QStringLiteral("stable"), QStringLiteral("稳定"), QStringLiteral("对外正式版"));
    add(QStringLiteral("rc"),     QStringLiteral("测试"), QStringLiteral("候选测试版"));
    add(QStringLiteral("gray"),   QStringLiteral("灰度"), QStringLiteral("指定机型/班级灰度"));
    return out;
}

// 设置页分类清单（2026-10-28 模块三：界面按参考重做）。
//   · 图标名必须是 resources/icons 里**真实存在**的文件名 —— 写不存在的名字会静默空白
//     （项目踩过一次："grid"/"users" 在 viewer 那边根本不存在）。
//   · active 由 C++ 定：QML 直接绑 `active`，不写 `key === current` 这种比较。
//   · 分类与内容的一一对应关系见各 tabXxx 布尔 —— 七块内容只显示一块。
inline QVariantList AgentUi::settingsSections() const
{
    QVariantList out;
    auto add = [&out, this](const QString &key, const QString &label, const QString &icon) {
        const bool on = (key == m_settingsTab);
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        m.insert(QStringLiteral("label"), label);
        m.insert(QStringLiteral("icon"), icon);
        m.insert(QStringLiteral("active"), on);
        // 选中态样式也由 C++ 定 —— QML 里不能写 `active ? A : B`（那是三元配色判断）。
        // 2026-10-30 用户点单「不要紫色」：原值取自 MD3 的 cSecondaryCont
        // (#E8DEF8/#4A4458 —— 淡紫/紫灰)，现改为 shadcn 锌灰中性色：
        //   rowBg 选中  = #E4E4E7 / #3F3F46（zinc-200 / zinc-700）
        //   fg    选中  = #09090B / #FAFAFA（= Theme.color.textPrimary）
        //   fg    未选  = #71717A / #A1A1AA（= Theme.color.textSecondary）
        // ⚠️ 为什么 rowBg 不用 Theme.color.surfaceVariant(#F4F4F5/#27272A)：
        //    侧栏自身底色就是 ash.muted(#27272A，深色) ⇒ 选中行会与底色**撞成一片**，
        //    选中态肉眼消失（2026-10-30 截图实证）。故往亮处再提一档。
        // ⚠️ 这几组 Hex 是共享库调色板的镜像，改 Theme/ash 色板时要同步（C++ 取不到 QML 单例）。
        // ⚠️ 不要改回紫色：被控端主色一律中性，品牌紫只属于管理端。
        m.insert(QStringLiteral("rowBg"),
                 on ? (m_lightMode ? QStringLiteral("#E4E4E7") : QStringLiteral("#3F3F46"))
                    : QStringLiteral("transparent"));
        m.insert(QStringLiteral("fg"),
                 on ? (m_lightMode ? QStringLiteral("#09090B") : QStringLiteral("#FAFAFA"))
                    : (m_lightMode ? QStringLiteral("#71717A") : QStringLiteral("#A1A1AA")));
        // QFont::Normal=50 / Medium=57，与 QML 的 Font.Normal/Font.Medium 同值
        m.insert(QStringLiteral("weight"), on ? int(QFont::Medium) : int(QFont::Normal));
        out.append(m);
    };
    // 七个分类对齐用户蓝图（通用/主界面/通知与音量/自动化/更新/插件/关于）。
    // 「关于」并进「诊断」：配置自检、噪音记录、自习记录是原托盘右键的能力，
    // 蓝图七类里没有独立位置，挂在「关于」下比新造第八类更贴近参考。
    add(QStringLiteral("general"),    QStringLiteral("通用"),       QStringLiteral("sliders"));
    add(QStringLiteral("interface"),  QStringLiteral("主界面"),     QStringLiteral("layout-grid"));
    add(QStringLiteral("notify"),     QStringLiteral("通知与音量"), QStringLiteral("bell"));
    add(QStringLiteral("automation"), QStringLiteral("自动化"),     QStringLiteral("clock"));
    add(QStringLiteral("update"),     QStringLiteral("更新"),       QStringLiteral("download"));
    add(QStringLiteral("plugin"),     QStringLiteral("插件"),       QStringLiteral("rocket"));
    add(QStringLiteral("about"),      QStringLiteral("关于与诊断"), QStringLiteral("info"));
    return out;
}

inline void AgentUi::setSettingsTab(const QString &key)
{
    // 只认上面那张表里的 key：乱传一律忽略，不留"点空"的假状态。
    static const QStringList kKeys{
        QStringLiteral("general"),    QStringLiteral("interface"),
        QStringLiteral("notify"),     QStringLiteral("automation"),
        QStringLiteral("update"),     QStringLiteral("plugin"),
        QStringLiteral("about"),
    };
    if (!kKeys.contains(key)) return;
    if (m_settingsTab == key) return;
    m_settingsTab = key;
    emit settingsTabChanged();
}

// 结果属性（2026-10-28 模块三）：色值与 Md3 令牌**同一组十六进制**，
// 保证 C++ 给的色和 QML 侧令牌在两种主题下看起来是同一个颜色。
//   cOk        亮 #1B7A43 / 暗 #4CC38A
//   cWarn      亮 #A87C0A / 暗 #D4A017
//   cError     亮 #B3261E / 暗 #F2B8B5
//   cOnSurface 亮 #1C1B1F / 暗 #E6E1E5
inline QColor AgentUi::connStateColor() const
{
    const bool ok = m_connState.startsWith(QStringLiteral("已连接"));
    if (ok) return m_lightMode ? QColor(QStringLiteral("#1B7A43"))
                               : QColor(QStringLiteral("#4CC38A"));
    return m_lightMode ? QColor(QStringLiteral("#A87C0A"))
                       : QColor(QStringLiteral("#D4A017"));
}

inline QColor AgentUi::latestColor() const
{
    if (m_hasNewer) return m_lightMode ? QColor(QStringLiteral("#A87C0A"))
                                       : QColor(QStringLiteral("#D4A017"));
    return m_lightMode ? QColor(QStringLiteral("#1B7A43"))
                       : QColor(QStringLiteral("#4CC38A"));
}

inline QColor AgentUi::otaModeColor() const
{
    // 「强制」档是不可回避的升级，标红让现场一眼看到；其余档用正文色。
    if (!m_otaModeText.startsWith(QStringLiteral("强制")))
        return m_lightMode ? QColor(QStringLiteral("#1C1B1F"))
                           : QColor(QStringLiteral("#E6E1E5"));
    return m_lightMode ? QColor(QStringLiteral("#B3261E"))
                       : QColor(QStringLiteral("#F2B8B5"));
}

inline QColor AgentUi::otaAutoColor() const
{
    if (!m_otaPaused) return m_lightMode ? QColor(QStringLiteral("#1C1B1F"))
                                         : QColor(QStringLiteral("#E6E1E5"));
    return m_lightMode ? QColor(QStringLiteral("#B3261E"))
                       : QColor(QStringLiteral("#F2B8B5"));
}

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
        s_voicehub = m;   // QPointer：模块先于 shell 销毁时，这里的判断自然为假
        if (s_live)
            s_live->engine()->rootContext()->setContextProperty(QStringLiteral("voicehub"), m);
        // ── 2026-10-19 远控崩溃同源修复（见 island.cpp setQmlObjectProperty）──
        // context property 同样是**裸指针**：模块销毁后 QML 侧仍持有野指针，任何绑定重算
        // 都会踩它（真机 dump：Qt6Core+0x38c0 / QQuickRepeater::setModel）。
        // 对象销毁瞬间把 context property 置 null，让重算读到空值（QML 侧有 null 守卫）。
        if (m && s_live) {
            AgentShell *live = s_live;
            QObject::connect(m, &QObject::destroyed, live, [live]() {
                if (live && live->engine())
                    live->engine()->rootContext()->setContextProperty(
                        QStringLiteral("voicehub"), QVariant());
            });
        }
    }

    /** 广播社成员模块（2026-10-23 #广播站移交）注入：与 setVoicehub 同口径，
     *  上下文属性名 `broadcastClub`，页内直接引用。 */
    static void setBroadcastClub(QObject *m)
    {
        s_broadcastClub = m;
        if (s_live)
            s_live->engine()->rootContext()->setContextProperty(QStringLiteral("broadcastClub"), m);
        if (m && s_live) {
            AgentShell *live = s_live;
            QObject::connect(m, &QObject::destroyed, live, [live]() {
                if (live && live->engine())
                    live->engine()->rootContext()->setContextProperty(
                        QStringLiteral("broadcastClub"), QVariant());
            });
        }
    }

private:
    // s_live：已创建实例（本类全局最多一个；ensure 的局部 s_inst 管创建，这里管补设）。
    inline static QPointer<QObject> s_voicehub;
    inline static QPointer<QObject> s_broadcastClub;
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
        // 广播社成员模块（2026-10-23）：main.cpp 注册后调 setBroadcastClub；
        // 懒创建保证正常路径下这里一定拿得到。
        if (s_broadcastClub)
            engine()->rootContext()->setContextProperty(QStringLiteral("broadcastClub"), s_broadcastClub);
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
