// 闹钟弹窗（2026-10-26 · 产品笔记 P1）：定时响铃 + 可选 TTS 播报 + 可选锁屏。
//
// 与「定时关机/重启」并行：闹钟落在设备本地（alarms.json，进程崩溃/重启不丢），
// 走 user32 LockWorkStation + 系统蜂鸣 + speakText（Edge TTS → SAPI 兜底）。
// 重复支持 一次 / 每天 / 工作日(一~五) / 周末 —— 对应的 days 数组下发到 agent：
//   一次 = []（仅当天，今天已过 → 明天同刻，与手机闹钟一致）
//   每天 = [1,2,3,4,5,6,7]（Qt dayOfWeek：1=周一…7=周日）
//   工作日 = [1,2,3,4,5]　周末 = [6,7]
//
// 面板自管「拉列表 / 挂 / 删」：全部走 backend.sendAction（viewer → 云端 → agent），
// 云端仅透传（无 action 白名单），无需云端改动。
//
// 2026-10-26（产品笔记 P2）：视觉直接吃 MD3 令牌 + fresh 组件（与发布中心同语言）。

import QtQuick
import QtQuick.Controls
import "components"        // InputField / SvgIcon 等（Qt6.12 严格解析，统一显式 import）
import "island"            // SvgIcon 唯一真源
import "components/theme"  // Theme 设计令牌单例（pragma Singleton）
import "components/fresh"  // FreshBtn（主按钮）

