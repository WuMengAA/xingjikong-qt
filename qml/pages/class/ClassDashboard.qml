// 班级工作台 · 三栏编排器（#461-#472）
// 点单项②：三栏 = 左身份 / 中设备网格+展板 / 右动作+Timeline。本文件只做编排与
// 状态接线，可见内容全在同目录窗格件里（各守 200 行，AGENTS.md §10）。
// ── 状态真源（不自己另存一份）──
//   · 订阅态：backend.classService（#460）—— subscribed/classCode/className/
//     onlineCount/deviceCount/lastError + 两个 typed model；
//   · 班级选择**双写**：setCurrentClass（旧筛选，TopBar/仪表盘/集控页消费）+
//     subscribeClass（新班级域），两套机制并存到集控页迁移完成；
//   · 选中设备：host.pickDevice(uid,0,0)（宿主唯一选择路径，currentUid + picked
//     双写；危险动作由宿主按 currentUid 快照下发，不会发错机器）。
// ── pending（订阅中）── 本地过渡态：activateClass 置 true；subscribedChanged /
//   classError / subscribeFailed 任一到达置 false（不探 m_pendingCode，用信号收口）。
// ── 竖屏（宽<高）── 三栏竖排：身份 → 中栏 → 动作 → 时间线，中段以下进 Flickable。
// ── 去集控页 ── openControlFn 由外壳注入（切 control section）；未注入回落旧壳 goPage(1)。
// ── 展板全屏 ── boardMode ⇒ 隐藏左右栏、中栏独占（入口在展板顶栏按钮）。
import QtQuick

