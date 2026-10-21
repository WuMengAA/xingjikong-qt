// 星集控管理端 · MD3 页面级骨架（Expanded 宽屏断点）· V1.0 P1
//
// 规格（用户点单）：
//   断点       Expanded（宽屏）
//   左侧导航栏  Sidebar 宽 240px，背景 SurfaceVariant，菜单「仪表盘 / 班级列表 / 设置」
//   主内容区    占满剩余空间，背景 Surface
//     └ TopBar    高 64px：标题「设备监控」+ 班级切换下拉框 + 用户头像
//     └ ActionBar 距顶部 24px：[ 一键锁屏 ] [ 批量重启 ] 两个 SecondaryButton
//     └ Grid      距操作区 16px：4 列网格，卡片间距 16px；卡片内边距 24px，纯白，圆角 16px
//   Z 轴       点设备 ⇒ 右侧滑出 400px SideSheet，Level 3 阴影，主背景 50% 黑遮罩
//
// ── 为什么是"新增骨架层"而不是改 Main.qml 的壳 ──────────────────────
// Main.qml 现在是「底栏 4 标签 + StackLayout」的旧壳，里面有大量存量逻辑
// （灵动岛、命令面板、弹窗互斥、帧订阅…）。**直接换壳 = 一次性推翻交付物**，
// 与「不许在烂地基上叠补丁」同样危险 —— 正确顺序是：
//   ① 新建骨架层（本文件 + shell/），挂成新页；② 用真实数据验收；③ 再决定迁移。
// 因此本文件**不删不改**任何现有页。
//
// ── 数据真源（全部沿用，不另存一份）───────────────────────────────────
//   班级目录   backend.classes   [{code,name}]
//   当前班     backend.currentClass / backend.setCurrentClass(code)
//   设备表     backend.devices     [{uid,name,classCode,online,version,lastSeenAgoSec}]
//   单台帧     image://frames/<uid>?t<tick>（FrameImageProvider，main.cpp:253-261）
//   帧订阅     backend.subscribeThumbnails(uids) + 信号 thumbnailChanged()
//   动作执行   ctx.window.runActionById(actionId)（权限/能力/二次确认全在宿主）
//
// ⚠️ 本页刻意**不用** ctx.window.filteredDevices：它叠加了「全部/在线/离线」筛选
//    （集控页左栏三枚 chip 的状态），老师筛过"离线"再进本页会只剩离线设备。
//
// ctx 形状与 pages/ControlPage.qml 同一份契约（Main.qml 的 _pageCtx）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/md3"
import "../shell"          // Sidebar / TopBar / ActionBar / DeviceCard / SideSheet
import "../island"         // SvgIcon

