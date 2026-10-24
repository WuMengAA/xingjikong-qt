// 班级工作台 · 竖屏竖排栈（ClassDashboard 抽件 · 守 200 行）
//
// 竖屏（宽<高）布局：身份 → 中栏（设备网格/展板/成员）→ 管理 → 动作 → 时间线，
// 中段以下进 Flickable（小屏放不下，必须可滚）。与横屏三栏是同一批窗格件、
// 同一份接线，只是排列方式不同 —— 编排真源仍在 ClassDashboard。
import QtQuick

Column {
    id: stack

    property var md3: ({})
    property var cs: null            // ClassService
    property var host: null          // ctx.window
    property var devices: []         // backend.devices
    property var devModel: null      // cs.deviceModel
    property var evModel: null       // cs.eventModel
    property bool pending: false
    property string boardTab: "devices"
    property string selectedUid: ""
    property string selectedName: ""
    property var toastFn: null
    property var openControlFn: null

    signal tabActivated(string v)
    signal deviceClicked(string uid)
    signal unsubscribeRequested()

    spacing: 12

    ClassIdentityPane {
        width: parent.width
        md3: stack.md3
        cs: stack.cs
        pending: stack.pending
        onUnsubscribeRequested: stack.unsubscribeRequested()
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
                md3: stack.md3
                deviceModel: stack.devModel
                subscribed: (stack.cs && stack.cs.subscribed === true)
                selectedUid: stack.selectedUid
                tab: stack.boardTab
                classes: (typeof backend !== "undefined" && backend) ? backend.classes : []
                classCode: (stack.cs && stack.cs.classCode) ? stack.cs.classCode : ""
                onTabActivated: function (v) { stack.tabActivated(v) }
                onDeviceClicked: function (uid) { stack.deviceClicked(uid) }
            }

            ClassAdminPane {
                width: parent.width
                md3: stack.md3
                cs: stack.cs
                toastFn: stack.toastFn
            }
            ClassActionPane {
                width: parent.width
                md3: stack.md3
                host: stack.host
                cs: stack.cs
                devices: stack.devices
                selectedUid: stack.selectedUid
                selectedName: stack.selectedName
                toastFn: stack.toastFn
                openControlFn: stack.openControlFn
            }
            ClassTimelinePane {
                width: parent.width
                height: 180
                md3: stack.md3
                model: stack.evModel
            }
        }
    }
}
