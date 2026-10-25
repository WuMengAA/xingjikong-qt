// 班级工作台 · 竖屏竖排栈（ClassDashboard 抽件 · 守 200 行）
//
// 竖屏（宽<高）布局：引导 → 身份 → 中栏（设备网格/展板/成员）→ 管理 → 动作 → 时间线，
// 中段以下进 Flickable（小屏放不下，必须可滚）。与横屏三栏是同一批窗格件、
// 同一份接线，只是排列方式不同 —— 编排真源仍在 ClassDashboard。
//
// 2026-10-25：① 顶部加「上手引导」（未订阅时）；② 展板全屏（boardExpanded）时
// 只留中栏独占整屏，其余面板隐藏 —— 与横屏"隐藏左右栏"是同一个语义。
import QtQuick

Column {
    id: stack

    property var cs: null            // ClassService
    property var host: null          // ctx.window
    property var devices: []         // backend.devices
    property var devModel: null      // cs.deviceModel
    property var evModel: null       // cs.eventModel
    property bool pending: false
    property string boardTab: "devices"
    property bool boardExpanded: false
    property string className: ""
    property string selectedUid: ""
    property string selectedName: ""
    property var toastFn: null
    property var openControlFn: null
    property int classCount: 0

    signal tabActivated(string v)
    signal deviceClicked(string uid)
    signal unsubscribeRequested()
    /** 发布中心（P1）：由窗格发起，向上冒到 ClassDashboard 打开页级模态。 */
    signal publishRequested()
    /** 展板全屏切换（向上冒到 ClassDashboard 收口）。 */
    signal boardExpandToggled()
    /** 上手引导的「新建班级」→ 宿主开 ClassCreateDialog。 */
    signal createRequested()
    /** 学生行「编辑」→ 宿主开页级编辑弹窗（竖屏同样走页级，避免被中段裁掉）。 */
    signal editStudentRequested(var student)

    readonly property bool subbed: stack.cs && stack.cs.subscribed === true
    spacing: 12

    ClassOnboardGuide {
        visible: !stack.boardExpanded && !stack.subbed
        width: parent.width
        classCount: stack.classCount
        onCreateRequested: stack.createRequested()
    }

    ClassIdentityPane {
        visible: !stack.boardExpanded
        width: parent.width
        cs: stack.cs
        pending: stack.pending
        onUnsubscribeRequested: stack.unsubscribeRequested()
    }

    Flickable {
        width: parent.width
        height: stack.boardExpanded ? parent.height : parent.height - 200
        contentHeight: pCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: pCol
            width: parent.width
            spacing: 12

            ClassMiddlePane {
                width: parent.width
                // 展板全屏时中栏吃掉整屏高度（竖屏没有左右栏可藏，只能给高度）
                height: stack.boardExpanded ? Math.max(320, stack.height - 24) : 260
                deviceModel: stack.devModel
                evModel: stack.evModel
                subscribed: (stack.cs && stack.cs.subscribed === true)
                selectedUid: stack.selectedUid
                tab: stack.boardTab
                classes: (typeof backend !== "undefined" && backend) ? backend.classes : []
                classCode: (stack.cs && stack.cs.classCode) ? stack.cs.classCode : ""
                className: stack.className
                boardExpanded: stack.boardExpanded
                onTabActivated: function (v) { stack.tabActivated(v) }
                onDeviceClicked: function (uid) { stack.deviceClicked(uid) }
                onBoardExpandToggled: stack.boardExpandToggled()
                onPublishRequested: stack.publishRequested()
                onEditStudentRequested: function (st) { stack.editStudentRequested(st) }
            }

            ClassAdminPane {
                visible: !stack.boardExpanded
                width: parent.width
                cs: stack.cs
                toastFn: stack.toastFn
            }
            ClassActionPane {
                visible: !stack.boardExpanded
                width: parent.width
                host: stack.host
                cs: stack.cs
                devices: stack.devices
                selectedUid: stack.selectedUid
                selectedName: stack.selectedName
                toastFn: stack.toastFn
                openControlFn: stack.openControlFn
                onPublishRequested: stack.publishRequested()
            }
            // 课堂工具（2026-10-25）：与横屏右栏同一面板，竖屏同样要能一键对全班。
            ClassToolPane {
                visible: !stack.boardExpanded
                width: parent.width
                host: stack.host
                cs: stack.cs
                devices: stack.devices
                toastFn: stack.toastFn
            }
            ClassTimelinePane {
                visible: !stack.boardExpanded
                width: parent.width
                height: 180
                model: stack.evModel
            }
        }
    }
}