Item {
    id: shell

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // ── 侧栏菜单（文案走教育 B 端规范）────────────────────────────────
    // icon 用 lucide 图标名（随 exe 打包），不用 emoji —— emoji 在本机会渲染成方块。
    readonly property var navItems: [
        { key: "dash",     icon: "monitor",     label: "仪表盘" },
        { key: "classes",  icon: "users",       label: "班级列表" },
        { key: "settings", icon: "settings",    label: "系统设置" }
    ]

    /** 当前栏目：dash / classes / settings。 */
    property string section: "dash"

    /** 侧滑面板展示哪台设备（"" = 关闭）。 */
    property string openUid: ""

    /**
     * 缩略帧订阅上限：UI 取前 9 台（沿用「多班墙最多 9 台」的既有口径）；
     * 后端另有 LRU 32 台硬上限兜底 —— 超出只保留最近订阅的那批。
     */
    readonly property int thumbLimit: 9

    /** 本班设备：按班级码过滤；currentClass 为空 = 「全部班级」⇒ 不过滤（理由见文件头）。 */
    function classDevices() {
        const ds = (backend && backend.devices) ? backend.devices : []
        const cc = backend.currentClass || ""
        if (cc === "") return ds.slice()        // 全部班级：显示全部设备，不是"什么都没"
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            if (!ds[i]) continue
            if ((ds[i].classCode || "") === cc) out.push(ds[i])
        }
        return out
    }

    /** 当前栏目在 StackLayout 里的下标。 */
    readonly property int sectionIndex: shell.section === "settings" ? 2
                                      : (shell.section === "classes" ? 1 : 0)

    /** 把"当前班全部在线设备"塞进宿主的 picked。返回台数（0 = 没东西可发）。 */
    function pickAllOnline() {
        const ds = shell.classDevices()
        var u = []
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) u.push(ds[i].uid)
        if (shell.ctx) shell.ctx.window.picked = u
        return u.length
    }
    /** 批量动作：把"当前班全部在线设备"塞进宿主的 picked，再走唯一执行入口。
     *  ⚠️ actionId 必须是 **pick 系列批量 id**（pick-lock / pick-shutdown）：
     *    单台 id（lock/reboot）在 runActionById 的 danger 分支只看 backend.currentUid
     *    （Main.qml:1532）—— 批量会被「先在控制页选中一台设备」拦截，或只锁那一台。
     *    pick 系列走 runOnPicked：台数确认框 + doBatch 逐台 sendActionTo + 清 picked。 */
    function runBulk(actionId) {
        if (!shell.ctx) return
        const n = shell.pickAllOnline()
        if (n === 0) { shell.toast("当前班没有在线设备，无法批量下发", true); return }
        const label = (actionId === "pick-lock") ? "锁定桌面" : "远程重启"
        shell.ctx.window.runActionById(actionId)
        // H3：批量经宿主 runActionById 执行，但结果走 root.toast → 灵动岛，
        //     该浮层在 page 6 被隐藏（Main.qml:1299），老师看不到"点了有没有反应"。
        //     这里先自绘一句本地回执，证明动作已派发（执行成败由 runActionById 内部处理）。
        shell.toast("已对 " + n + " 台在线设备下发「" + label + "」")
    }

    // ── H3 页级 Toast（骨架页自绘，不依赖灵动岛）─────────────────────
    // ⚠️ 灵动岛浮层在 page 6 被隐藏（Main.qml:1299 visible: root.page !== 6），
    //    root.toast() 走 bridge.showNote → 灵动岛，这个页面上根本看不见。
    //    本地动作（批量下发 / RBAC 拒绝 / 登录）的反馈必须自绘。
    // 颜色用固定深色底 + 白字（token 集无 inverseSurface，故不依赖主题反向色）。
    function toast(msg, isErr) {
        if (!msg) return
        toastText.text = msg
        toastBox.color = isErr ? (shell.th ? shell.th.cError : "#B3261E") : "#322F35"
        toastLayer.visible = true
        toastHide.restart()
    }

    // ── H4 状态条文案（现算：设备总数 / 在线数 + 当前班 + 正在查看设备）──
    function _statusText() {
        const ds = shell.classDevices()
        const total = ds.length
        var on = 0
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) on++
        let cls = "全部班级"
        if (backend.currentClass) {
            cls = backend.currentClass
            const cs = (backend.classes) ? backend.classes : []
            for (var c = 0; c < cs.length; ++c)
                if (cs[c] && cs[c].code === backend.currentClass) { cls = cs[c].name || cs[c].code; break }
        }
        var s = "班级 " + cls + " · " + total + " 台 · " + on + " 台在线"
        if (shell.openUid !== "") {
            for (var j = 0; j < ds.length; ++j)
                if (ds[j].uid === shell.openUid) { s += " · 正在查看 " + (ds[j].name || ds[j].uid); break }
        }
        return s
    }

    Rectangle { anchors.fill: parent; color: shell.th ? shell.th.cSurface : "#FFFBFE" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ══ 左侧导航栏（240px · SurfaceVariant）══
            Sidebar {
                id: nav
                md3: shell.th
                ctx: shell.ctx
                items: shell.navItems
                current: shell.section
                // H5：点「系统设置」先过 RBAC 门禁 —— 受限身份不切栏、弹页级提示。
                // 原本直接切 section，等于绕过了 goPage() 里那道 perms.can 拦截
                // （Main.qml:1262），老师点设置会"进了页面但啥也干不了"。
                onActivated: function (key) {
                    if (key === "settings" && (typeof perms === "undefined" || !perms.can("system:settings"))) {
                        shell.toast("当前身份不能进系统设置", true)
                        return
                    }
                    shell.section = key
                }
            }

        // ══ 主内容区（Surface）══
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                // 整页左右留白：主区不再贴侧栏 / 贴窗边（qml-md3 Gallery 的 pagePadding 纪律）
                anchors.leftMargin: shell.th ? shell.th.spL : 24
                anchors.rightMargin: shell.th ? shell.th.spL : 24
                spacing: 0

                // ── 顶部导航栏（64px）──
                TopBar {
                    id: topBar
                    ctx: shell.ctx
                    md3: shell.th          // 颜色令牌：不传时 md3={} 恒真 ⇒ 颜色取到 undefined
                    title: "设备监控"
                    // ⚠️ 班级列表与当前班**不在这里传参** —— TopBar 内部直接读
                    //    backend.classes / backend.currentClass（与集控页同一套路）。
                    //    曾经这里多传了 classes / currentClass 两个 TopBar 上不存在的属性，
                    //    结果整个 Main.qml `Type Md3Shell unavailable` **直接起不来** ——
                    //    qmllint 报 0 Error，只有真跑才暴露。
                    onClassPicked: function (code) { backend.setCurrentClass(code) }
                    onAvatarClicked: function () { if (shell.ctx) shell.ctx.window.goPage(2) }
                    onLoginRequested: {
                        shell.toast("正在打开站点登录…")
                        if (backend) backend.loginWithSite()
                    }
                }

                // ── 操作区（距顶部 24px）──
                ActionBar {
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    ctx: shell.ctx
                    // 批量动作必须先把"本班在线设备"塞进宿主 picked，再走唯一执行入口；
                    // id 用 pick 系列（批量语义），不能用单台 id —— 单台 danger 分支只看 currentUid。
                    onLockRequested: shell.runBulk("pick-lock")
                    onRebootRequested: shell.runBulk("pick-shutdown")
                }

                // ── 内容区（距操作区 16px）──
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 16
                    currentIndex: shell.sectionIndex

                    // ① 仪表盘：4 列设备网格（统计/班级已在 TopBar 展示，内容区不再重复）
                    Flickable {
                        contentWidth: width          // 纪律：显式声明，否则列宽算不出来
                        contentHeight: gridCol.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: gridCol
                            width: parent.width
                            spacing: 16

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                columnSpacing: 16
                                rowSpacing: 16

                                Repeater {
                                    model: shell.classDevices()
                                    delegate: DeviceCard {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 168
                                        md3: shell.th
                                        uid: modelData.uid || ""
                                        deviceName: modelData.name || ""
                                        online: modelData.online !== false
                                        version: modelData.version || ""
                                        lastSeenAgoSec: (modelData.lastSeenAgoSec === undefined)
                                                      ? -1 : modelData.lastSeenAgoSec
                                        selected: shell.openUid === (modelData.uid || "")
                                        onClicked: {
                                            const uid = modelData.uid || ""
                                            shell.openUid = uid
                                            // H1：同时写宿主的「看哪台」真源 —— SideSheet 三键
                                            // （screenshot/lock/reboot）与危险动作的目标都从
                                            // backend.currentUid 取（Main.qml:1599/1606/1532）。
                                            // 只设 openUid 的话，单机动作会发到空目标或被
                                            // 「先在控制页选中一台设备」拦截（B1/B2 硬缺口）。
                                            if (uid !== "" && backend) backend.currentUid = uid
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: shell.classDevices().length === 0
                                text: "这个班还没有设备"
                                font.pixelSize: 12
                                color: shell.th ? shell.th.cOutline : "#79747E"
                            }
                        }
                    }

                    // ② 班级列表：复用已交付的看板页（零重复，行为与 page 4 一致）
                    Loader {
                        source: "AdminDashboard.qml"
                        onLoaded: if (item) item.ctx = shell.ctx
                    }

                    // ③ 系统设置：复用已交付的设置页（零重复）
                    Loader {
                        source: "SettingsPage.qml"
                        onLoaded: if (item) item.ctx = shell.ctx
                    }
                }
            }
        }
        }

        // ── H4 状态条（全宽贴底）──────────────────────────────────
        // 展示：当前班级范围设备总数 / 在线数 + 当前班级 + 正在查看的设备名。
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: shell.th ? shell.th.cSurfaceVar : "#E7E0EC"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16
                Text {
                    Layout.fillWidth: true
                    text: shell._statusText()
                    font.pixelSize: 12
                    color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                    elide: Text.ElideRight
                }
            }
        }
    }

    // ══ Z 轴上层：侧滑详情面板 + 50% 黑遮罩 ══
    SideSheet {
        ctx: shell.ctx
        opened: shell.openUid !== ""
        device: (function () {
            const ds = shell.classDevices()
            for (var i = 0; i < ds.length; ++i)
                if (ds[i].uid === shell.openUid) return ds[i]
            return null
        })()
        onClosed: shell.openUid = ""
    }

    // ── H3 Toast 浮层（最上层，z 高于 SideSheet）────────────────────
    Item {
        id: toastLayer
        anchors.fill: parent
        z: 1000
        visible: false
        Rectangle {
            id: toastBox
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            radius: 8
            color: "#322F35"
            Text {
                id: toastText
                anchors.centerIn: parent
                text: ""
                font.pixelSize: 13
                color: "#FFFFFF"
                padding: 10
            }
        }
        Timer {
            id: toastHide
            interval: 2600
            onTriggered: toastLayer.visible = false
        }
    }

    // ── 帧订阅：进页面订本班前 N 台在线设备，离开时退订 ────────────────
    // ⚠️ 为什么必须退订：云端按订阅推帧，不退订 = 后台一直拉流
    //    （历史 bug 见 Main.qml onPageChanged 的 2026-10-08 注释）。
    function syncThumbs() {
        const ds = shell.classDevices()
        var u = []
        for (var i = 0; i < ds.length && u.length < shell.thumbLimit; ++i)
            if (ds[i].online !== false && ds[i].uid !== shell.openUid) u.push(ds[i].uid)
        backend.subscribeThumbnails(u)
    }

    Connections {
        target: backend
        function onDevicesChanged() {
            shell.syncThumbs()
        }
    }
    Connections {
        target: shell
        function onOpenUidChanged() { shell.syncThumbs() }
    }

    Component.onCompleted: shell.syncThumbs()
    Component.onDestruction: backend.subscribeThumbnails([])
}
