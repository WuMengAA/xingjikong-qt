// QML 热重载实现（2026-10-31）。
//
// 为什么单独一个 .cpp 而不是内联在 agent_ui.h：
//   agent_ui.h 已 707 行、正好卡在白名单登记值，门禁规则是「比登记长 1 行即 error」。
//   内联实现要几十行，会让规模门禁直接变红。本文件远低于 600 红线。
//
// 为什么需要这个能力（用户点单「QML 它可以热更新的」）：
//   没有它，改一行 QML 也要重编 + 重启进程。而重启被控端有两道额外成本：
//     ① 全局互斥体挡住第二个实例，必须先杀掉在跑的那个；
//     ② 强杀会留下 agent-run.flag ⇒ 下次启动被判"上次异常退出"⇒ 进安全模式
//        ⇒ 第三方插件全部不加载，界面与真机不一致。
//   即"为了看一眼颜色，得付一整套副作用"。热重载把这个循环压到 1 秒。
#include "agent_ui.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QQmlEngine>
#include <QTimer>

#include "svgiconprovider.h"   // 图标 provider（与 island 同一份实现）

bool AgentShell::reloadQml()
{
    if (!engine()) {
        qWarning("[agent-shell] 热重载失败：engine 为空");
        return false;
    }
    const QString qml = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/qml/shell/AgentShell.qml");
    if (!QFile::exists(qml)) {
        qWarning("[agent-shell] 热重载失败：找不到 %s", qPrintable(qml));
        return false;
    }

    // ① 清组件缓存：不清会复用已编译结果，改了 QML 也看不到变化
    //    （这是"热重载看起来没用"最常见的成因）。
    engine()->clearComponentCache();

    // ② 先卸再装：直接 setSource 同一 URL 有时不会重建；先给空 URL 强制拆旧树，
    //    避免旧绑定在新对象构造期间对旧指针求值（同 oobe 关窗那条经验）。
    setSource(QUrl());
    setSource(QUrl::fromLocalFile(qml));

    // ③ 重画后重新拉一次快照：否则界面是"空壳"（数据要等下次 refresh 才回来）。
    //    C++ 侧对象（m_ui / 各模块 / 登录态）都没动，只是界面重画。
    m_ui.refresh();

    qInfo("[agent-shell] QML 已热重载（%s）", qPrintable(qml));
    return true;
}

// ── 自动监听：STE_QML_RELOAD=1 时，改 QML 自动重载 ──────────────────────
//
// 为什么用环境变量而不是托盘菜单/热键：
//   main.cpp 卡在 4443 行台账（「只降不升」铁律），加菜单项要多 2 行 ⇒ 门禁直接红。
//   而热重载是开发期能力，用环境变量最合适：生产启动不设该变量，行为完全不变。
//
// 用途：`_dev_run.py` 会设这个变量并同步 QML，
//       于是"改完 QML 存盘 → 界面自动重画"，不用重启进程。
// 边界：只对 QML 生效。改 C++ 必须重编重启（运行时不可能热更已编译的二进制）。
void AgentShell::enableQmlAutoReload()
{
    const QString qmlRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/qml");
    // 只盯改动最频繁的三处：外壳、页面、共享组件。
    // 递归监听整个 qml/ 太重（几百个文件 + Qt 的资源开销）。
    const QStringList dirs = { qmlRoot + QStringLiteral("/shell"),
                               qmlRoot + QStringLiteral("/shell/pages"),
                               qmlRoot + QStringLiteral("/components") };

    auto *watcher = new QFileSystemWatcher(this);
    for (const QString &d : dirs) {
        if (QDir(d).exists()) watcher->addPath(d);
    }

    // 去抖：编辑器保存一次会触发多个事件，且可能在写入中途触发。
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(300);
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                     [debounce](const QString &) { debounce->start(); });
    QObject::connect(debounce, &QTimer::timeout, this, [this, watcher, dirs]() {
        reloadQml();
        // ⚠️ 编辑器"原子保存"= 写临时文件再 rename，这会让 QFileSystemWatcher
        //    丢失该目录的监听。故每次重载后重新 addPath（已存在时 addPath 幂等）。
        for (const QString &d : dirs) {
            if (QDir(d).exists() && !watcher->directories().contains(d))
                watcher->addPath(d);
        }
    });

    qInfo("[agent-shell] QML 自动热重载已启用：改 build/qml 下文件会自动重画");
}

// ── SVG 图标 provider（2026-10-31 修 · 面板图标全空白的根因）──────────────
//
// 现象：开发实例打开控制面板，日志刷几十行
//   `QML SvgIcon: Invalid image provider: image://svgicon/hourglass?color=...`
//   底部导航、设置页侧栏的图标**全部空白**，但界面其余部分正常。
//
// 根因：`svgicon` 只在 island.cpp:288 注册给了**灵动岛自己的 engine**。
//   QQuickWidget 各自持有独立 engine，AgentShell（控制面板）是另一个 QWidget，
//   它的 engine 从未注册该 provider ⇒ 所有 image://svgicon/... 一律解析失败。
//   island.cpp 那行上方的注释其实早就写明过这个坑（"挂错了图标全空白且不报错"），
//   只是新增 AgentShell 时没照做。
//
// 注意：必须在 setSource() **之前**注册，否则首次加载就已经解析失败。
void AgentShell::registerIconProvider()
{
    if (!engine()) return;
    // addImageProvider 对同名重复注册是"替换"语义，重载后再次调用安全。
    engine()->addImageProvider(QStringLiteral("svgicon"), new SvgIconProvider);
}
