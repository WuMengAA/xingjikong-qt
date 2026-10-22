// 星集控管理端 · MD3 主界面（2026-10-22 起为**默认首页**）
//
// 定位变更（用户 2026-10-22 点单「把新界面全面替换老界面」）：
//   本页原先只是 page=6 的"骨架预览"，现在升为**默认入口**（Main.qml 里 page 初值 6）。
//   旧壳（page 1 集控 / 2 设置 / 3 监控 / 4 看板 / 5 班级详情）**不删除**，改为
//   「新壳导航 + 旧页下钻」：能复用的一律 Loader 复用，做不了的给明确出口，
//   绝不另写第二份实现（与「不许在烂地基上叠补丁」同一条纪律：先立新壳，再逐步消化旧页）。
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
import "../components/md3"
import "../shell"          // Sidebar / TopBar / ActionBar / DeviceCard / SideSheet
import "../island"         // SvgIcon

Item {
    id: shell

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

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

    /** S5：在 classDevices() 之上再按 filterMode 过滤（本页自有筛选，不碰宿主那份）。 */
    function visibleDevices() {
        const ds = shell.classDevices()
        if (shell.filterMode === "all") return ds
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            const on = ds[i].online !== false
            if ((shell.filterMode === "online") === on) out.push(ds[i])
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

    /** S7：内容区宽度决定列数（窄窗不再挤成 4 列 100px 卡片）。 */
    function gridColumns(w) {
        if (w >= 900) return 4
        if (w >= 660) return 3
        return 2
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
    function toast(msg, isErr) {
        if (!msg) return
        toastText.text = msg
        toastBox.color = isErr ? (shell.th ? shell.th.cError : "#B3261E") : "#322F35"
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

    /** 动作用中文名（复用宿主的映射表，不另建一份）。 */
    function actionLabel(a) {
        if (shell.ctx && shell.ctx.window && shell.ctx.window.actionTitleOf) {
            const t = shell.ctx.window.actionTitleOf(a)
            if (t && t !== "") return t
        }
        return a || ""
    }

    /** S4：宿主最近回执（真回执流水，主界面 root.results）。 */
    function recentResults() {
        if (shell.ctx && shell.ctx.window && shell.ctx.window.results) return shell.ctx.window.results
        return []
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
                onActivated: function (key) {
                    // 运行监控 = 跳旧壳 page 3（整页大画面），不是本页 section
                    if (key === "monitor") {
                        if (shell.ctx) shell.ctx.window.goPage(3)
                        return
                    }
                    // H5：点「系统设置」先过 RBAC 门禁 —— 受限身份不切栏、弹页级提示。
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
                    id: mainCol
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
                        //    backend.classes / backend.currentClass。曾经多传不存在的属性，
                        //    结果 `Type Md3Shell unavailable` 整页起不来（qmllint 0 Error）。
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

                        // ① 仪表盘（S1/S2/S3/S4/S5/S7 全在这里）
                        Flickable {
                            id: dash
                            contentWidth: width          // 纪律：显式声明，否则列宽算不出来
                            contentHeight: dashCol.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            ColumnLayout {
                                id: dashCol
                                width: dash.width
                                spacing: 16

                                // ── S1 三张统计卡 ──
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    Repeater {
                                        model: [
                                            { k: "在线设备", v: String(shell.stats().on),  sub: "共 " + shell.stats().total + " 台" },
                                            { k: "待执行指令", v: String(shell.stats().pending), sub: "云端队列" },
                                            { k: "离线设备", v: String(shell.stats().off), sub: "需排查" }
                                        ]
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 96
                                            radius: 16
                                            color: shell.th ? shell.th.cContLow : "#F7F2FA"
                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 20
                                                spacing: 4
                                                Text {
                                                    text: modelData.k
                                                    font.pixelSize: 12
                                                    color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                                                }
                                                Text {
                                                    text: modelData.v
                                                    font.pixelSize: 28
                                                    font.weight: Font.Medium
                                                    color: shell.th ? shell.th.cOnSurface : "#1D1B20"
                                                }
                                                Text {
                                                    text: modelData.sub
                                                    font.pixelSize: 12
                                                    color: shell.th ? shell.th.cOutline : "#79747E"
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── S5 筛选 chip ──
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: [
                                            { k: "all",    t: "全部" },
                                            { k: "online", t: "在线" },
                                            { k: "offline", t: "离线" }
                                        ]
                                        delegate: Rectangle {
                                            required property var modelData
                                            readonly property bool on: shell.filterMode === modelData.k
                                            height: 32
                                            width: chipTxt.implicitWidth + 28
                                            radius: 16
                                            color: on ? (shell.th ? shell.th.cSecondaryCont : "#E8DEF8")
                                                      : "transparent"
                                            border.width: on ? 0 : 1
                                            border.color: shell.th ? shell.th.cOutlineVar : "#CAC4D0"
                                            Text {
                                                id: chipTxt
                                                anchors.centerIn: parent
                                                text: modelData.t
                                                font.pixelSize: 13
                                                color: on ? (shell.th ? shell.th.cOnSecondaryCont : "#1D192B")
                                                          : (shell.th ? shell.th.cOnSurfaceVar : "#49454F")
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: shell.filterMode = modelData.k
                                            }
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: shell.visibleDevices().length + " 台"
                                        font.pixelSize: 12
                                        color: shell.th ? shell.th.cOutline : "#79747E"
                                    }
                                }

                                // ── 设备网格（S7 响应式列数）──
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: shell.gridColumns(dashCol.width)
                                    columnSpacing: 16
                                    rowSpacing: 16

                                    Repeater {
                                        model: shell.visibleDevices()
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
                                                // 与危险动作的目标都从 backend.currentUid 取。
                                                if (uid !== "" && backend) backend.currentUid = uid
                                            }
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: shell.visibleDevices().length === 0
                                    text: shell.filterMode === "all"
                                          ? "这个班还没有设备"
                                          : ("当前筛选（" + (shell.filterMode === "online" ? "在线" : "离线")
                                             + "）下没有设备")
                                    font.pixelSize: 12
                                    color: shell.th ? shell.th.cOutline : "#79747E"
                                }

                                // ── S3 动作排行 + S2 存储概况 + S4 最近回执 ──
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: 8
                                    spacing: 16

                                    // S3 动作排行
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 208
                                        radius: 16
                                        color: shell.th ? shell.th.cContLow : "#F7F2FA"
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 20
                                            spacing: 8
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Text {
                                                    text: "动作排行 · 近 7 天"
                                                    font.pixelSize: 14
                                                    font.weight: Font.Medium
                                                    color: shell.th ? shell.th.cOnSurface : "#1D1B20"
                                                }
                                                Item { Layout.fillWidth: true }
                                                Text {
                                                    text: "刷新"
                                                    font.pixelSize: 12
                                                    color: shell.th ? shell.th.cPrimary : "#6750A4"
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            backend.fetchActionRanking()
                                                            shell.toast("已刷新动作排行")
                                                        }
                                                    }
                                                }
                                            }
                                            Repeater {
                                                model: backend.actionRanking ? backend.actionRanking.slice(0, 5) : []
                                                delegate: RowLayout {
                                                    required property var modelData
                                                    // ⚠️ 不用 `required property int index`：Repeater 的
                                                    //    index 是上下文属性，声明成 required 在部分版本会拿不到值。
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    Text {
                                                        text: (index + 1)
                                                        font.pixelSize: 12
                                                        font.weight: index < 3 ? Font.Bold : Font.Normal
                                                        color: index < 3 ? (shell.th ? shell.th.cPrimary : "#6750A4")
                                                                         : (shell.th ? shell.th.cOnSurfaceVar : "#49454F")
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        elide: Text.ElideRight
                                                        text: shell.actionLabel(modelData.action)
                                                        font.pixelSize: 12
                                                        color: shell.th ? shell.th.cOnSurface : "#1D1B20"
                                                    }
                                                    Text {
                                                        text: "成功 " + modelData.ok + " / " + modelData.total
                                                        font.pixelSize: 11
                                                        color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                                                    }
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: !backend.actionRanking || backend.actionRanking.length === 0
                                                text: "还没有排行数据（连上云端后自动统计）"
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                                color: shell.th ? shell.th.cOutline : "#79747E"
                                            }
                                            Item { Layout.fillHeight: true }
                                        }
                                    }

                                    // S2 存储概况 + S4 最近回执
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 208
                                        radius: 16
                                        color: shell.th ? shell.th.cContLow : "#F7F2FA"
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 20
                                            spacing: 6
                                            Text {
                                                text: "云端存储概况"
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: shell.th ? shell.th.cOnSurface : "#1D1B20"
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: "已知设备 " + (backend.storage.knownDevices !== undefined ? backend.storage.knownDevices : "—")
                                                      + " · 班级 " + (backend.storage.classes !== undefined ? backend.storage.classes : "—")
                                                      + " · 事件文件 " + (backend.storage.eventsFiles !== undefined ? backend.storage.eventsFiles : "—")
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                                color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: "最近回执"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: shell.th ? shell.th.cOnSurface : "#1D1B20"
                                                topPadding: 4
                                            }
                                            Repeater {
                                                model: shell.recentResults().slice(0, 4)
                                                delegate: RowLayout {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    Text {
                                                        text: modelData.t || ""
                                                        font.pixelSize: 11
                                                        color: shell.th ? shell.th.cOutline : "#79747E"
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        elide: Text.ElideRight
                                                        text: shell.actionLabel(modelData.action) + " · " + (modelData.uid || "")
                                                        font.pixelSize: 11
                                                        color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                                                    }
                                                    Text {
                                                        text: modelData.ok ? "成功" : "失败"
                                                        font.pixelSize: 11
                                                        color: modelData.ok
                                                               ? (shell.th ? shell.th.cPrimary : "#6750A4")
                                                               : (shell.th ? shell.th.cError : "#B3261E")
                                                    }
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: shell.recentResults().length === 0
                                                text: "还没有回执（下发动作后这里会显示执行结果）"
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                                color: shell.th ? shell.th.cOutline : "#79747E"
                                            }
                                            Item { Layout.fillHeight: true }
                                        }
                                    }
                                }
                            }
                        }

                        // ② 班级看板：复用已交付的看板页（零重复，行为与 page 4 一致）
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
