// 定时弹窗：安排关机/重启（schedule_shutdown / schedule_reboot）、看待执行（list_schedules）、取消（cancel_schedule）。
//
// 为什么单独一个弹窗：定时是**唯一一个"设了看不见结果"**的动作 ——
// 老师最怕"我设了，机器照常开机"。所以这里必须三件事一屏给全：
//   ① 怎么安排（填多少分钟后）
//   ② 现在挂了几条、什么时候
//   ③ 怎么取消
// 只有①的话，设完就黑盒；有②③，"设了没生效"至少当场能看见并改。
//
// 另外一个必须写明的事实：被控端到期**默认不会真关机**（要装机时把 STE_QT_ALLOW_SCHED_FIRE 设成 1）。
// 所以安排完要把这条显示出来，别让"安排成功"被当成"机器会关"。

import QtQuick
import QtQuick.Controls
import "components"       // 2026-10-10：SvgIcon 等组件统一显式 import（Qt6.12 严格解析）

Popup {
    id: dlg
    property var theme: null
    property var items: []        // list_schedules 的 items
    property string schedFile: ""
    property string chosenId: ""   // 当前选中要取消的那条

    anchors.centerIn: parent
    width: 560
    height: 400
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme ? dlg.theme.cream : "#141414"
        radius: dlg.theme ? dlg.theme.rCard : 12
        border.color: dlg.theme ? dlg.theme.stroke : "#242424"
        border.width: 1
    }

    contentItem: Column {
        spacing: 0

        Item {
            width: parent.width
            height: 46

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "定时"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                name: "x"
                tint: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                width: 14
                height: 14
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -10
                    onClicked: dlg.close()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
        }

        // ── ① 安排 ──
        Item {
            width: parent.width
            height: 34
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "安排"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                width: 92
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                TextInput {
                    id: minInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                    validator: IntValidator { bottom: 1; top: 720 }
                    text: "60"
                }
            }
            Rectangle {
                id: btnShutdown
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 116
                width: 84
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                color: btnShutdownMA.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "分钟后关机"
                    color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                    font.pixelSize: 11
                }
                MouseArea {
                    id: btnShutdownMA
                    anchors.fill: parent
                    onClicked: doSchedule("schedule_shutdown")
                }
            }
            Rectangle {
                id: btnReboot
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 208
                width: 84
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                color: btnRebootMA.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "分钟后重启"
                    color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                    font.pixelSize: 11
                }
                MouseArea {
                    id: btnRebootMA
                    anchors.fill: parent
                    onClicked: doSchedule("schedule_reboot")
                }
            }
        }

        // ── ② 待执行 ──
        Item {
            width: parent.width
            height: 28
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "这条机器机器上的待执行（" + dlg.items.length + "）"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
        }

        ListView {
            width: parent.width
            height: 150
            clip: true
            model: dlg.items
            spacing: 2

            delegate: Rectangle {
                width: ListView.width - 24
                height: 30
                x: 12
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                color: schedRowMA.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E")
                                                : (dlg.chosenId === modelData.id ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent")

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    text: (modelData.what === "reboot" ? "重启" : "关机") + "  " + (modelData.at || "")
                    color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                    font.pixelSize: 12
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: "还有 " + Math.round((modelData.inSec || 0) / 60) + " 分钟"
                    color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                    font.pixelSize: 11
                }

                MouseArea {
                    id: schedRowMA
                    anchors.fill: parent
                    onClicked: dlg.chosenId = modelData.id
                }
            }

            Item {
                width: ListView.width
                height: 40
                visible: dlg.items.length === 0
                Text {
                    anchors.centerIn: parent
                    text: "现在没有任何待执行（取消过或被到期跑掉了）"
                    color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                    font.pixelSize: 11
                }
            }
        }

        // ── ③ 取消 ──
        Item {
            width: parent.width
            height: 46
            Rectangle {
                id: btnCancel
                anchors.centerIn: parent
                width: 200
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                color: btnCancelMA.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: dlg.chosenId ? ("取消 " + dlg.chosenId) : "（先选一条上面待执行的）"
                    color: dlg.chosenId ? (dlg.theme ? dlg.theme.op : "#C8C8C8") : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                    font.pixelSize: 11
                }
                MouseArea {
                    id: btnCancelMA
                    anchors.fill: parent
                    onClicked: {
                        if (dlg.chosenId === "") return;
                        backend.sendAction("cancel_schedule", { "id": dlg.chosenId })
                        dlg.refreshTimer.restart()
                    }
                }
            }

            // 每次打开/操作完都重新拉一次：让列表反映真实落盘状态，别显示"上次看到的"
            Timer {
                id: refreshTimer
                interval: 900
                repeat: false
                onTriggered: backend.sendAction("list_schedules")
            }
        }

        Item {
            width: parent.width
            height: 26
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 36
                wrapMode: Text.WordWrap
                text: "被控端到期**默认不会真关机**：装教室机时要把 STE_QT_ALLOW_SCHED_FIRE 设成 1（回执里会带 fireAllowed 说明当前状态）"
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
            }
        }
    }

    function doSchedule(action) {
        const m = parseInt(minInput.text, 10);
        if (!m || m <= 0) { minInput.text = "60"; return; }
        backend.sendAction(action, { "delay_sec": m * 60 });
        refreshTimer.restart();
    }

    function setItems(arr, file) {
        dlg.items = arr || []
        dlg.schedFile = file || ""
        dlg.chosenId = ""
    }
}
