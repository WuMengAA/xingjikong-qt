// 星集控管理端 · 唯一应用外壳（2026-11-01 转正）
//
// ── 来历 ──────────────────────────────────────────────────────────────
// 版面照设计稿 qml/layout/DesktopConsole.qml（2026-10-14 起的新版外壳假数据稿），
// 2026-11-01 先做成「预览版」（设置页开关切），用户看过五个栏目全部真实业务内容后
// **点单转正**：本件从此是 MainWindow 的**唯一**外壳，经典外壳
// （shell/{Sidebar,TopBar,ActionBar,BottomNav,StatusBar}.qml 那套手绘件）已退役删除。
//
// ── 转正时补上的真功能（不能丢的那些）────────────────────────────────
//   ① 班级切换下拉：PreviewTopBar 里走 ClassSort 排序 → classPicked → 宿主 shell.pickClass()
//      （**双写**：旧筛选 setCurrentClass + 新班级域 subscribeClass，2026-10-23 #472 裁决）
//   ② 未登录态「登录」按钮（H8）→ backendRef.loginWithSite()
//   ③ 头像 → 个人中心（**不**是系统设置：system:settings 有 RBAC 门禁，老师点自己头像会被挡）
//   ④ 状态条（PreviewStatusBar，文案由宿主 _statusText() 现算）
//   ⑤ 竖屏底部导航（PreviewBottomNav，与左导航轨同一份 navItems）
//
// ── 不归本件管的上层浮层 ──────────────────────────────────────────────
//   SideSheet（设备详情抽屉）/ ToastLayer / ReauthDialog 挂在 MainWindow 根节点上，
//   与外壳无关 —— 转正时**原样保留**，本件不重复挂一份。
//
// ── 有意偏差（相对设计稿）──────────────────────────────────────────────
//   图标用 lucide SVG（image://svgicon/<name>）而不是设计稿的 emoji
//   —— 设计稿用 emoji，本机会渲染成方块；且项目已有 SVG 图标库（随 exe 打包）。
//
// 分层（AGENTS §10.9）：QML 只画图。数据从宿主注入、点击回调给宿主，不在 QML 里做业务判断。
//
// ⚠️ 件装配清单（各自 ≤200 行红线，§10.5）：
//   PreviewNavRail.qml（左导航轨）/ PreviewTopBar.qml（顶栏）/ PreviewStatsRow.qml（统计行）/
//   PreviewStatusBar.qml（状态条）/ PreviewBottomNav.qml（竖屏导航）/
//   ShellContent.qml（业务页，六个栏目）
import QtQuick
import "../components/theme"

Item {
    id: root

    // ── 输入（由 MainWindow 注入）─────────────────────────────────────
    property var ctx: null
    property var backendRef: null
    property var navItems: []
    property string section: "dash"
    property string sectionTitle: ""
    property string sectionSubtitle: ""
    property string accountTitle: ""
    property string accountSub: ""
    property string accountAvatarText: ""
    property string currentClass: ""
    property var devices: []
    property var stats: ({})
    property int sectionIndex: 0
    property bool portrait: false
    property string filterMode: "all"
    property string openUid: ""
    property var host: null
    property var toastFn: null
    property string statusLine: ""
    property int statusBarH: 32
    property int navBarH: 80

    signal navActivated(string key)
    signal avatarClicked()
    signal deviceActivated(string uid)
    signal classPicked(string code)
    signal loginRequested()

    readonly property int onlineCount: {
        var n = 0
        for (var i = 0; i < root.devices.length; ++i)
            if (root.devices[i].online !== false) n++
        return n
    }

    // 2026-11-03：窄屏（手机竖屏）边距收紧。桌面 24 的留白在 390pt 上等于吃掉
    // 12% 的内容宽度；手机是"宽度稀缺"，必须把钱花在内容上。
    readonly property int edgeMargin: root.portrait ? Theme.spacing.md : Theme.spacing.lg

    Rectangle {
        anchors.fill: parent
        color: Theme.color.background
    }

    // ══ 横屏：左导航轨 ═══════════════════════════════════════════════
    PreviewNavRail {
        id: rail
        visible: !root.portrait
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        anchors.left: parent.left
        navItems: root.navItems
        section: root.section
        accountTitle: root.accountTitle
        accountSub: root.accountSub
        accountAvatarText: root.accountAvatarText
        onNavActivated: function (key) { root.navActivated(key) }
        onAvatarClicked: function () { root.avatarClicked() }
    }

    // ══ 右侧主区（顶栏 + 统计行 + 内容）════════════════════════════════
    Item {
        id: main
        anchors.top: parent.top
        anchors.left: root.portrait ? parent.left : rail.right
        anchors.right: parent.right
        anchors.bottom: root.portrait ? bottomNav.top : statusBar.top

        PreviewTopBar {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            compact: root.portrait
            ctx: root.ctx
            backendRef: root.backendRef
            currentClass: root.currentClass
            title: root.sectionTitle
            subtitle: root.sectionSubtitle
            accountAvatarText: root.accountAvatarText
            onClassPicked: function (code) { root.classPicked(code) }
            onAvatarClicked: function () { root.avatarClicked() }
            onLoginRequested: function () { root.loginRequested() }
        }

        PreviewStatsRow {
            id: statsRow
            anchors.top: topBar.bottom
            anchors.topMargin: root.edgeMargin === Theme.spacing.md ? Theme.spacing.sm : Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: root.edgeMargin
            anchors.right: parent.right
            anchors.rightMargin: root.edgeMargin
            compact: root.portrait
            deviceTotal: root.devices.length
            onlineCount: root.onlineCount
            offlineCount: root.devices.length - root.onlineCount
        }

        // 业务页（六个栏目，与 C++/宿主同一份数据）
        ShellContent {
            id: content
            anchors.top: statsRow.bottom
            anchors.topMargin: root.edgeMargin === Theme.spacing.md ? Theme.spacing.sm : Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: root.edgeMargin
            anchors.right: parent.right
            anchors.rightMargin: root.edgeMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacing.md
            sectionIndex: root.sectionIndex
            ctx: root.ctx
            host: root.host
            backendRef: root.backendRef
            devices: root.devices
            stats: root.stats
            portrait: root.portrait
            filterMode: root.filterMode
            openUid: root.openUid
            toastFn: root.toastFn
        }
    }

    // ══ 竖屏：底部导航 ═══════════════════════════════════════════════
    PreviewBottomNav {
        id: bottomNav
        visible: root.portrait
        anchors.bottom: statusBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        navItems: root.navItems
        section: root.section
        onNavActivated: function (key) { root.navActivated(key) }
    }

    // ══ 状态条（全宽贴底，朝向无关）═══════════════════════════════════
    PreviewStatusBar {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        barHeight: root.statusBarH
        text: root.statusLine
    }
}
