// 星集控管理端 · 新版外壳预览（2026-11-01）
//
// 这是 qml/layout/DesktopConsole.qml（新版外壳设计稿，2026-10-14 起的假数据版式）
// 的**真数据**版本：观感照设计稿，数据全部接 backend。
//
// 定位：**预览版**。用户在设置里打开「新版外观预览」后切到这一套；
// 关掉则回到经典外壳（qml/shell/Sidebar.qml + TopBar.qml + ActionBar.qml）。
// 确认满意后才考虑把整套外壳换成这一版（§10.8 需点单）。
//
// 与设计稿的两处**有意偏差**（理由写在各节注释里）：
//   ① 图标用 lucide SVG（image://svgicon/<name>）而不是 emoji
//      —— 设计稿用 emoji，本机会渲染成方块；且项目已有 SVG 图标库（随 exe 打包）。
//   ② 内容区装**真实业务页**（与经典外壳同一份 qml/shell/ShellContent.qml），
//      不是设计稿的假数据 —— 六个栏目全部可用。
//
// 分层（AGENTS §10.9）：QML 只画图。数据从 backend 读、点击调 shell 既有方法，
// 不在 QML 里做业务判断。
//
// ⚠️ 本件是**骨架**（装配），具体视觉在同目录的 Preview* 件里（各自 ≤200 行红线）：
//   PreviewNavRail.qml（左导航轨） / PreviewTopBar.qml（顶栏） /
//   PreviewStatsRow.qml（统计行） / PreviewDeviceGrid.qml（设备网格） /
//   ShellContent.qml（业务页，两套外壳共用）。
import QtQuick
import "../components/theme"

Item {
    id: root

    // ── 输入（由 MainWindow 注入，与经典外壳同一套数据源）──────────────
    property var ctx: null
    property var backendRef: null
    property var navItems: []
    property string section: "dash"
    property string accountTitle: ""
    property string accountSub: ""
    property string accountAvatarText: ""
    property string currentClass: ""
    /** 业务页要用的数据（主壳算好给这里，避免两处各算一份）。 */
    property var devices: []
    property var stats: ({})
    property int sectionIndex: 0
    property bool portrait: false
    property string filterMode: "all"
    property string openUid: ""
    property var host: null
    property var toastFn: null

    signal navActivated(string key)
    signal avatarClicked()
    /** 点开某台设备（uid）—— 由 MainWindow 接到既有 openUid 逻辑。 */
    signal deviceActivated(string uid)

    readonly property int onlineCount: {
        var n = 0
        for (var i = 0; i < root.devices.length; ++i)
            if (root.devices[i].online !== false) n++
        return n
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.color.background
    }

    // ══ 左导航轨 ═══════════════════════════════════════════════════
    PreviewNavRail {
        id: rail
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        navItems: root.navItems
        section: root.section
        accountTitle: root.accountTitle
        accountSub: root.accountSub
        accountAvatarText: root.accountAvatarText
        onNavActivated: function (key) { root.navActivated(key) }
        onAvatarClicked: function () { root.avatarClicked() }
    }

    // ══ 右侧主区（顶栏 + 统计行 + 内容）══════════════════════════════
    Item {
        id: main
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: rail.right
        anchors.right: parent.right

        PreviewTopBar {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            currentClass: root.currentClass
            accountAvatarText: root.accountAvatarText
            onAvatarClicked: function () { root.avatarClicked() }
        }

        PreviewStatsRow {
            id: stats
            anchors.top: topBar.bottom
            anchors.topMargin: Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.lg
            deviceTotal: root.devices.length
            onlineCount: root.onlineCount
            offlineCount: root.devices.length - root.onlineCount
        }

        // 业务页（与经典外壳共用同一份 ShellContent）
        ShellContent {
            id: content
            anchors.top: stats.bottom
            anchors.topMargin: Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.lg
            anchors.bottom: parent.bottom
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
}
