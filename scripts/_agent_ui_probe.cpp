// 星集控 · 被控端运行时探针：AgentShell 全实例化（_agent_ui_probe.cpp，随仓库提交）
// ───────────────────────────────────────────────────────────────────────────
// 与 scripts/_agent_shell_probe.qml 的分工：那个用 qml.exe 跑，**注入不了 context
// property**（qml.exe 没有 setContextProperty）⇒ 页内 agentUi.* 只能报 ReferenceError，
// 页的实例化内容验不到。本探针用 QQuickWidget + setContextProperty("agentUi", …)
// 把**照真实签名伪造的 AgentUi** 注进去 ⇒ 5 个页全部真实例化，可以断言：
//   · 内置模块区：每条模块渲成一张卡（名称/版本/运行状态/启停按钮）
//   · 插件市场区：插件卡 + 安装/卸载/启停按钮 + 空态文案的显隐
//   · 自检区：条目卡 + 一键修复/重新检查
//   · 播放时段区：状态文案
//   · 页的 Component.onCompleted 钩子真的跑了（pluginsRefresh / runSelfCheckCheck）
//
// ⚠️ 假上下文的纪律（bulkList 教训）：属性/方法**照 src/agent_ui.h 的真实签名**写，
//    面由 `grep -rho "agentUi\.[A-Za-z_]*" qml/shell/` 全量收集（47 处）。
// ⚠️ 不链 main.cpp：被控端本体不能起生产进程；只链 SvgIconProvider（自绘，Qt only）。
//
// 跑法（scripts/_agent_ui_probe.py 一键）：编译 moc → cl → 跑 → 出报告 + 截图。
// 退出码：10=全过 20=有失败 30=壳没起来/兜底超时。

#include "svgiconprovider.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlComponent>
#include <QQmlContext>
#include <QTextStream>
#include <QTimer>
#include <QVariantList>
#include <QWidget>

namespace {

int  g_fail = 0;
int  g_pass = 0;
QTextStream *g_out = nullptr;
QStringList  g_qmlErrors;

void check(bool ok, const QString &what)
{
    if (ok) { ++g_pass; *g_out << QStringLiteral("PASS  %1\n").arg(what); }
    else    { ++g_fail; *g_out << QStringLiteral("FAIL  %1\n").arg(what); }
    g_out->flush();
}

} // namespace（辅助函数；FakeAgentUi 必须在全局 —— moc 不支持匿名 namespace）

#include "../src/agent_ui.h"


namespace {

// 有效可见性：自身 visible 且整条父链都可见。
// 设置页的七个分类区块用 `visible: agentUi.tabXxx` 挂在**外层面板**上，
// 里层 Text 自己的 visible 仍然是 true —— 只看自身会误判"七个分类同时显示"。
bool effectivelyVisible(QQuickItem *it)
{
    for (; it; it = it->parentItem())
        if (!it->isVisible()) return false;
    return true;
}

// 在可见子树里找 text == s 的项（不含隐藏分支）。
bool hasShownText(QObject *root, const QString &s, QSet<QObject *> &seen, int depth = 0)
{
    if (!root || depth > 24 || seen.contains(root)) return false;
    seen.insert(root);
    auto *it = qobject_cast<QQuickItem *>(root);
    if (it && !it->isVisible()) return false;
    const QVariant t = root->property("text");
    if (t.isValid() && t.typeId() == QMetaType::QString && t.toString() == s)
        return !it || effectivelyVisible(it);
    for (QObject *c : root->children())
        if (hasShownText(c, s, seen, depth + 1)) return true;
    if (it)
        for (QQuickItem *c : it->childItems())
            if (hasShownText(c, s, seen, depth + 1)) return true;
    return false;
}

/**
 * 找到 text == s 的那个 Text item（用来做几何断言）。
 *
 * 为什么除 hasShownText 之外还要它：hasShownText 只回答"这串字在该不该看得见"，
 * **不回答"它和邻居有没有叠在一起"**。2026-10-31 实测吃过这个亏 ——
 * AppErrorState 的 implicitHeight 算矮了，标题/原因/影响三行**文字重叠**、
 * 重试按钮压在标题上，而 4 条 hasShownText 断言全部 PASS（文字确实"存在且在显示"）。
 * 截图一看才发现。故这里补一条几何判据。
 */
QQuickItem *findTextItem(QObject *root, const QString &s, QSet<QObject *> &seen, int depth = 0)
{
    if (!root || depth > 24 || seen.contains(root)) return nullptr;
    seen.insert(root);
    auto *it = qobject_cast<QQuickItem *>(root);
    if (it && !it->isVisible()) return nullptr;
    const QVariant t = root->property("text");
    if (t.isValid() && t.typeId() == QMetaType::QString && t.toString() == s) {
        if (!it || effectivelyVisible(it)) return it;
    }
    for (QObject *c : root->children())
        if (auto *r = findTextItem(c, s, seen, depth + 1)) return r;
    if (it)
        for (QQuickItem *c : it->childItems())
            if (auto *r = findTextItem(c, s, seen, depth + 1)) return r;
    return nullptr;
}

QQuickItem *findTextItem(QObject *root, const QString &s)
{
    QSet<QObject *> seen;
    return findTextItem(root, s, seen);
}

/* @@ANCHOR@@ */bool hasShownText(QObject *root, const QString &s)
{
    QSet<QObject *> seen;    return hasShownText(root, s, seen, 0);
}

void collectTexts(QObject *root, QStringList &texts, QStringList &btnTexts,
                  QSet<QObject *> &seen, int depth = 0)
{
    // 2026-10-30：深度上限 24 → 64。原值太贴边：卡片里的按钮比卡片标题正好深一层
    // （Column → Row → Md3Btn → Text），任何一侧多包一层就把按钮整批"看不见"，
    // 表现是「同页文本全 PASS、只有按钮断言 FAIL」——极易被误判成按钮没渲染。
    // 上限只为防环，`seen` 已去重，放宽没有代价。
    if (!root || depth > 64 || seen.contains(root)) return;
    seen.insert(root);
    const QVariant t = root->property("text");
    if (t.isValid() && t.typeId() == QMetaType::QString) {
        const QString s = t.toString();
        if (!s.isEmpty()) {
            texts << s;
            // 2026-10-30：按钮判定改为**按信号**，不再按类名。
            // 原判据 `className().contains("Btn")` 对 QML 复合类型不可靠 ——
            // Md3Btn/AppButton 都是 .qml 文件定义的合成类型，C++ 侧元对象类名是
            // QQuickItem，`contains("Btn")` 恒 false ⇒ btnTexts 恒为空，
            // 而 texts 里其实有按钮文字（实测 texts 含「刷新」但 btnTexts=0）。
            // 后果是「按钮没渲染」与「判据没命中」分不开，且会随组件改名静默失效。
            // 现在：声明了 `clicked()` 信号的就是按钮（Md3Btn / AppButton / AshButton 都有）；
            // 类名含 Btn 的仍然收，兜住非 QML 定义的按钮。
            const QMetaObject *mo = root->metaObject();
            const bool looksLikeButton =
                mo->indexOfSignal("clicked()") >= 0
                || QString::fromLatin1(mo->className()).contains(QStringLiteral("Btn"));
            if (looksLikeButton) btnTexts << s;
        }
    }
    for (QObject *c : root->children())
        collectTexts(c, texts, btnTexts, seen, depth + 1);
    // Repeater 的 delegate 有时只在视觉树里（objectName 教训）：childItems 也走一遍，
    // 用 seen 去重（QObject 子与视觉子大面积重叠）。
    if (auto *it = qobject_cast<QQuickItem *>(root))
        for (QQuickItem *c : it->childItems())
            collectTexts(c, texts, btnTexts, seen, depth + 1);
}

} // namespace

