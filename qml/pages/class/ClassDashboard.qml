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
    /** 中栏当前段：devices / board。 */
    property string boardTab: "devices"
    /** 当前选中设备（UI 态；真执行目标看 host.pickDevice 双写后的 currentUid）。 */
    property string selectedUid: ""

    readonly property bool portrait: dash.width < dash.height
    readonly property var devModel: (dash.cs && dash.cs.deviceModel) ? dash.cs.deviceModel : null
    readonly property var evModel: (dash.cs && dash.cs.deviceModel) ? dash.cs.eventModel : null

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
        dash.pending = true
        if (typeof backend !== "undefined" && backend) backend.setCurrentClass(code)
        if (dash.cs) dash.cs.subscribeClass(code)
    }

    /** 点格子：走宿主唯一选择路径（currentUid/picked 双写）。 */
    function onTileClicked(uid) {
        dash.selectedUid = uid
        if (dash.host) dash.host.pickDevice(uid, 0, 0)
    }

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

    // ══ 横屏：三栏并列 ══
    Row {
        anchors.fill: parent
        visible: !dash.portrait
        spacing: dash.md3 ? dash.md3.spM : 16

        // 左：身份与订阅态
        ClassIdentityPane {
            id: idPaneL
            width: 260
            height: parent.height
            md3: dash.md3
            cs: dash.cs
            pending: dash.pending
            onUnsubscribeRequested: if (dash.cs) dash.cs.unsubscribeClass()
        }

        // 中：设备网格 / 班级展板 / 成员管理（分段切换，横竖屏共用 ClassMiddlePane）
        ClassMiddlePane {
            width: Math.max(320, dash.width - 260 - 320 - (dash.md3 ? dash.md3.spM : 16) * 2)
            height: parent.height
            md3: dash.md3
            deviceModel: dash.devModel
            subscribed: (dash.cs && dash.cs.subscribed === true)
            selectedUid: dash.selectedUid
            tab: dash.boardTab
            classes: (typeof backend !== "undefined" && backend) ? backend.classes : []
            classCode: (dash.cs && dash.cs.classCode) ? dash.cs.classCode : ""
            onTabActivated: function (v) { dash.boardTab = v }
            onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
        }

        // 右：管理 + 动作 + 时间线
        Item {
            width: 320
            height: parent.height

            Column {
                anchors.fill: parent
                spacing: 16

                // 班级管理（2026-10-24 方案 B）：建/改名/删。管"班"本身；
                // 成员（设备去留）走中栏「成员管理」tab。两处不混。
                ClassAdminPane {
                    width: parent.width
                    md3: dash.md3
                    cs: dash.cs
                    toastFn: dash.toastFn
                }
                ClassActionPane {
                    width: parent.width
                    md3: dash.md3
                    host: dash.host
                    cs: dash.cs
                    devices: dash.devices
                    selectedUid: dash.selectedUid
                    selectedName: dash.selectedName()
                    toastFn: dash.toastFn
                    openControlFn: dash.openControlFn
                    onPublishRequested: publishDlg.open()
                }
                // 课堂工具（2026-10-25 教室小工具第一刀）：对全班一键的音量档位 /
                // 专注 / 噪音监测。独立面板而不是塞进 ClassActionPane —— 那个已顶
                // 200 行红线，且语义不同（那边是电源类危险动作，这边是教学常用）。
                ClassToolPane {
                    width: parent.width
                    md3: dash.md3
                    host: dash.host
                    cs: dash.cs
                    devices: dash.devices
                    toastFn: dash.toastFn
                }
                ClassTimelinePane {
                    width: parent.width
                    height: Math.max(120, parent.height - 220)
                    md3: dash.md3
                    model: dash.evModel
                }
            }
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
        selectedUid: dash.selectedUid
        selectedName: dash.selectedName()
        toastFn: dash.toastFn
        openControlFn: dash.openControlFn
        onTabActivated: function (v) { dash.boardTab = v }
        onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
        onUnsubscribeRequested: if (dash.cs) dash.cs.unsubscribeClass()
        onPublishRequested: publishDlg.open()
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
}
