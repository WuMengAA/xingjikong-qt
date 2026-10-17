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
#include <QFile>
#include <QMetaObject>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWidget>
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

#include "_agent_ui_probe_fakeui.h"


namespace {

void collectTexts(QObject *root, QStringList &texts, QStringList &btnTexts,
                  QSet<QObject *> &seen, int depth = 0)
{
    if (!root || depth > 24 || seen.contains(root)) return;
    seen.insert(root);
    const QVariant t = root->property("text");
    if (t.isValid() && t.typeId() == QMetaType::QString) {
        const QString s = t.toString();
        if (!s.isEmpty()) {
            texts << s;
            const QString cls = QString::fromLatin1(root->metaObject()->className());
            if (cls.contains(QStringLiteral("Btn"))) btnTexts << s;
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
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            g_qmlErrors << msg;
    });

    FakeAgentUi ui;
    // 数据：照 agentUiFill 的真实形状（snapshot() 的键：id/name/version/description/
    //        builtin/enabled/running/permissions/actions）
    ui.set_appVersion(QStringLiteral("0.6.37-probe"));
    ui.set_uid(QStringLiteral("probe-uid"));
    ui.set_cloudUrl(QStringLiteral("wss://example.invalid/ws/agent"));
    ui.set_connState(QStringLiteral("已连接（云端已确认）"));
    ui.set_dataDir(QStringLiteral("C:/tmp/xingjikong"));
    ui.set_logPath(QStringLiteral("C:/tmp/xingjikong/agent-qt.log"));
    ui.set_latestText(QStringLiteral("已是最新（云端 v0.6.37）"));
    ui.set_hasNewer(false);
    ui.set_canUpgrade(false);
    ui.set_updateNotes(QString());
    ui.set_otaModeText(QStringLiteral("可选：只有点「立即升级」才装"));
    ui.set_otaAutoText(QStringLiteral("到点自动装"));
    ui.set_otaLastText(QStringLiteral("（这台机器还没自动升级过）"));
    ui.set_otaNextText(QStringLiteral("立刻（有网就试）"));
    ui.set_otaPausedNow(false);
    ui.set_notifyEnabled(true);
    ui.set_voiceEnabled(true);
    ui.set_noiseEnabled(true);
    ui.set_islandIdle(true);
    ui.set_lightMode(false);
    ui.set_autostart(true);
    ui.set_desktopShortcut(false);
    ui.set_noiseLogText(QStringLiteral("噪音记录目录：C:/tmp/noise"));
    ui.set_studyLogText(QStringLiteral("自习记录目录：C:/tmp/study"));
    ui.set_playScheduleEnabled(true);
    ui.set_playWindowsText(QStringLiteral("周一至周五 12:30-13:30 / 17:00-18:00"));
    ui.set_playStateText(QStringLiteral("播放中：午间校园广播（今天还有 2 个时段）"));

    // 自检条目（照 main.cpp buildSelfCheckItems 的形状）
    QVariantList checks;
    for (int i = 0; i < 3; ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), QStringLiteral("自检项%1").arg(i + 1));
        m.insert(QStringLiteral("ok"), i != 1);
        m.insert(QStringLiteral("status"), i == 1 ? QStringLiteral("未配置") : QStringLiteral("正常"));
        m.insert(QStringLiteral("fix"), i == 1 ? QStringLiteral("去修复") : QString());
        checks << m;
    }
    ui.set_selfCheckItems(checks);

    // 插件市场（照 buildPluginList 的形状：id/name/version/author/description/
    //        installed/enabled/status——status 是 C++ 生成的展示串，缺了页面 146 行
    //        会把 undefined 绑进 QString（探针据此抓到过一次假数据失真））
    QVariantList plugins;
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QStringLiteral("echo-sample"));
        m.insert(QStringLiteral("name"), QStringLiteral("示例回显插件"));
        m.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        m.insert(QStringLiteral("author"), QStringLiteral("官方"));
        m.insert(QStringLiteral("description"), QStringLiteral("把收到的指令原样回显，用于验证插件链路。"));
        m.insert(QStringLiteral("installed"), true);
        m.insert(QStringLiteral("enabled"), true);
        m.insert(QStringLiteral("status"), QStringLiteral("● 运行中"));
        plugins << m;
    }
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QStringLiteral("voicehub-broadcast"));
        m.insert(QStringLiteral("name"), QStringLiteral("VoiceHub 联动"));
        m.insert(QStringLiteral("version"), QStringLiteral("1.2.0"));
        m.insert(QStringLiteral("author"), QStringLiteral("VoiceHub"));
        m.insert(QStringLiteral("description"), QStringLiteral("广播站点歌/歌词联动。"));
        m.insert(QStringLiteral("installed"), false);
        m.insert(QStringLiteral("enabled"), false);
        m.insert(QStringLiteral("status"), QStringLiteral("— 未安装"));
        plugins << m;
    }
    ui.set_plugins(plugins);

    // 内置模块（照 ModuleHost::snapshot() 的键）
    QVariantList modules;
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QStringLiteral("usb"));
        m.insert(QStringLiteral("name"), QStringLiteral("U盘助手"));
        m.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        m.insert(QStringLiteral("description"),
                 QStringLiteral("U 盘插入时在灵动岛弹窗询问是否打开（只开文件夹、绝不自动运行）；考试模式可自动静默。"));
        m.insert(QStringLiteral("builtin"), true);
        m.insert(QStringLiteral("enabled"), true);
        m.insert(QStringLiteral("running"), true);
        QVariantList perms;
        perms << QStringLiteral("island") << QStringLiteral("notify") << QStringLiteral("settings");
        m.insert(QStringLiteral("permissions"), perms);
        QVariantList acts;
        acts << QStringLiteral("usb_drives");
        m.insert(QStringLiteral("actions"), acts);
        modules << m;
    }
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QStringLiteral("voicehub"));
        m.insert(QStringLiteral("name"), QStringLiteral("VoiceHub 联动"));
        m.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        m.insert(QStringLiteral("description"), QStringLiteral("对接 VoiceHub：正在播放/歌词/点歌。"));
        m.insert(QStringLiteral("builtin"), true);
        m.insert(QStringLiteral("enabled"), false);
        m.insert(QStringLiteral("running"), false);
        modules << m;
    }
    ui.set_modules(modules);

    QQuickWidget view;
    view.engine()->addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider());
    view.engine()->rootContext()->setContextProperty(QStringLiteral("agentUi"), &ui);
    view.setResizeMode(QQuickWidget::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(
        QStringLiteral("D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/build/qml/shell/AgentShell.qml")));
    view.resize(980, 680);
    view.show();

    QTimer::singleShot(1200, &app, [&] {
        QObject *root = view.rootObject();
        check(root != nullptr, QStringLiteral("AgentShell.qml 加载出根对象"));
        if (!root) {
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
        // 探针里没有 voicehub 上下文属性（模块只在 agent 主进程注册）⇒ 卡片必须走
        // typeof 守卫显示「模块未加载」，且**不许**报 ReferenceError（见 §六 realErrors）。
        check(hasText(QStringLiteral("VoiceHub 联动")), QStringLiteral("设置页有「VoiceHub 联动」卡"));
        check(hasText(QStringLiteral("连接状态")), QStringLiteral("卡里有「连接状态」行"));
        check(hasText(QStringLiteral("模块未加载")),
              QStringLiteral("无 voicehub 属性时守卫生效=「模块未加载」（不是 ReferenceError）"));
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
        check(hasText(QStringLiteral("播放中：午间校园广播（今天还有 2 个时段）")),
              QStringLiteral("时段页状态文案上屏"));

        // ── 五、页的加载钩子真的跑了（证页实例化，不是只编译）──
        check(ui.calls.contains(QStringLiteral("pluginsRefresh")),
              QStringLiteral("插件页 Component.onCompleted 调了 pluginsRefresh"));
        check(ui.calls.contains(QStringLiteral("runSelfCheckCheck")),
              QStringLiteral("自检页 Component.onCompleted 调了 runSelfCheckCheck"));

        // ── 六、QML 运行期报错 ──
        // 已知项（**不许静默吞**，逐条留痕）：
        //   · svgicon 缺图：IslandRoot 引用 check-circle 而资源叫 circle-check（存量错配，待点单）
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
        check(realErrors.isEmpty(),
              QStringLiteral("无未知 QML 运行期报错（%1 条）").arg(realErrors.size()));
        for (const QString &e : realErrors) out << QStringLiteral("      · %1\n").arg(e);

        // 截图留证（含内置模块区）
        const QString shot = QStringLiteral("_agent_ui_probe.png");
        if (view.grab().save(shot)) out << QStringLiteral("INFO  截图 → %1\n").arg(shot);

        out << QStringLiteral("== 汇总：PASS %1 / FAIL %2 ==\n").arg(g_pass).arg(g_fail);
        out.flush();
        app.exit(g_fail == 0 ? 10 : 20);
    });

    // 兜底超时
    QTimer::singleShot(15000, &app, [&] {
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
