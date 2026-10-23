// 星集控管理端 · **全局主框架（App Scaffold 之一：MainWindow）**
//
// 定位（2026-10-22 用户点单「全局统一」+ AGENTS.md §9 全局 UI 构建红线）：
//   本文件是管理端**唯一的应用外壳**——规范里三种全局骨架的第一种
//   （主框架 / 弹窗遮罩 Md3Dialog+SideSheet / OOBE 独立窗口）。
//   **所有业务页面必须长在本骨架里**（经 StackLayout/Loader 挂载），
//   禁止任何页面自绘侧栏 / 顶栏 / 状态条，禁止新建第二个应用外壳。
//
// 结构（规范口径，全部尺寸走命名常量或 Md3 令牌，零硬编码）：
//   横屏：左侧 NavigationDrawer 240（surfaceContainer 底）+ 右侧 64 顶栏 + 内容区
//        （边缘内边距 spL=24）+ 底部 32 状态条；上层 SideSheet（400 右滑）+ Toast。
//   竖屏（2026-10-22 用户点单「竖屏横屏两种布局模式」）：左栏 240 让位给底部
//        BottomNav 80（同一份 navItems 的横排形态），设备网格 4/3/2 列改 1/2 列，
//        S2/S3/S4 双卡改单列堆叠，Toast 抬到底栏上方；SideSheet 宽度自适应。
//        判定用**窗口朝向**（宽<高）而非设备类型：桌面窗拉高也进竖屏，手机旋转自动切。
//
// 沿革：本文件原为 pages/Md3Shell.qml（2026-10-22 更名为 MainWindow.qml 并
//   从 pages/ 移入 shell/——它是骨架不是业务页；内容为"迁移到设计系统"版：
//   手绘统计卡/排行卡/存储卡 → Md3Card，手绘筛选 chip → Md3Chip，
//   Toast 配色 → inverse 令牌，字号/圆角/间距全部走 Md3 令牌）。
//
// 本页承载（对应《骨架迁移缺口清单》S 系列，2026-10-22 补齐）：
//   S1 三张统计卡（在线 / 待办 / 离线）
//   S2 云端存储概况（backend.storage）
//   S3 动作排行 · 近 7 天（backend.actionRanking）
//   S4 最近回执流水（宿主 root.results，最新 8 条）
//   S5 设备筛选 chip（全部 / 在线 / 离线）
//   S6 远控出口 → SideSheet 里「在集控页打开」
//   S7 响应式列数（4 / 3 / 2 列，随内容区宽度）
//
// ── 数据真源（全部沿用，绝不另存一份）───────────────────────────────────
//   班级目录   backend.classes   [{code,name}]
//   当前班     backend.currentClass / backend.setCurrentClass(code)
//   设备表     backend.devices     [{uid,name,classCode,online,version,lastSeenAgoSec}]
//   单台帧     image://frames/<uid>?t<tick>（FrameImageProvider）
//   帧订阅     backend.subscribeThumbnails(uids) + 信号 thumbnailChanged()
//   动作执行   ctx.window.runActionById(actionId)（权限/能力/二次确认全在宿主）
//
// ⚠️ 刻意**不用** ctx.window.filteredDevices：它叠加了集控页左栏 chip 的状态，
//   老师在那页筛过"离线"再进本页会只剩离线设备（本页有自己的 chip，见 S5）。
//
// ctx 形状与 pages/ControlPage.qml 同一份契约（Main.qml 的 _pageCtx）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/md3"     // Md3Card / Md3Chip / Md3Btn（全局组件库，AGENTS.md §9.3）
import "../shell"              // Sidebar / TopBar / ActionBar / DeviceCard / SideSheet（同目录骨架件）
import "../pages"              // ClassDashboard（班级工作台 · #472）
import "../island"             // SvgIcon