Popup {
    id: dlg
    property var theme: null
    property var ctx

    // 已设闹钟（list_alarms 回执 items）
    property var alarms: []
    property string timeH: ""     // 时（HH）
    property string timeM: ""     // 分（MM）
    property string repeatSel: "once"   // once | daily | weekdays | weekend
    property bool withLock: false
    property string text: ""

    readonly property string timeFull: {
        const h = (dlg.timeH === "" ? "" : String(parseInt(dlg.timeH, 10))).padStart(2, "0")
        const m = (dlg.timeM === "" ? "" : String(parseInt(dlg.timeM, 10))).padStart(2, "0")
        return h.length === 2 && m.length === 2 ? h + ":" + m : ""
    }
    readonly property bool timeOk: timeFull !== ""
    readonly property var daysOfSel: ({
        once: [], daily: [1,2,3,4,5,6,7], weekdays: [1,2,3,4,5], weekend: [6,7]
    })[dlg.repeatSel]

    anchors.centerIn: parent
    width: 560
    height: 470
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.color.surface
        radius: Theme.radius.lg
        border.color: Theme.color.surfaceVariant
        border.width: 1
    }

    // 手写小组件：分段选择的一段（与 NotifyDialog 同款）
    component Seg: Rectangle {
        id: seg
        property string label: ""
        property bool active: false
        signal tapped()
        width: 88
        height: 28
        radius: Theme.radius.md
        color: seg.active
               ? (Theme.color.primary)
               : (segHover.containsMouse ? (Theme.color.surface) : "transparent")
        border.color: seg.active ? "transparent" : (Theme.color.textSecondary)
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: seg.label
            color: seg.active
                   ? (Theme.color.primaryForeground)
                   : (Theme.color.textSecondary)
            font.pixelSize: 12
            font.weight: seg.active ? Font.Medium : Font.Normal
        }
        MouseArea {
            id: segHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: seg.tapped()
        }
    }

    // 手写小组件：勾选整行（与 NotifyDialog 同款）
    component Tick: Rectangle {
        id: tick
        property bool checked: false
        width: 16; height: 16; radius: 4
        color: tick.checked ? (Theme.color.primary) : "transparent"
        border.color: tick.checked ? (Theme.color.primary)
                                   : (Theme.color.textSecondary)
        border.width: 1
        SvgIcon {
            anchors.centerIn: parent
            visible: tick.checked
            name: "check"
            tint: Theme.color.primaryForeground
            width: 11; height: 11
        }
    }

    component ToggleRow: Item {
        id: row
        property string label: ""
        property string help: ""
        property bool checked: false
        signal tapped()
        width: 240; height: 32
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Tick { checked: row.checked }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: row.label
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.help !== ""
                text: row.help
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: row.tapped()
        }
    }

    contentItem: Column {
        spacing: 0

        // ── 标题栏 ──
        Item {
            width: parent.width
            height: 46
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "闹钟"
                color: Theme.color.textPrimary
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 62
                text: "定时响铃 · 设备本地，重启不丢"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                name: "x"
                tint: Theme.color.textSecondary
                width: 14; height: 14
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
                color: Theme.color.surfaceVariant
            }
        }

        // ── 时间 ──
        Item {
            width: parent.width
            height: 44
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "时间"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
            InputField {
                id: hhInput
                th: dlg.theme
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                width: 56; height: 30
                validator: IntValidator { bottom: 0; top: 23 }
                text: dlg.timeH
                onTextChanged: dlg.timeH = hhInput.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 136
                text: ":"
                color: Theme.color.textPrimary
                font.pixelSize: 14
            }
            InputField {
                id: mmInput
                th: dlg.theme
                anchors.verticalCenter: parent.verticalCenter
                x: 148
                width: 56; height: 30
                validator: IntValidator { bottom: 0; top: 59 }
                text: dlg.timeM
                onTextChanged: dlg.timeM = mmInput.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 214
                text: "24 小时制；已过今天 → 明天同刻响"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
        }

        // ── 重复 ──
        Item {
            width: parent.width
            height: 38
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "重复"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                spacing: 6
                Seg { label: "一次";         active: dlg.repeatSel === "once";     onTapped: dlg.repeatSel = "once" }
                Seg { label: "每天";         active: dlg.repeatSel === "daily";    onTapped: dlg.repeatSel = "daily" }
                Seg { label: "工作日";       active: dlg.repeatSel === "weekdays"; onTapped: dlg.repeatSel = "weekdays" }
                Seg { label: "周末";         active: dlg.repeatSel === "weekend";  onTapped: dlg.repeatSel = "weekend" }
            }
        }

        // ── 选项 ──
        Item {
            width: parent.width
            height: 72
            ToggleRow {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                label: "到点锁屏"
                help: "响铃同时锁住屏幕"
                checked: dlg.withLock
                onTapped: dlg.withLock = !dlg.withLock
            }
            InputField {
                id: textInput
                th: dlg.theme
                anchors.verticalCenter: parent.verticalCenter
                x: 282
                width: parent.width - 300
                height: 30
                placeholderText: "播报文字（可选，TTS 朗读）"
                maximumLength: 80
                onTextChanged: dlg.text = textInput.text
            }
        }

        // ── 挂闹钟 ──
        Item {
            width: parent.width
            height: 42
            FreshBtn {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: 120
                height: 32
                text: "挂闹钟"
                icon: "clock"
                enabled: dlg.timeOk
                onClicked: dlg.addAlarm()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 150
                text: dlg.timeOk ? "" : "先填时间（时 0-23 · 分 0-59）"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: "已达 " + dlg.alarms.length + " 个"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
        }

        // ── 已设列表 ──
        Item {
            width: parent.width
            height: 26
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "本机已设闹钟"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.color.surfaceVariant
            }
        }

        ListView {
            width: parent.width
            height: 150
            clip: true
            model: dlg.alarms
            spacing: 2

            delegate: Rectangle {
                width: ListView.width - 24
                height: 34
                x: 12
                radius: Theme.radius.md
                color: rowHover.containsMouse ? (Theme.color.surface) : "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    text: {
                        const d = modelData.days || []
                        const rep = d.length === 0 ? "一次"
                                   : d.length === 7 ? "每天"
                                   : d.length === 5 && d[0] === 1 ? "工作日"
                                   : d.length === 2 && d[0] === 6 ? "周末"
                                   : "星期" + d.join("/")
                        return "⏰ " + modelData.time + " · " + rep
                    }
                    color: Theme.color.textPrimary
                    font.pixelSize: 12
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 210
                    width: parent.width - 330
                    elide: Text.ElideMiddle
                    text: modelData.text ? ("播报：" + modelData.text) : ""
                    color: Theme.color.textSecondary
                    font.pixelSize: 11
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 56
                    text: modelData.nextAt ? ("下次 " + String(modelData.nextAt).replace("T", " ").slice(5, 16)) : "已到"
                    color: Theme.color.textSecondary
                    font.pixelSize: 11
                }
                Rectangle {
                    id: delBtn
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    width: 40; height: 24
                    radius: Theme.radius.md
                    color: delHover.containsMouse ? (Theme.color.destructive) : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "删"
                        color: Theme.color.destructive
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: delHover
                        anchors.fill: parent
                        onClicked: {
                            backend.sendAction("cancel_alarm", { "id": modelData.id })
                            dlg.refreshTimer.restart()
                        }
                    }
                }

                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            Item {
                width: ListView.width
                height: 40
                visible: dlg.alarms.length === 0
                Text {
                    anchors.centerIn: parent
                    text: "还没挂闹钟 —— 上面填个时间点「挂闹钟」"
                    color: Theme.color.textSecondary
                    font.pixelSize: 11
                }
            }
        }

        // ── 一把说清 ──
        Item {
            width: parent.width
            height: 44
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.color.surfaceVariant
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 36
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                color: Theme.color.textSecondary
                font.pixelSize: 11
                text: "闹钟存在设备本地（\"%LOCALAPPDATA%\\xingjikong\\alarms.json\"），进程重启不丢；"
                      + "到点系统蜂鸣三声，勾了播报就 TTS 读文字，勾了锁屏就锁住屏幕。"
            }
        }

        // 每次打开/操作完重拉一次，让列表反映真实落盘状态
        Timer {
            id: refreshTimer
            interval: 900
            repeat: false
            onTriggered: backend.sendAction("list_alarms")
        }
    }

    function addAlarm() {
        const days = []
        for (let i = 0; i < dlg.daysOfSel.length; ++i) days.push(dlg.daysOfSel[i])
        backend.sendAction("schedule_alarm", {
            "time": dlg.timeFull,
            "days": days,
            "lock": dlg.withLock,
            "text": textInput.text.trim()
        })
        dlg.repeatSel = "once"; dlg.withLock = false; dlg.timeH = ""; dlg.timeM = ""
        hhInput.text = ""; mmInput.text = ""; textInput.text = ""
        dlg.refreshTimer.restart()
    }

    function setItems(arr, file) {
        dlg.alarms = arr || []
    }

    onOpened: {
        dlg.alarms = []
        backend.sendAction("list_alarms")
    }
}