// ── 真 AgentUi 的数据填充（2026-10-28 改造）──────────────────────────────
// agent_ui.h 对 `void agentUiFill(AgentUi *)` 有 friend 声明，成员全 private。
// 探针不链 main.cpp（被控端本体不能起生产进程），于是**在自己 TU 里定义同签名
// 实现** —— 与产品同一个填充入口，不再维护 FakeAgentUi 那份"两套签名各自漂移"
// 的伪件（fakeui 缺 20+ 个模块三新属性，加载设置页必然满屏 ReferenceError）。
// 2026-10-30：`deviceRole` 是**只读**属性（产品里由 OOBE 落盘、main.cpp 填快照后才定）。
// 探针要验"广播机那套页"（广播台首屏）只能走产品同一条路径：agentUiFill（friend）
// 重填快照 → emit infoChanged() → 壳里 `manifest: AgentPages.manifestFor(...)` 重算。
// 顺手也就把「切用途后页清单会不会跟着变」这件事验了。
QString g_probeRole = QStringLiteral("classroom");

void agentUiFill(AgentUi *ui)
{
    ui->m_appVersion = QStringLiteral("0.6.50-probe");
    ui->m_uid = QStringLiteral("probe-uid");
    ui->m_cloudUrl = QStringLiteral("wss://example.invalid/ws/agent");
    ui->m_connState = QStringLiteral("已连接（云端已确认）");
    ui->m_deviceRole = g_probeRole;
    ui->m_classCode = QStringLiteral("260901");
    ui->m_dataDir = QStringLiteral("C:/tmp/xingjikong");
    ui->m_logPath = QStringLiteral("C:/tmp/xingjikong/agent-qt.log");
    ui->m_latestText = QStringLiteral("已是最新（云端 v0.6.50）");
    ui->m_hasNewer = false;
    ui->m_canUpgrade = false;
    ui->m_updateNotes = QString();
    ui->m_otaModeText = QStringLiteral("可选：只有点「立即升级」才装");
    ui->m_otaAutoText = QStringLiteral("到点自动装");
    ui->m_otaLastText = QStringLiteral("（这台设备还没自动升级过）");
    ui->m_otaNextText = QStringLiteral("立刻（有网就试）");
    ui->m_otaPaused = false;
    ui->m_notifyEnabled = true;
    ui->m_voiceEnabled = true;
    ui->m_noiseEnabled = true;
    ui->m_islandIdle = true;
    ui->m_lightMode = false;
    ui->m_autostart = true;
    ui->m_desktopShortcut = false;
    ui->m_pluginIslandPush = true;
    ui->m_noiseLogText = QStringLiteral("噪音记录目录：C:/tmp/noise");
    ui->m_studyLogText = QStringLiteral("自习记录目录：C:/tmp/study");
    ui->m_playScheduleEnabled = true;
    ui->m_playWindowsText = QStringLiteral("周一至周五 12:30-13:30 / 17:00-18:00");
    ui->m_playStateText = QStringLiteral("播放中：午间校园广播（今天还有 2 个时段）");
    // 更新渠道（2026-10-28 B4）：选中 stable，三项清单一并给出（选中态由 C++ 定）
    ui->m_updateChannel = QStringLiteral("stable");
    ui->m_channelOptions = AgentUi::channelOptionList(ui->m_updateChannel);
    ui->m_channelNoteText = QStringLiteral("生效文件：C:/tmp/xingjikong/channel.txt（临时开关）");

    // ── 通用分类（2026-10-28 模块三·按蓝图原文六项）的假数据 ──────────────
    // 探针不链 main.cpp（那会起整条生产运行时），所以这里按"真实形状"给值：
    // 文案格式必须与 agentUiFill 里的真实现一致，否则断言验的是假形状。
    ui->m_serverUrl = QStringLiteral("wss://control.example.com/ws/agent");
    ui->m_safeModeAuto = true;
    ui->m_safeModeNow = false;
    ui->m_lastExitText = QStringLiteral("上次正常退出");
    ui->m_timeNowText = QStringLiteral("2026-10-28 10:52:00");
    ui->m_timeStateText = QStringLiteral("已与 NTP 对齐");
    ui->m_timeServerText = QStringLiteral("ntp.aliyun.com");
    ui->m_timeSkewText = QStringLiteral("系统钟比标准时间慢 0.4 秒");
    ui->m_timeOffsetText = QStringLiteral("+0 分钟");
    ui->m_timeLastSyncText = QStringLiteral("10-28 08:30:00");
    ui->m_timeAutoSync = true;
    ui->m_storageConfigText = QStringLiteral("12.4 KB  ·  C:/tmp/xingjikong");
    ui->m_storageDataText   = QStringLiteral("186.2 MB  ·  C:/tmp/xingjikong");
    ui->m_storageLogText    = QStringLiteral("3.1 MB  ·  C:/tmp/xingjikong");
    ui->m_storageTotalText  = QStringLiteral("186.3 MB");
    ui->m_storageHintText   = QStringLiteral("统计于 10:52:00");
    // 动作结果条默认空 = 不显示（真实使用中要点过按钮才出现）
    ui->m_actionResultText = QString();

    // 自检条目（照 main.cpp buildSelfCheckItems 的形状）
    {
        QVariantList checks;
        for (int i = 0; i < 3; ++i) {
            QVariantMap m;
            m.insert(QStringLiteral("name"), QStringLiteral("自检项%1").arg(i + 1));
            m.insert(QStringLiteral("ok"), i != 1);
            m.insert(QStringLiteral("status"), i == 1 ? QStringLiteral("未配置") : QStringLiteral("正常"));
            m.insert(QStringLiteral("fix"), i == 1 ? QStringLiteral("去修复") : QString());
            checks << m;
        }
        ui->applySelfCheckItems(checks);   // 与产品同一入口（含汇总口径）
    }
    // 记录页结构化行：走产品同一入口 applyNoiseLogView / applyStudyLogView
    {
        auto row = [](const QString &l, const QString &v) {
            QVariantMap m;
            m.insert(QStringLiteral("label"), l);
            m.insert(QStringLiteral("value"), v);
            return m;
        };
        QVariantList noise;
        noise << row(QStringLiteral("2026-10-31"),
                     QStringLiteral("1240 次 · 均值 46.2 dB · 峰值 88.4 dB"))
              << row(QStringLiteral("2026-10-30"),
                     QStringLiteral("2610 次 · 均值 44.8 dB · 峰值 91.2 dB"))
              << row(QStringLiteral("2026-10-29"),
                     QStringLiteral("1980 次 · 均值 43.1 dB · 峰值 84.6 dB"));
        QVariantMap nv;
        nv.insert(QStringLiteral("summary"), QStringLiteral("共 3 天 · 5830 条采样"));
        nv.insert(QStringLiteral("rows"), noise);
        ui->applyNoiseLogView(QString(), nv);

        QVariantList study;
        study << row(QStringLiteral("10-31 14:05"), QStringLiteral("时长 42 分 10 秒 · 异常录制 3 次"))
              << row(QStringLiteral("10-30 08:50"), QStringLiteral("时长 25 分 0 秒 · 异常录制 0 次"));
        QVariantMap sv;
        sv.insert(QStringLiteral("summary"), QStringLiteral("共 2 次自习会话"));
        sv.insert(QStringLiteral("rows"), study);
        ui->applyStudyLogView(QString(), sv);
    }
    // 插件市场（照 buildPluginList 的形状；status 是 C++ 生成的展示串）
    {
        QVariantList plugins;
        QVariantMap a;
        a.insert(QStringLiteral("id"), QStringLiteral("echo-sample"));
        a.insert(QStringLiteral("name"), QStringLiteral("示例回显插件"));
        a.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        a.insert(QStringLiteral("author"), QStringLiteral("官方"));
        a.insert(QStringLiteral("description"), QStringLiteral("把收到的指令原样回显，用于验证插件链路。"));
        a.insert(QStringLiteral("installed"), true);
        a.insert(QStringLiteral("enabled"), true);
        a.insert(QStringLiteral("status"), QStringLiteral("● 运行中"));
        plugins << a;
        QVariantMap b;
        b.insert(QStringLiteral("id"), QStringLiteral("voicehub-broadcast"));
        b.insert(QStringLiteral("name"), QStringLiteral("VoiceHub 联动"));
        b.insert(QStringLiteral("version"), QStringLiteral("1.2.0"));
        b.insert(QStringLiteral("author"), QStringLiteral("VoiceHub"));
        b.insert(QStringLiteral("description"), QStringLiteral("广播站点歌/歌词联动。"));
        b.insert(QStringLiteral("installed"), false);
        b.insert(QStringLiteral("enabled"), false);
        b.insert(QStringLiteral("status"), QStringLiteral("— 未安装"));
        plugins << b;
        ui->m_plugins = plugins;
    }
    // 内置模块（照 ModuleHost::snapshot() 的键）
    {
        QVariantList modules;
        QVariantMap a;
        a.insert(QStringLiteral("id"), QStringLiteral("usb"));
        a.insert(QStringLiteral("name"), QStringLiteral("U盘助手"));
        a.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        a.insert(QStringLiteral("description"),
                 QStringLiteral("U 盘插入时在灵动岛弹窗询问是否打开（只开文件夹、绝不自动运行）；考试模式可自动静默。"));
        a.insert(QStringLiteral("builtin"), true);
        a.insert(QStringLiteral("enabled"), true);
        a.insert(QStringLiteral("running"), true);
        QVariantList perms;
        perms << QStringLiteral("island") << QStringLiteral("notify") << QStringLiteral("settings");
        a.insert(QStringLiteral("permissions"), perms);
        QVariantList acts;
        acts << QStringLiteral("usb_drives");
        a.insert(QStringLiteral("actions"), acts);
        modules << a;
        QVariantMap b;
        b.insert(QStringLiteral("id"), QStringLiteral("voicehub"));
        b.insert(QStringLiteral("name"), QStringLiteral("VoiceHub 联动"));
        b.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        b.insert(QStringLiteral("description"), QStringLiteral("对接 VoiceHub：正在播放/歌词/点歌。"));
        b.insert(QStringLiteral("builtin"), true);
        b.insert(QStringLiteral("enabled"), false);
        b.insert(QStringLiteral("running"), false);
        modules << b;
        ui->m_modules = modules;
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("stelarith-probe"));
    QCoreApplication::setApplicationName(QStringLiteral("agent-ui-probe"));

    QFile report(QStringLiteral("_agent_ui_probe_report.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "[agent-ui-probe] 报告打不开\n");
        return 30;
    }
    QTextStream out(&report);
    g_out = &out;
    out << QStringLiteral("== AgentShell 全实例化探针（pid=%1）==\n").arg(qApp->applicationPid());
    out.flush();

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        if (type == QtInfoMsg || type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            g_qmlErrors << msg;   // 2026-10-28：收 Info —— UiPageHost 加载失败走 console.log（Info），此前完全静默
    });

    // 真 AgentUi（header-only、零依赖可独立构造）：数据由上面的 agentUiFill() 填，
    // 与产品同一 friend 入口；只读属性的展示串（connStateColor/versionText 等）
    // 也走 agent_ui.h 里的真实现，探针不再复刻一遍。
    AgentUi ui;
    agentUiFill(&ui);

    // 伪件时代用 `calls` 记录页调过哪些 invokable；真件没有这个字段，
    // 改成**收原始信号**（真件每个 slot 都是 emit 一个 xxxRequested）——
    // 记录口径不变，而且验的是产品真实信号名。
    QStringList calls;
    {
        const auto rec = [&calls](const QString &name) { calls << name; };
        QObject::connect(&ui, &AgentUi::refreshRequested, &app, [rec] { rec(QStringLiteral("refresh")); });
        QObject::connect(&ui, &AgentUi::openLogDirRequested, &app, [rec] { rec(QStringLiteral("openLogDir")); });
        QObject::connect(&ui, &AgentUi::openDataDirRequested, &app, [rec] { rec(QStringLiteral("openDataDir")); });
        QObject::connect(&ui, &AgentUi::openConfigDirRequested, &app, [rec] { rec(QStringLiteral("openConfigDir")); });
        QObject::connect(&ui, &AgentUi::upgradeNowRequested, &app, [rec] { rec(QStringLiteral("upgradeNow")); });
        QObject::connect(&ui, &AgentUi::toggleOtaPauseRequested, &app, [rec] { rec(QStringLiteral("toggleOtaPause")); });
        QObject::connect(&ui, &AgentUi::selfCheckRequested, &app, [rec] { rec(QStringLiteral("runSelfCheckCheck")); });
        QObject::connect(&ui, &AgentUi::selfCheckFixRequested, &app, [rec] { rec(QStringLiteral("selfCheckFix")); });
        QObject::connect(&ui, &AgentUi::pluginsRefreshRequested, &app,
                         [rec] { rec(QStringLiteral("pluginsRefresh")); });
        QObject::connect(&ui, &AgentUi::modulesRefreshRequested, &app, [rec] { rec(QStringLiteral("modulesRefresh")); });
        QObject::connect(&ui, &AgentUi::playScheduleSaveRequested, &app, [rec] { rec(QStringLiteral("savePlaySchedule")); });
        QObject::connect(&ui, &AgentUi::settingsTabChanged, &app,
                         [&calls, &ui] { calls << QStringLiteral("settingsTab:") + ui.settingsTab(); });
        QObject::connect(&ui, &AgentUi::channelChangedRequested, &app,
                         [rec](const QString &ch) { rec(QStringLiteral("channel:") + ch); });
    }

    // 自检条目 / 插件市场 / 内置模块的数据也都在 agentUiFill() 里（与产品同一个形状）。

    QQuickWidget view;
    view.engine()->addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider());
    view.engine()->rootContext()->setContextProperty(QStringLiteral("agentUi"), &ui);

    // 假 voicehub（2026-10-28）：广播台页内联的排期中心面板（BroadcastScheduleSheet）
    // 引用 voicehub.xxx —— 产品由 main.cpp 注入真实桥接对象，探针不链 main.cpp，
    // 不注入就会刷 15 条 ReferenceError 把「无未知报错」这一项打红。
    // 成员面按 `grep -o "voicehub\.[A-Za-z_]*" BroadcastScheduleSheet.qml` 全量收集，
    // 含两个信号（Connections 里 onControlFinished / onReorderFinished）。
    {
        QQmlComponent vhComp(view.engine());
        vhComp.setData(R"QML(
import QtQuick
QtObject {
    property bool canRequest: false
    property var schedule: []
    property string baseUrl: ""
    property string title: ""
    property int scheduleCount: 0
    property bool playing: false
    property bool controlEnabled: false
    property string artist: ""
    // 设置页「VoiceHub 联动」卡读的是 page.vh（= voicehub）这几个成员：
    // active / statusText / nextTitle / autoNotify —— 缺一个就会在赋值处报
    // "Unable to assign [undefined] to QString/bool"（2026-10-28 探针实证）。
    property bool active: false
    property string statusText: "未在播放"
    property string nextTitle: ""
    property bool autoNotify: false
    signal controlFinished(bool ok, string message)
    signal reorderFinished(bool ok, string message)
    function requestSong() {}
    function refreshSchedule() {}
    function reorderSchedule() {}
}
)QML", QUrl(QStringLiteral("qrc:/probe_voicehub.qml")));
        if (QObject *vh = vhComp.create())
            view.engine()->rootContext()->setContextProperty(QStringLiteral("voicehub"), vh);
    }
    view.setResizeMode(QQuickWidget::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(
        QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/build/qml/shell/AgentShell.qml")));
    view.resize(980, 680);
    view.show();

    QTimer::singleShot(1200, &app, [&] {
        QObject *root = view.rootObject();
        check(root != nullptr, QStringLiteral("AgentShell.qml 加载出根对象"));
        if (!root) {
            // 2026-10-30：壳起不来必须**带上 QML 错误原文**。原来只报一行 FAIL，
            // 于是「探针失败」和「探针自己坏了」分不出来（本轮就是这么被卡住的）。
            out << QStringLiteral("-- view.status = %1\n").arg(int(view.status()));
            for (const QQmlError &e : view.errors())
                out << QStringLiteral("   [QML] %1\n").arg(e.toString());
            out << QStringLiteral("== 汇总：PASS %1 / FAIL %2 ==\n").arg(g_pass).arg(g_fail + 1);
            g_fail++;
            out.flush();
            app.exit(20);
            return;
        }


        QStringList texts, btnTexts;
        QSet<QObject *> seen;
        collectTexts(root, texts, btnTexts, seen);
        const auto hasText = [&](const QString &s) { return texts.contains(s); };
        const auto hasBtn  = [&](const QString &s) { return btnTexts.contains(s); };

        // 计数留档：btnTexts 归零是判据失效的早期信号（见 collectTexts 里 2026-10-30 注释）
        out << QStringLiteral("      [debug] 可点文本 %1 条 / 文本 %2 条\n")
                   .arg(btnTexts.size()).arg(texts.size());
        out.flush();

        // ── 一、导航（5 页）──
        for (const QString &nav : { QStringLiteral("设置"), QStringLiteral("自检"),
                                    QStringLiteral("插件"), QStringLiteral("记录"),
                                    QStringLiteral("时段") })
            check(hasText(nav), QStringLiteral("底导航有「%1」").arg(nav));

        // ── 二、内置模块区（本轮新增）──
        check(hasText(QStringLiteral("内置模块")), QStringLiteral("有「内置模块」分区标题"));
        check(hasText(QStringLiteral("U盘助手  v1.0.0")), QStringLiteral("模块卡标题=名称+版本"));
        check(hasText(QStringLiteral("运行中")), QStringLiteral("模块状态=运行中"));
        check(hasText(QStringLiteral("已停用")), QStringLiteral("第二个模块状态=已停用"));
        check(hasBtn(QStringLiteral("停用")), QStringLiteral("已启用模块的按钮=停用"));
        check(hasBtn(QStringLiteral("启用")), QStringLiteral("未启用模块的按钮=启用"));
        check(hasText(QStringLiteral("U 盘插入时在灵动岛弹窗询问是否打开（只开文件夹、绝不自动运行）；考试模式可自动静默。")),
              QStringLiteral("模块描述文案上屏"));

        // ── 二·补：设置页的「VoiceHub 联动」卡（#329）──
        // 2026-10-28：探针改为**注入假 voicehub**（同 main.cpp 的上下文属性名），
        // 因为壳里的广播站排期浮层引用它，不注入会刷 15 条 ReferenceError。
        // ⇒ 本卡走的是"模块已加载"分支，显示假件的 statusText（未在播放）。
        // QML 里 typeof + null 的双重守卫仍在产品代码中，此处只验有值时的渲染。
        check(hasText(QStringLiteral("VoiceHub 联动")), QStringLiteral("设置页有「VoiceHub 联动」卡"));
        check(hasText(QStringLiteral("连接状态")), QStringLiteral("卡里有「连接状态」行"));
        check(hasText(QStringLiteral("未在播放")),
              QStringLiteral("voicehub 注入后卡片显示模块状态（假件 statusText=未在播放）"));
        check(hasText(QStringLiteral("开播提醒")), QStringLiteral("有「开播提醒」开关"));
        check(hasText(QStringLiteral("控制接口（点歌 / 歌词 / 广播）")),
              QStringLiteral("有「控制接口」开关（留桩）"));
        check(hasText(QStringLiteral("服务地址（默认官方站）")), QStringLiteral("有「服务地址」输入框"));

        // ── 三、插件市场区 ──
        check(hasText(QStringLiteral("示例回显插件  v1.0.0")), QStringLiteral("插件卡标题=名称+版本"));
        check(hasText(QStringLiteral("● 运行中")), QStringLiteral("插件状态串=● 运行中"));
        check(hasText(QStringLiteral("— 未安装")), QStringLiteral("插件状态串=— 未安装"));
        check(hasText(QStringLiteral("作者：官方")), QStringLiteral("插件作者行上屏"));
        check(hasBtn(QStringLiteral("安装")), QStringLiteral("未安装插件有「安装」按钮"));
        check(hasBtn(QStringLiteral("卸载")), QStringLiteral("已安装插件有「卸载」按钮"));
        check(hasBtn(QStringLiteral("刷新")), QStringLiteral("有「刷新」按钮"));
        check(hasBtn(QStringLiteral("安装本地示例(echo)")), QStringLiteral("有「安装本地示例」按钮"));
        check(!hasText(QStringLiteral("商店拉取中，或还没有任何插件。")),
              QStringLiteral("有插件时空态文案不出现"));

        // ── 四、自检区 / 时段区 ──
        check(hasText(QStringLiteral("配置自检")), QStringLiteral("自检页渲染（卡片标题在）"));
        check(hasText(QStringLiteral("自检项2")), QStringLiteral("自检条目按模型渲染"));
        check(hasText(QStringLiteral("去修复")), QStringLiteral("失败项有「去修复」按钮"));
        // 2026-10-31 新增：自检汇总行 + 记录页的汇总与行（组件显示雏形批次）
        check(hasText(QStringLiteral("共 3 项 · 通过 2 · 需处理 1")),
              QStringLiteral("自检页有汇总行（C++ 口径）"));
        check(hasText(QStringLiteral("共 3 天 · 5830 条采样")),
              QStringLiteral("记录页噪音汇总上屏"));
        check(hasText(QStringLiteral("1240 次 · 均值 46.2 dB · 峰值 88.4 dB")),
              QStringLiteral("记录页按天渲染行"));
        check(hasText(QStringLiteral("时长 42 分 10 秒 · 异常录制 3 次")),
              QStringLiteral("记录页按次渲染行"));
        check(hasText(QStringLiteral("播放中：午间校园广播（今天还有 2 个时段）")),
              QStringLiteral("时段页状态文案上屏"));

        // ── 五、页的加载钩子真的跑了（证页实例化，不是只编译）──
        check(calls.contains(QStringLiteral("pluginsRefresh")),
              QStringLiteral("插件页 Component.onCompleted 调了 pluginsRefresh"));
        check(calls.contains(QStringLiteral("runSelfCheckCheck")),
              QStringLiteral("自检页 Component.onCompleted 调了 runSelfCheckCheck"));

        // ── 五·补2、通用分类六项（2026-10-28 模块三·严格按蓝图原文）──
        // 蓝图原文：「通用设置：开机自启、url 配置、安全模式（意外崩溃防护）、
        //            时间配置、存储与备份、高级选项。」
        // 开机自启上一批已验（启动卡）；这里验新增的五项，全部按**有效可见**判定。
        const auto shown = [&](const QString &s) { return hasShownText(root, s); };
        check(shown(QStringLiteral("云端与服务")), QStringLiteral("通用 › url 配置卡在（云端与服务）"));
        check(shown(QStringLiteral("wss://control.example.com/ws/agent")),
              QStringLiteral("url 配置显示当前地址"));
        check(shown(QStringLiteral("保存并重连")), QStringLiteral("url 配置有「保存并重连」"));
        check(shown(QStringLiteral("安全模式（意外崩溃防护）")),
              QStringLiteral("通用 › 安全模式卡在"));
        check(shown(QStringLiteral("上次退出")), QStringLiteral("安全模式显示上次退出状态"));
        check(shown(QStringLiteral("异常退出后自动进入安全模式")),
              QStringLiteral("安全模式开关在"));
        check(shown(QStringLiteral("时间")), QStringLiteral("通用 › 时间卡在"));
        check(shown(QStringLiteral("当前判定时间")), QStringLiteral("时间显示判定时间"));
        check(shown(QStringLiteral("NTP 服务器")), QStringLiteral("时间显示 NTP 服务器"));
        check(shown(QStringLiteral("已与 NTP 对齐")), QStringLiteral("时间状态文案上屏"));
        check(shown(QStringLiteral("立即对时")), QStringLiteral("时间有「立即对时」"));
        check(shown(QStringLiteral("+15 分")), QStringLiteral("时间有人工偏移步进"));
        check(shown(QStringLiteral("存储与备份")), QStringLiteral("通用 › 存储与备份卡在"));
        check(shown(QStringLiteral("3.1 MB  ·  C:/tmp/xingjikong")), QStringLiteral("日志用量行上屏"));
        check(shown(QStringLiteral("统计用量")), QStringLiteral("存储有「统计用量」"));
        check(shown(QStringLiteral("备份配置")), QStringLiteral("存储有「备份配置」"));
        check(shown(QStringLiteral("清理旧日志")), QStringLiteral("存储有「清理旧日志」"));
        check(shown(QStringLiteral("高级选项")), QStringLiteral("通用 › 高级选项卡在"));
        check(shown(QStringLiteral("重置全部设置…")), QStringLiteral("高级选项有重置入口"));

        // ── 五·补、设置页七分类与切换（2026-10-28 模块三）──
        // 分类清单来自 C++ settingsSections（icon/label/active/rowBg/fg/weight 全由 C++ 给），
        // 切换由可写属性 agentUi.settingsTab 驱动，右侧区块可见性绑 tabXxx。
        // ⚠️ 只认「有效可见」文本：七个区块都在对象树里，只看树会把全部当成已显示。
        for (const QString &label : { QStringLiteral("通用"), QStringLiteral("主界面"),
                                      QStringLiteral("通知与音量"), QStringLiteral("自动化"),
                                      QStringLiteral("更新"), QStringLiteral("插件"),
                                      QStringLiteral("关于与诊断") }) {
            check(hasText(label), QStringLiteral("分类菜单含「%1」").arg(label));
        }
        check(hasShownText(root, QStringLiteral("这台设备")),
              QStringLiteral("默认分类=通用（「这台设备」可见）"));
        check(!hasShownText(root, QStringLiteral("自动升级")),
              QStringLiteral("通用分类下「自动升级」不可见"));

        ui.setSettingsTab(QStringLiteral("interface"));
        QCoreApplication::processEvents();
        check(ui.tabInterface(), QStringLiteral("切「主界面」→ tabInterface=true"));
        check(hasShownText(root, QStringLiteral("外观")), QStringLiteral("主界面内容已显示（外观）"));
        check(!hasShownText(root, QStringLiteral("这台设备")), QStringLiteral("切走后「这台设备」隐藏"));

        ui.setSettingsTab(QStringLiteral("update"));
        QCoreApplication::processEvents();
        check(ui.tabUpdate(), QStringLiteral("切「更新」→ tabUpdate=true"));
        check(hasShownText(root, QStringLiteral("自动升级")), QStringLiteral("更新内容已显示（自动升级）"));

        ui.setSettingsTab(QStringLiteral("about"));
        QCoreApplication::processEvents();
        check(ui.tabAbout(), QStringLiteral("切「关于与诊断」→ tabAbout=true"));
        check(hasShownText(root, QStringLiteral("诊断与记录")),
              QStringLiteral("关于内容已显示（诊断与记录）"));

        // 未知 key 不许生效：C++ setSettingsTab 有白名单，乱传应保持原分类
        ui.setSettingsTab(QStringLiteral("general"));
        QCoreApplication::processEvents();
        check(ui.tabGeneral(), QStringLiteral("切回「通用」→ tabGeneral=true"));
        ui.setSettingsTab(QStringLiteral("__not_a_tab__"));
        QCoreApplication::processEvents();
        check(ui.tabGeneral(), QStringLiteral("未知分类 key 被忽略（仍是通用）"));

        // ── 六、QML 运行期报错 ──
        // 已知项（**不许静默吞**，逐条留痕）：
        //   · Md3Btn「enabled 覆盖基类成员」：md3/Md3Btn.qml:11 自定义 property enabled
        //     盖掉 Item.enabled —— 存量设计味道（后果：祖先 enabled 传导到按钮会失效），
        //     动共享组件要两端一起看，**待点单**，不在这里擅自改。
        QStringList realErrors;
        QStringList notes;
        for (const QString &e : g_qmlErrors) {
            if (e.contains(QStringLiteral("svgicon")) || e.contains(QStringLiteral("image provider")))
                continue;
            if (e.contains(QStringLiteral("overrides a member of the base object"))) {
                notes << e.left(120);
                continue;
            }
            realErrors << e.left(160);
        }
        for (const QString &n : notes) out << QStringLiteral("      note %1\n").arg(n);
        out << QStringLiteral("      [debug] g_qmlErrors raw=%1\n").arg(g_qmlErrors.size());
        for (const QString &e : g_qmlErrors) out << QStringLiteral("      [raw] %1\n").arg(e.left(200));
        check(realErrors.isEmpty(),
              QStringLiteral("无未知 QML 运行期报错（%1 条）").arg(realErrors.size()));
        for (const QString &e : realErrors) out << QStringLiteral("      · %1\n").arg(e);

        // ── 七、逐屏实物截图（2026-10-30「全部换新」视觉核验）────────────────
        // 为什么必须单列一段：本轮把 ash/ 私有组件（9 件 699 行）、md3 组件库、
        // 四套井存令牌（tok / tk / st / Md3）、硬编码 Hex、border.* 全部清零，
        // 面上覆盖 6 页 + 设置页 7 分类 + 广播站浮层 + 广播机整套 + OOBE 12 件。
        // 72 条文本断言只证明"字还在屏幕上"，证明不了"色/圆角/间距/去边框没坏" ——
        // 这种面级改动不出实物图，'全过' 就是自欺。
        // 落盘位置：cwd 由 _agent_ui_probe.py 设为 build/_objs
        //           ⇒ build/_objs/_shots/NN_<页id>.png
        const QString shotsDir = QStringLiteral("_shots");
        QDir().mkpath(shotsDir);
        int shotN = 0;
        // 统一等待：截图与"等动画收尾再继续"共用一份，避免两处各写一个 QEventLoop
        const auto waitMs = [](int ms) {
            QEventLoop l;
            QTimer::singleShot(ms, &l, &QEventLoop::quit);
            l.exec();
        };
        const auto snap = [&](const QString &name, int settle = 260) {
            // settle 收尾：Behavior / NumberAnimation（果冻动效 300ms）走完再抓，
            // 抓的是稳态而不是动画中间帧 —— 否则每张图都在"半透明/hover 中途"。
            // ⚠️ 抽展类（BroadcastScheduleSheet 的抽屉滑入 360ms）要传更长值：
            //    2026-10-30 首轮只等 260ms，截出来的抽屉还差 88px 没滑到位，
            //    内容被窗口右缘切掉 —— 差点被误判成"浮层溢出"的布局 bug。
            waitMs(settle);
            ++shotN;
            const QString fn = QStringLiteral("%1/%2_%3.png")
                                   .arg(shotsDir)
                                   .arg(shotN, 2, 10, QChar('0'))
                                   .arg(name);
            if (view.grab().save(fn)) out << QStringLiteral("SHOT  %1\n").arg(fn);
            else                      out << QStringLiteral("SHOT  失败 %1\n").arg(fn);
            out.flush();
        };

        // 页宿主 = 唯一同时有 `count` 属性与 `goTo()` 方法的对象。
        // 刻意不用 objectName：那要么改产品代码加探针专用字段，要么依赖 id ——
        // QML 的 id 不进元对象，抓不到。按"能力"找才不绑死实现细节。
        QObject *pageHost = nullptr;
        {
            QList<QObject *> stack;
            stack << root;
            while (!stack.isEmpty()) {
                QObject *o = stack.takeFirst();
                if (o->metaObject()->indexOfMethod("goTo(QVariant)") >= 0
                    && o->property("count").isValid()) {
                    pageHost = o;
                    break;
                }
                for (QObject *c : o->children()) stack << c;
                if (auto *it = qobject_cast<QQuickItem *>(o))
                    for (QQuickItem *c : it->childItems()) stack << c;
            }
        }
        check(pageHost != nullptr, QStringLiteral("找到页宿主（goTo + count 齐备）"));

        int settingsIdx = -1;
        if (pageHost) {
            const int pageCount = pageHost->property("count").toInt();
            out << QStringLiteral("      [debug] 页宿主 count=%1\n").arg(pageCount);
            check(pageCount == 6,
                  QStringLiteral("教室机页清单 = 6 页（实得 %1）").arg(pageCount));

            QStringList ids;
            for (int i = 0; i < pageCount; ++i) {
                QMetaObject::invokeMethod(pageHost, "goTo", Q_ARG(QVariant, i));
                QCoreApplication::processEvents();
                const QString id = pageHost->property("currentId").toString();
                ids << id;
                if (id == QStringLiteral("settings")) settingsIdx = i;
                snap(QStringLiteral("page_%1_%2").arg(i, 2, 10, QChar('0')).arg(id));
                out << QStringLiteral("      · 第 %1 页 = %2\n").arg(i).arg(id);
            }
            check(!ids.contains(QString()), QStringLiteral("逐页 goTo 全部落到实页（无空 id）"));
            check(settingsIdx >= 0, QStringLiteral("页清单含 settings（设置页截图入口）"));

            // 设置页七分类：分类切换决定右侧区块，是本轮改动最密集的一块
            // （11 件 Sec* + 侧栏选中态去紫 + AppDialog 替换）。
            if (settingsIdx >= 0) {
                QMetaObject::invokeMethod(pageHost, "goTo", Q_ARG(QVariant, settingsIdx));
                QCoreApplication::processEvents();
                for (const QString &key : { QStringLiteral("general"), QStringLiteral("interface"),
                                            QStringLiteral("notify"), QStringLiteral("automation"),
                                            QStringLiteral("update"), QStringLiteral("plugin"),
                                            QStringLiteral("about") }) {
                    ui.setSettingsTab(key);
                    QCoreApplication::processEvents();
                    snap(QStringLiteral("settings_%1").arg(key));
                }
                ui.setSettingsTab(QStringLiteral("general"));
                QCoreApplication::processEvents();
            }

            // 排期中心（2026-10-30 改为嵌入广播台页内联面板，不再是独立浮层抽屉）：
            // 它在 broadcastconsole 页里，已被下面的广播机逐页截图（bc_00_*）覆盖，无需再单独开抽屉。

            // ── 空态实物核验（2026-10-31 组件显示雏形批次）─────────────────
            // 记录页的"没数据"分支平时拍不到（上面给的是有数据形状）。
            // 这里把两条记录视图清空、回到记录页拍一张：证明空态件真的占位、文案在、
            // 而不是"隐藏了整块导致卡片塌成一条线"。
            {
                const int ri = ids.indexOf(QStringLiteral("records"));
                if (ri >= 0) {
                    QVariantMap ev;
                    ev.insert(QStringLiteral("summary"), QStringLiteral("还没有噪音记录"));
                    ev.insert(QStringLiteral("rows"), QVariantList());
                    ui.applyNoiseLogView(QString(), ev);
                    QVariantMap sv2;
                    sv2.insert(QStringLiteral("summary"), QStringLiteral("还没有自习记录"));
                    sv2.insert(QStringLiteral("rows"), QVariantList());
                    ui.applyStudyLogView(QString(), sv2);
                    // ⚠️ 必须走 publish()（双发），不能只 `emit ui.infoChanged()`：
                    // 2026-10-31 拆分第一刀后，noise/study 记录属性已搬到基类 AgentRecordUi，
                    // 只发派生类信号时基类属性**收不到通知** ⇒ 空态永远不上屏。
                    // 本探针实测正是这样挂掉 2 条断言（记录页空态不可见）——留此注释防回归。
                    ui.publish();
                    QMetaObject::invokeMethod(pageHost, "goTo", Q_ARG(QVariant, ri));
                    QCoreApplication::processEvents();
                    snap(QStringLiteral("records_empty"));
                    check(hasShownText(root, QStringLiteral("还没有噪音记录")),
                          QStringLiteral("记录页空态上屏（无数据时可见）"));
                    check(hasShownText(root, QStringLiteral("打开记录文件夹")),
                          QStringLiteral("记录页空态带引导按钮"));

                    // ── 错误态核验（2026-10-31 二改：修「错误被折叠成空」的真 bug）──
                    // 造一次"文件打不开"的形状：failReason 非空 ⇒ 必须显示错误态，
                    // 且**不能**同时显示「还没有噪音记录」（两者互斥）。
                    QVariantMap evf;
                    evf.insert(QStringLiteral("summary"), QString());
                    evf.insert(QStringLiteral("rows"), QVariantList());
                    evf.insert(QStringLiteral("failReason"),
                               QStringLiteral("记录文件打不开（2 个文件全部读取失败）。"));
                    evf.insert(QStringLiteral("failImpact"),
                               QStringLiteral("记录大概率还在磁盘上，可能被其他程序占用或权限不足。"));
                    QVariantMap svf;
                    svf.insert(QStringLiteral("summary"), QString());
                    svf.insert(QStringLiteral("rows"), QVariantList());
                    svf.insert(QStringLiteral("failReason"),
                               QStringLiteral("记录文件内容无法解析（5 行损坏）。"));
                    svf.insert(QStringLiteral("failImpact"),
                               QStringLiteral("原始文件没有被删除，仍可用「打开文件夹」取出。"));
                    ui.applyNoiseLogView(QString(), evf,
                                         evf.value(QStringLiteral("failReason")).toString(),
                                         evf.value(QStringLiteral("failImpact")).toString());
                    ui.applyStudyLogView(QString(), svf,
                                         svf.value(QStringLiteral("failReason")).toString(),
                                         svf.value(QStringLiteral("failImpact")).toString());
                    ui.publish();
                    QCoreApplication::processEvents();
                    snap(QStringLiteral("records_error"));
                    check(ui.noiseLoadFailed(), QStringLiteral("噪音读取失败标志位为真"));
                    check(hasShownText(root, QStringLiteral("噪音记录读取失败")),
                          QStringLiteral("记录页错误态上屏"));
                    check(hasShownText(root, QStringLiteral("重新读取")),
                          QStringLiteral("错误态带重试按钮"));
                    check(!hasShownText(root, QStringLiteral("还没有噪音记录")),
                          QStringLiteral("错误态与空态互斥（失败时不显示「还没有记录」）"));

                    // ── 几何断言：错误态底块必须有真实尺寸（真正能抓 bug 的判据）────────
                    // 2026-10-31 两次写错的经验，记下来免得后人再走一遍：
                    //   ① 写"两个 Text 的 bounding box 不相交" → 坏版本也全 PASS。
                    //      因为 QML 重叠的是**渲染出的字形**，item 的 box 从不超出自己。
                    //   ② 写"正文不溢出底块" → 坏版本也全 PASS。同理。
                    // 真正区分好坏的是：**底块（那个带 radius 的红块）高度塌没塌**。
                    // 实测：好版本红底 236 行像素、跨 y=252..656；坏版本红底 **0 行**
                    // （implicitHeight 算矮 + body 用 verticalCenter 铺 → 块被压成一条，底色画不出来）。
                    // 这里直接查 item 几何，不依赖截图。
                    {
                        auto *tTitle = findTextItem(root, QStringLiteral("噪音记录读取失败"));
                        auto *tBody = findTextItem(root, QStringLiteral(
                            "记录文件打不开（2 个文件全部读取失败）。\n"
                            "记录大概率还在磁盘上，可能被其他程序占用或权限不足。"));
                        check(tTitle && tBody,
                              QStringLiteral("错误态标题块与正文块都能定位到 item"));
                        if (tTitle && tBody) {
                            auto *rootItem = qobject_cast<QQuickItem *>(root);
                            // 往上找第一个"有 radius 属性"的父 item = 错误态的红底块
                            QQuickItem *box = tTitle->parentItem();
                            while (box && box != rootItem
                                   && box->metaObject()->indexOfProperty("radius") < 0) {
                                box = box->parentItem();
                            }
                            const bool boxFound = box && box != rootItem;
                            check(boxFound, QStringLiteral("找到错误态底块"));
                            if (boxFound) {
                                // ① 底块必须有真实高度（坏版本会塌成一行/0）
                                check(box->height() > 40,
                                      QStringLiteral("错误态底块有真实高度（坏版本会塌陷）"));
                                // ② 底块必须比正文块更高（能包住它）
                                check(box->height() > tBody->height(),
                                      QStringLiteral("错误态底块高于正文块（包得住）"));
                                // ③ 底块宽度必须接近内容区（塌成 0 宽时文字会竖排）
                                check(box->width() > rootItem->width() * 0.8,
                                      QStringLiteral("错误态底块铺满内容区宽度"));
                            }
                        }
                    }
                    // 恢复有数据形状（后续广播机截图虽不涉记录页，仍保持一致）
                    agentUiFill(&ui);
                    emit ui.infoChanged();
                    QCoreApplication::processEvents();
                }
            }

            // 用途切到广播机：页清单多一页「广播台」（首屏），这是本轮壳绑定链的验证点
            const int errBefore = g_qmlErrors.size();
            g_probeRole = QStringLiteral("broadcast");
            agentUiFill(&ui);
            emit ui.infoChanged();
            QCoreApplication::processEvents();
            const int bcCount = pageHost->property("count").toInt();
            // ⚠️ 页数**不变**（=6）是正确结果，别按"+1"判：广播机是「+广播台 **−记录**」
            //    （records 只有教室机有，见 AgentPages.js 的 roles）。判据必须是**内容**：
            //    首屏变 broadcastconsole、records 消失。
            // ⚠️ 也别在 goTo 之前读 currentId：manifest 变了但 currentIndex 还停在 5，
            //    读出来是 broadcastclub —— 那是"没切页"，不是"没换清单"。
            QCoreApplication::processEvents();
            check(bcCount == pageCount,
                  QStringLiteral("广播机页清单仍 %1 页（+广播台 −记录）").arg(bcCount));
            QStringList bcIds;
            for (int i = 0; i < bcCount; ++i) {
                QMetaObject::invokeMethod(pageHost, "goTo", Q_ARG(QVariant, i));
                QCoreApplication::processEvents();
                const QString id = pageHost->property("currentId").toString();
                bcIds << id;
                snap(QStringLiteral("bc_%1_%2").arg(i, 2, 10, QChar('0')).arg(id));
            }
            check(bcIds.value(0) == QStringLiteral("broadcastconsole"),
                  QStringLiteral("广播机首屏 = broadcastconsole（实得 %1）").arg(bcIds.value(0)));
            check(!bcIds.contains(QStringLiteral("records")),
                  QStringLiteral("广播机清单不含 records（教室机专有页已过滤）"));
            const QStringList newErrs = g_qmlErrors.mid(errBefore);
            check(newErrs.isEmpty(),
                  QStringLiteral("切用途无新增 QML 报错（%1 条）").arg(newErrs.size()));
            for (const QString &e : newErrs) out << QStringLiteral("      · %1\n").arg(e.left(160));

            // 还原成教室机，免得后面若加断言拿到广播机清单
            g_probeRole = QStringLiteral("classroom");
            agentUiFill(&ui);
            emit ui.infoChanged();
            QCoreApplication::processEvents();
        }

        out << QStringLiteral("== 汇总：PASS %1 / FAIL %2 ==\n").arg(g_pass).arg(g_fail);
        out.flush();
        app.exit(g_fail == 0 ? 10 : 20);
    });

    // 兜底超时
    // 2026-10-30：加逐屏截图（21 屏 × 260~800ms 等待）后 15s 贴边，放宽到 40s
    QTimer::singleShot(40000, &app, [&] {
        out << QStringLiteral("== 汇总：PASS %1 / FAIL %2（兜底超时）==\n").arg(g_pass).arg(g_fail + 1);
        out.flush();
        app.exit(30);
    });

    const int rc = app.exec();
    out.flush();
    return rc;
}
// moc 产物由 scripts/_agent_ui_probe.py 单独编译（moc → _agent_ui_probe_moc.cpp），
// 不在本文件 include（探针自己编，不走 AUTOMOC）。