Item {
    id: shell

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // ── 骨架结构尺寸（AGENTS.md §9.2：令牌未覆盖的骨架尺寸以命名常量集中声明）──
    readonly property int statusBarH: 32      // 底部状态条高
    readonly property int toastBottomMargin: 28  // Toast 距底（横屏）
    readonly property int navBarH: 80         // 竖屏底部导航高（MD3 bottom navigation）

    // ── 竖横屏自适应（2026-10-22 用户点单）─────────────────────────────
    // 判定用**窗口朝向**（宽<高 = 竖屏），不用设备类型：
    //   · 桌面窗拉高成竖条形也进竖屏布局（合理：窄窗放 240 侧栏太挤）；
    //   · 手机旋转时 QML 绑定自动重算，无需重启；
    //   · 两种模式共用 navItems / 业务页 / 数据真源，只换导航位置与网格列数。
    readonly property bool portrait: shell.width < shell.height

    // ── 侧栏菜单（文案走教育 B 端规范）────────────────────────────────
    // icon 用 lucide 图标名（随 exe 打包），不用 emoji —— emoji 在本机会渲染成方块。
    // 2026-10-22：新增「运行监控」——它不是本页的一个 section，而是**跳旧壳 page 3**
    //   （监控页是整页大画面 + 单机帧流，一次性重写风险大且收益低，先做出口）。
    readonly property var navItems: [
        { key: "dash",     icon: "monitor",     label: "仪表盘" },
        { key: "classes",  icon: "users",       label: "班级看板" },
        { key: "monitor",  icon: "eye",          label: "运行监控" },
        { key: "settings", icon: "settings",    label: "系统设置" }
    ]

    /** 当前栏目：dash / classes / settings（monitor 是跳转项，不驻留）。 */
    property string section: "dash"

    /** 侧滑面板展示哪台设备（"" = 关闭）。 */
    property string openUid: ""

    /** S5 设备筛选：all / online / offline。 */
    property string filterMode: "all"

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

    /** S1：计数（读 backend.storage 的待办；未拉到前显示 ——，不假装是 0）。 */
    function stats() {
        const ds = shell.classDevices()
        var on = 0
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) on++
        const st = (backend && backend.storage) ? backend.storage : ({})
        const pend = (st.pendingInstructions === undefined) ? "—" : st.pendingInstructions
        return { total: ds.length, on: on, off: ds.length - on, pending: pend }
    }

    /** 当前栏目在 StackLayout 里的下标。 */
    readonly property int sectionIndex: shell.section === "settings" ? 2
                                      : (shell.section === "classes" ? 1 : 0)

    /** 导航激活（Sidebar 与 BottomNav 共用，别处不许再写一份分支逻辑）：
     *   运行监控 = 跳旧壳 page 3（整页大画面），不是本页 section；
     *   系统设置先过 RBAC 门禁（H5：受限身份不切栏、弹页级提示）。 */
    function navActivated(key) {
        if (key === "monitor") {
            if (shell.ctx) shell.ctx.window.goPage(3)
            return
        }
        if (key === "settings" && (typeof perms === "undefined" || !perms.can("system:settings"))) {
            shell.toast("当前身份不能进系统设置", true)
            return
        }
        shell.section = key
    }

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
     *    单台 id（lock/reboot）在 runActionById 的 danger 分支只看 backend.currentUid，
     *    批量会被「先在控制页选中一台设备」拦截，或只锁那一台。 */
    function runBulk(actionId) {
        if (!shell.ctx) return
        const n = shell.pickAllOnline()
        if (n === 0) { shell.toast("当前班没有在线设备，无法批量下发", true); return }
        const label = (actionId === "pick-lock") ? "锁定桌面" : "远程重启"
        shell.ctx.window.runActionById(actionId)
        shell.toast("已对 " + n + " 台在线设备下发「" + label + "」")
    }

    // ── H3 页级 Toast（骨架页自绘，不依赖灵动岛）─────────────────────
    // ⚠️ 灵动岛浮层在 page 6 被隐藏（Main.qml:1299），root.toast() 走 bridge.showNote
    //    → 灵动岛，这个页面上根本看不见。本地动作反馈必须自绘。
    // 配色走令牌：常规 = inverseSurface/inverseOn（MD3 Snackbar 口径，2026-10-22 前是
    // 硬编码 #322F35/#FFFFFF）；错误 = error 底 + 白字。
    property bool toastErr: false
    function toast(msg, isErr) {
        if (!msg) return
        toastText.text = msg
        shell.toastErr = (isErr === true)
        toastBox.color = shell.toastErr
                         ? (shell.th ? shell.th.cError : "#B3261E")
                         : (shell.th ? shell.th.cInverseSurf : "#313033")
        toastLayer.visible = true
        toastHide.restart()
    }

    // ── H4 状态条文案（现算：设备总数 / 在线数 + 当前班 + 正在查看设备）──
    function _statusText() {
        const s = shell.stats()
        let cls = "全部班级"
        if (backend.currentClass) {
            cls = backend.currentClass
            const cs = (backend.classes) ? backend.classes : []
            for (var c = 0; c < cs.length; ++c)
                if (cs[c] && cs[c].code === backend.currentClass) { cls = cs[c].name || cs[c].code; break }
        }
        var txt = "班级 " + cls + " · " + s.total + " 台 · " + s.on + " 台在线"
        if (shell.openUid !== "") {
            const ds = shell.classDevices()
            for (var j = 0; j < ds.length; ++j)
                if (ds[j].uid === shell.openUid) { txt += " · 正在查看 " + (ds[j].name || ds[j].uid); break }
        }
        return txt
    }

    Rectangle { anchors.fill: parent; color: shell.th ? shell.th.cSurface : "#FFFBFE" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ══ 左侧班级导航（240px · #473 点单①：左班级导航 + 右班级工作台）══
            // 与 Sidebar 的分工：Sidebar 是功能菜单（竖屏 BottomNav 仍用它那份 navItems），
            // ClassNav 是"以班为单位"的入口——班级列表在上（人数一眼可见），系统组在下。
            // 竖屏让位给 BottomNav（visible:false 的项不占布局空间）。
            ClassNav {
                id: nav
                visible: !shell.portrait
                md3: shell.th
                ctx: shell.ctx
                classes: backend ? backend.classes : []
                devices: backend ? backend.devices : []
                currentClass: backend ? backend.currentClass : ""
                section: shell.section
                onClassPicked: function (code) {
                    // 点班级 = 双写（旧筛选 + 新班级域）并切到工作台段
                    shell.section = "classes"
                    classDash.activateClass(code)
                }
                onSystemActivated: function (key) { shell.navActivated(key) }
                onClassicRequested: if (shell.ctx) shell.ctx.window.goPage(1)
            }

            // ══ 主内容区（Surface）══
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    id: mainCol
                    // 内容区边缘内边距 = spacing.lg(24)（AGENTS.md §9.2）
                    anchors.leftMargin: shell.th ? shell.th.spL : 24
                    anchors.rightMargin: shell.th ? shell.th.spL : 24
                    spacing: 0

                    // ── 顶部导航栏（64px）──
                    TopBar {
                        id: topBar
                        ctx: shell.ctx
                        md3: shell.th          // 颜色令牌（TopBar 默认 md3=null，内部有兜底）
                        title: "设备监控"
                        // ⚠️ 班级列表与当前班**不在这里传参** —— TopBar 内部直接读
                        //    backend.classes / backend.currentClass。曾经多传不存在的属性，
                        //    结果 `Type Md3Shell unavailable` 整页起不来（qmllint 0 Error）。
                        // 2026-10-23 #472：班级选择改为**双写**——activateClass 同时
                        // 写旧筛选（setCurrentClass）与新班级域（subscribeClass）。
                        // 只写旧的话，班级工作台永远订不到班（网格空着不说话）。
                        onClassPicked: function (code) { classDash.activateClass(code) }
                        onAvatarClicked: function () { if (shell.ctx) shell.ctx.window.goPage(2) }
                        onLoginRequested: {
                            shell.toast("正在打开站点登录…")
                            if (backend) backend.loginWithSite()
                        }
                    }

                    // ── 操作区（距顶部 spacing.lg=24）──
                    ActionBar {
                        Layout.fillWidth: true
                        Layout.topMargin: shell.th ? shell.th.spL : 24
                        ctx: shell.ctx
                        portrait: shell.portrait     // 竖屏：多一个「返回经典界面」出口
                        onLockRequested: shell.runBulk("pick-lock")
                        onRebootRequested: shell.runBulk("pick-shutdown")
                        onClassicRequested: {
                            // H7：竖屏侧栏被底栏取代，出口随 ActionBar 可见
                            // （横屏该出口在 Sidebar 底部，两条路都通向 page 1）。
                            if (shell.ctx) shell.ctx.window.goPage(1)
                        }
                    }

                    // ── 内容区（距操作区 spacing.md=16）──
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: shell.th ? shell.th.spM : 16
                        Layout.bottomMargin: shell.th ? shell.th.spM : 16
                        currentIndex: shell.sectionIndex

                        // ① 仪表盘（S1/S2/S3/S4/S5/S7 全在 pages/DashPage.qml）
                        // 抽件原因：本文件已到登记上限（710 行），内联 270 行不允许再长。
                        DashPage {
                            md3: shell.th
                            host: shell.ctx ? shell.ctx.window : null
                            devices: shell.classDevices()
                            stats: shell.stats()
                            portrait: shell.portrait
                            filterMode: shell.filterMode
                            openUid: shell.openUid
                            toastFn: function (msg, isErr) { shell.toast(msg, isErr) }
                            onFilterChanged: function (m) { shell.filterMode = m }
                            onDeviceActivated: function (uid) {
                                shell.openUid = uid
                                // H1：同时写宿主「看哪台」真源（SideSheet 与危险动作都取它）
                                if (uid !== "" && backend) backend.currentUid = uid
                            }
                        }

                        // ② 班级工作台（#461-#472）：左身份 / 中设备网格+展板 / 右动作+Timeline
                        // 数据走班级域 backend.classService（#460 typed 服务）。
                        // 旧「班级看板」页（AdminDashboard.qml）文件保留，page 4 仍用它。
                        ClassDashboard {
                            id: classDash
                            md3: shell.th
                            host: shell.ctx ? shell.ctx.window : null
                            cs: backend ? backend.classService : null
                            devices: backend ? backend.devices : []
                            toastFn: function (msg, isErr) { shell.toast(msg, isErr) }
                        }

                        // ③ 系统设置：复用已交付的设置页（零重复）
                        Loader {
                            source: "../pages/SettingsPage.qml"
                            onLoaded: if (item) item.ctx = shell.ctx
                        }
                    }
                }
            }
        }

        // ══ 竖屏：底部导航（与 Sidebar 同一份 navItems 的横排形态）══
        // 放在状态条**上方**：状态条是全局信息带，朝向无关，两种模式都在最底。
        BottomNav {
            Layout.fillWidth: true
            Layout.preferredHeight: shell.navBarH
            visible: shell.portrait
            md3: shell.th
            items: shell.navItems
            current: shell.section
            onActivated: function (key) { shell.navActivated(key) }
        }

        // ── H4 状态条（全宽贴底）──────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: shell.statusBarH
            color: shell.th ? shell.th.cSurfaceVar : "#E7E0EC"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: shell.th ? shell.th.spM : 16
                anchors.rightMargin: shell.th ? shell.th.spM : 16
                spacing: shell.th ? shell.th.spM : 16
                Text {
                    Layout.fillWidth: true
                    text: shell._statusText()
                    font.pixelSize: shell.th ? shell.th.bodyS : 12
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
            // 竖屏：抬到底部导航 + 状态条上方（否则糊在底栏上看不清）
            anchors.bottomMargin: shell.portrait
                                    ? (shell.navBarH + shell.statusBarH + 12)
                                    : shell.toastBottomMargin
            radius: shell.th ? shell.th.rS : 8
            color: shell.th ? shell.th.cInverseSurf : "#313033"
            Text {
                id: toastText
                anchors.centerIn: parent
                text: ""
                font.pixelSize: shell.th ? shell.th.body13 : 13
                color: shell.toastErr ? "#FFFFFF"
                                       : (shell.th ? shell.th.cInverseOn : "#F4EFF4")
                padding: shell.th ? shell.th.spS + shell.th.spXS : 12
            }
        }
        Timer {
            id: toastHide
            interval: 2600
            onTriggered: toastLayer.visible = false
        }
    }

    // ── 帧订阅：进页面订本班前 N 台在线设备，离开时退订 ────────────────
    // ⚠️ 为什么必须退订：云端按订阅推帧，不退订 = 后台一直拉流。
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

    Component.onCompleted: {
        shell.syncThumbs()
        // S3：进页面拉一次排行（云端 instructions 近 7 天；失败静默，界面自己会显示空态）
        if (backend) backend.fetchActionRanking()
    }
    Component.onDestruction: backend.subscribeThumbnails([])
}