Item {
    id: dash

    property var md3: ({})
    property var host: null      // ctx.window
    property var cs: null        // ClassService
    property var devices: []     // backend.devices（动作面板算本班在线名单用）
    property var toastFn: null   // (msg, isErr) 外壳页级 toast
    property var openControlFn: null   // 去集控页（外壳注入 section 切换）

    /** 订阅请求在飞（本地过渡态，见文件头）。 */
    property bool pending: false
    property string boardTab: "devices"    // devices / students / board / members
    property bool boardMode: false          // 展板页内全屏（隐藏左右栏，中栏独占）
    /** 当前选中设备（UI 态；真执行目标看 host.pickDevice 双写后的 currentUid）。 */
    property string selectedUid: ""

    /** 竖屏（宽<高）或**宽度不够三栏**时走竖排栈。
     *  2026-10-25 修「右栏被裁、按钮按不动」：原判据只看宽高比 —— 1180×700 的默认窗口
     *  是横屏，但内容区只有 892 逻辑宽，三栏硬性最小 260+320+320+32=932 ⇒ 溢出 40，
     *  右栏 320 被推出窗口右缘（用户实测：窗口 1201 逻辑宽时右栏起点 1151，只露 50px）。
     *  现在加宽度阈值：内容区 < 860 直接切竖排栈（可滚动），不再硬挤三栏。 */
    readonly property bool narrow: dash.width < 860
    readonly property bool portrait: dash.width < dash.height || dash.narrow
    readonly property var devModel: (dash.cs && dash.cs.deviceModel) ? dash.cs.deviceModel : null
    readonly property var evModel: (dash.cs && dash.cs.deviceModel) ? dash.cs.eventModel : null

    // 溢出内容不许画到页面外（QML 默认不裁剪 → 会盖住邻栏、吞掉点击）
    clip: true

    /** 选中设备名：从 backend.devices 现查（云端名单口径），查不到回落 uid。 */
    function selectedName() {
        const ds = dash.devices
        for (var i = 0; i < ds.length; ++i) {
            if (ds[i] && ds[i].uid === dash.selectedUid)
                return ds[i].name || dash.selectedUid
        }
        return dash.selectedUid
    }

    /** 班级选择（ClassNav / TopBar 下拉都汇到这里）：双写 + 清选中 + 置 pending。 */
    function activateClass(code) {
        dash.selectedUid = ""
        dash.boardMode = false          // 换班自动退出展板全屏，别让新班停在黑乎乎的展示态
        dash.pending = true
        if (typeof backend !== "undefined" && backend) backend.setCurrentClass(code)
        if (dash.cs) dash.cs.subscribeClass(code)
    }

    /** 点格子：走宿主唯一选择路径（currentUid/picked 双写）。 */
    function onTileClicked(uid) {
        dash.selectedUid = uid
        if (dash.host) dash.host.pickDevice(uid, 0, 0)
    }

    /** 新建班级弹窗（左栏空态「＋ 新建班级」/ 右栏「上手三步」同一入口）。 */
    function openCreateDialog() { createDlg.open() }

    /** pending 收口：订阅结果任一通道到达即完事。错误内容由左栏内联展示
     *  （ClassService.onError 已 setError ⇒ lastError 非空），这里不再弹
     *  一次 toast —— 同一句话显示两遍是噪音，不是反馈。 */
    Connections {
        target: dash.cs
        function onSubscribedChanged() { dash.pending = false }
        function onClassError(msg) { dash.pending = false }
        function onSubscribeFailed(reason) { dash.pending = false }
    }

    // 恢复上次上下文：外壳加载时 currentClass 非空 ⇒ 自动重订
    // （ClassService.subscribeClass 幂等：已订同班/在飞同班都不重发）。
    Component.onCompleted: {
        if (typeof backend !== "undefined" && backend && backend.currentClass && dash.cs) {
            dash.pending = true
            dash.cs.subscribeClass(backend.currentClass)
        }
    }

    // ══ 横屏：三栏并列（展板全屏时隐藏左右栏）══
    Row {
        anchors.fill: parent
        visible: !dash.portrait
        spacing: dash.boardMode ? 0 : (dash.md3 ? dash.md3.spM : 16)

        // 左：身份与订阅态（260→240：给中栏让 20px，见 narrow 注释）
        ClassIdentityPane {
            id: idPaneL
            visible: !dash.boardMode
            width: dash.boardMode ? 0 : 240
            height: parent.height
            md3: dash.md3
            cs: dash.cs
            pending: dash.pending
            onUnsubscribeRequested: if (dash.cs) dash.cs.unsubscribeClass()
        }

        // 中：设备网格 / 班级展板 / 学生 / 成员（分段切换，横竖屏共用 ClassMiddlePane）
        ClassMiddlePane {
            id: midPane
            width: dash.boardMode
                   ? dash.width
                   : Math.max(240, dash.width - 240 - 300 - (dash.md3 ? dash.md3.spM : 16) * 2)
            height: parent.height
            md3: dash.md3
            deviceModel: dash.devModel
            evModel: dash.evModel
            subscribed: (dash.cs && dash.cs.subscribed === true)
            selectedUid: dash.selectedUid
            tab: dash.boardTab
            classes: (typeof backend !== "undefined" && backend) ? backend.classes : []
            classCode: (dash.cs && dash.cs.classCode) ? dash.cs.classCode : ""
            className: (dash.cs && dash.cs.className) ? dash.cs.className : ""
            boardExpanded: dash.boardMode
            onTabActivated: function (v) { dash.boardTab = v }
            onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
            onBoardExpandToggled: dash.boardMode = !dash.boardMode
        }

        // 右：管理 + 本班设备管控 + 课堂工具 + 时间线（2026-10-25 抽件 ClassRightRail，
        // 起因是本文件撞 200 行红线；班级页保留设备管控面板，与设备集控页共用动作、
        // 仅作用域限定本班，不做去集控化）
        ClassRightRail {
            visible: !dash.boardMode
            width: dash.boardMode ? 0 : 300
            height: parent.height
            md3: dash.md3
            cs: dash.cs
            host: dash.host
            devices: dash.devices
            evModel: dash.evModel
            selectedUid: dash.selectedUid
            selectedName: dash.selectedName()
            toastFn: dash.toastFn
            openControlFn: dash.openControlFn
            classCount: (typeof backend !== "undefined" && backend && backend.classes) ? backend.classes.length : 0
            onPublishRequested: publishDlg.open()
            onCreateRequested: dash.openCreateDialog()
        }
    }

    // ══ 竖屏：竖排栈（抽件 ClassPortraitStack：身份 → 中栏 → 动作 → 时间线）══
    ClassPortraitStack {
        anchors.fill: parent
        visible: dash.portrait
        md3: dash.md3
        cs: dash.cs
        host: dash.host
        devices: dash.devices
        devModel: dash.devModel
        evModel: dash.evModel
        pending: dash.pending
        boardTab: dash.boardTab
        boardExpanded: dash.boardMode
        className: (dash.cs && dash.cs.className) ? dash.cs.className : ""
        selectedUid: dash.selectedUid
        selectedName: dash.selectedName()
        toastFn: dash.toastFn
        openControlFn: dash.openControlFn
        classCount: (typeof backend !== "undefined" && backend && backend.classes) ? backend.classes.length : 0
        onTabActivated: function (v) { dash.boardTab = v }
        onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
        onBoardExpandToggled: dash.boardMode = !dash.boardMode
        onUnsubscribeRequested: if (dash.cs) dash.cs.unsubscribeClass()
        onPublishRequested: publishDlg.open()
        onCreateRequested: dash.openCreateDialog()
    }

    // ══ 发布中心（P1）：页级模态 ══
    // 刻意放在 dash 末尾而不是右栏内：右栏只有 320 宽且竖屏那只还在 Flickable 里，
    // 模态塞进去会被裁切/跟着滚。盖整页才是对的，入口仍在右栏「班级管理」。
    ClassPublishDialog {
        id: publishDlg
        md3: dash.md3
        cs: dash.cs
        toastFn: dash.toastFn
    }

    // ══ 新建班级（2026-10-25）：页级模态，与发布中心同层 ══
    ClassCreateDialog {
        id: createDlg
        md3: dash.md3
        toastFn: dash.toastFn
    }
}
