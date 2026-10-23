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

        // 中：设备网格 / 班级展板（分段切换，横竖屏共用 ClassMiddlePane）
        ClassMiddlePane {
            width: Math.max(320, dash.width - 260 - 320 - (dash.md3 ? dash.md3.spM : 16) * 2)
            height: parent.height
            md3: dash.md3
            deviceModel: dash.devModel
            subscribed: (dash.cs && dash.cs.subscribed === true)
            selectedUid: dash.selectedUid
            tab: dash.boardTab
            onTabActivated: function (v) { dash.boardTab = v }
            onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
        }

        // 右：动作 + 时间线
        Item {
            width: 320
            height: parent.height

            Column {
                anchors.fill: parent
                spacing: 16

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

    // ══ 竖屏：竖排（身份 → 中栏 → 动作 → 时间线，中段以下可滚动）══
    Column {
        anchors.fill: parent
        visible: dash.portrait
        spacing: 12

        ClassIdentityPane {
            width: parent.width
            md3: dash.md3
            cs: dash.cs
            pending: dash.pending
            onUnsubscribeRequested: if (dash.cs) dash.cs.unsubscribeClass()
        }

        Flickable {
            width: parent.width
            height: parent.height - 200
            contentHeight: pCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: pCol
                width: parent.width
                spacing: 12

                ClassMiddlePane {
                    width: parent.width
                    height: 260
                    md3: dash.md3
                    deviceModel: dash.devModel
                    subscribed: (dash.cs && dash.cs.subscribed === true)
                    selectedUid: dash.selectedUid
                    tab: dash.boardTab
                    onTabActivated: function (v) { dash.boardTab = v }
                    onDeviceClicked: function (uid) { dash.onTileClicked(uid) }
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
                }
                ClassTimelinePane {
                    width: parent.width
                    height: 180
                    md3: dash.md3
                    model: dash.evModel
                }
            }
        }
    }
}
