import QtQuick 2.15
import "."

// 灵动岛 · 自动化规则「面」（面 6）
//
// 2026-10-27 · Phase 3（被控端定时自动化规则引擎）。
// 主入口长在岛上（用户裁决：这类"老师当场要配"的能力放被控端、且长在岛上）。
//
// 数据来自被控端 AutomationEngine（main.cpp 经 setQmlObjectProperty 注入为
// `automationObj`，本面收 `automationRef`）。引擎是纯模型：点火时把动作交给
// main.cpp 转发到既有 executeAction —— 复用整套设备指令词表与其安全白名单。
//
// 接口：
//   th            主题令牌（必传）
//   automationRef  AutomationEngine QObject（ruleSummaries / addRule / removeRule
//                 / toggleRule / runNow）
//   bridgeRef     桥（预留，本面暂不消费）
Item {
    id: face
    property var th: null
    property var automationRef: null
    property var bridgeRef: null

    readonly property bool hasEngine: face.automationRef != null
    readonly property int  ruleCount: (face.automationRef ? face.automationRef.ruleCount : 0)

    // ── 编辑态 + 表单状态 ──
    property bool   editing: false
    property string fName: ""
    property string fTrigger: "time"        // time | interval | event
    property string fTime: "08:00"
    property int    fInterval: 30
    property string fEvent: "boot"          // boot|idle|classStart|classEnd|screenLock|screenUnlock
    property string fAction: "notify"       // notify|set_volume|mute|unmute|lock|screenshot|launch_app
    property string fTitle: ""
    property int    fVolume: 50
    property string fTarget: ""

    function openEditor() {
        face.fName = ""; face.fTrigger = "time"; face.fTime = "08:00"; face.fInterval = 30
        face.fEvent = "boot"; face.fAction = "notify"; face.fTitle = ""; face.fVolume = 50; face.fTarget = ""
        face.editing = true
    }
    function saveEditor() {
        if (!face.automationRef) { face.editing = false; return }
        var trig = {}
        if (face.fTrigger === "time")       trig = { "type": "time", "time": face.fTime }
        else if (face.fTrigger === "interval") trig = { "type": "interval", "intervalMin": face.fInterval }
        else trig = { "type": "event", "event": face.fEvent }
        var act = { "kind": face.fAction }
        if (face.fAction === "notify")          act["title"] = face.fTitle
        else if (face.fAction === "set_volume")  act["value"] = face.fVolume
        else if (face.fAction === "unmute")      act["value"] = face.fVolume
        else if (face.fAction === "launch_app")  act["target"] = face.fTarget
        face.automationRef.addRule({
            "name": (face.fName !== "" ? face.fName : "未命名规则"),
            "enabled": true, "trigger": trig, "condition": {}, "action": act
        })
        face.editing = false
    }

    // ════════════════════════════════════════════════════════════
    // 列表态
    // ════════════════════════════════════════════════════════════
    Item {
        visible: face.hasEngine && !face.editing
        anchors.fill: parent

        Text {
            id: hdr
            anchors.top: parent.top; anchors.left: parent.left
            text: "自动化规则"; color: th.fg
            font.pixelSize: th.fsBadge; font.bold: true
        }
        Text {
            anchors.left: hdr.right; anchors.leftMargin: th.spS
            anchors.verticalCenter: hdr.verticalCenter
            text: face.ruleCount > 0 ? ("· " + face.ruleCount) : ""
            color: th.sub; font.pixelSize: th.fsNote
        }

        Flickable {
            id: list
            anchors.top: hdr.bottom; anchors.topMargin: th.spS
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: addBtn.top; anchors.bottomMargin: th.spS
            contentHeight: col.height
            clip: true
            Column {
                id: col
                width: parent.width
                spacing: th.spXS

                Repeater {
                    model: face.automationRef ? face.automationRef.ruleSummaries : []
                    delegate: Item {
                        required property var modelData
                        width: col.width; height: th.rowH

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1; color: th.light ? "#EAEAEA" : "#2C2C2C"
                        }
                        // 启用指示（点它切换）
                        SvgIcon {
                            id: dot
                            name: modelData.enabled ? "circle-dot" : "circle-dashed"
                            tint: modelData.enabled ? th.accent : th.sub
                            px: 14
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                onClicked: face.automationRef.toggleRule(modelData.id, !modelData.enabled)
                            }
                        }
                        // 文本
                        Column {
                            anchors.left: dot.right; anchors.leftMargin: th.spS
                            anchors.right: runBtn.left; anchors.rightMargin: th.spS
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: modelData.name
                                color: th.fg; font.pixelSize: th.fsBody; font.bold: true
                                elide: Text.ElideRight; width: parent.width
                            }
                            Text {
                                text: modelData.triggerText + " → " + modelData.actionText
                                      + (modelData.nextRunText !== "—" ? "  ·  " + modelData.nextRunText : "")
                                color: th.sub; font.pixelSize: th.fsNote
                                elide: Text.ElideRight; width: parent.width
                            }
                        }
                        // 立即运行
                        SvgIcon {
                            id: runBtn
                            name: "play"; tint: th.sub; px: 16
                            anchors.right: delBtn.left; anchors.rightMargin: th.spM
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea { anchors.fill: parent; onClicked: face.automationRef.runNow(modelData.id) }
                        }
                        // 删除
                        SvgIcon {
                            id: delBtn
                            name: "trash"; tint: th.sub; px: 16
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            MouseArea { anchors.fill: parent; onClicked: face.automationRef.removeRule(modelData.id) }
                        }
                    }
                }

                // 空态
                Item {
                    visible: face.ruleCount === 0
                    width: col.width; height: 84
                    Text {
                        anchors.centerIn: parent
                        text: "还没有自动化规则\n点下方「新建规则」添加第一条"
                        color: th.sub; font.pixelSize: th.fsNote
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // 新建按钮
        Rectangle {
            id: addBtn
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 34; radius: th.rRow
            color: th.light ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1, 0.06)
            Text {
                anchors.centerIn: parent
                text: "＋ 新建规则"; color: th.accent
                font.pixelSize: th.fsBody; font.bold: true
            }
            MouseArea { anchors.fill: parent; onClicked: face.openEditor() }
        }
    }

    // ════════════════════════════════════════════════════════════
    // 编辑态
    // ════════════════════════════════════════════════════════════
    Item {
        visible: face.editing
        anchors.fill: parent

        Flickable {
            id: form
            anchors.fill: parent; anchors.bottomMargin: 44
            contentHeight: fcol.height; clip: true
            Column {
                id: fcol
                width: parent.width
                spacing: th.spM

                // 名称
                Text { text: "名称"; color: th.sub; font.pixelSize: th.fsNote }
                Rectangle {
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: face.fName
                        onTextChanged: face.fName = text
                    }
                }

                // 触发类型
                Text { text: "触发"; color: th.sub; font.pixelSize: th.fsNote }
                Row {
                    spacing: th.spXS
                    Repeater {
                        model: [{ v: "time", t: "每天" }, { v: "interval", t: "每分" }, { v: "event", t: "事件" }]
                        delegate: Rectangle {
                            width: 64; height: 28; radius: th.rPill
                            color: face.fTrigger === modelData.v ? th.accent : (th.light ? "#FFFFFF" : "#2A2A2A")
                            Text {
                                anchors.centerIn: parent; text: modelData.t
                                color: face.fTrigger === modelData.v ? (th.light ? "#FFFFFF" : "#1E1E1E") : th.sub
                                font.pixelSize: th.fsNote
                            }
                            MouseArea { anchors.fill: parent; onClicked: face.fTrigger = modelData.v }
                        }
                    }
                }
                // 触发参数
                // ⚠️ 2026-10-27：原用 TextInput 的 placeholderText 提示格式 —— 该属性
                //    **只在 QtQuick.Controls 的 TextField 上**，QtQuick 原生 TextInput 没有
                //    （qmllint + 运行时双证：Cannot assign to non-existent property）。
                //    本工程禁原生控件 ⇒ 保持 TextInput，提示改由下方 hint 小字给出。
                Text {
                    visible: face.fTrigger === "time"
                    text: "时刻，格式 HH:MM（如 08:00）"; color: th.sub; font.pixelSize: th.fsNote
                }
                Rectangle {
                    visible: face.fTrigger === "time"
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: face.fTime
                        onTextChanged: face.fTime = text
                    }
                }
                Text {
                    visible: face.fTrigger === "interval"
                    text: "间隔分钟（如 30）"; color: th.sub; font.pixelSize: th.fsNote
                }
                Rectangle {
                    visible: face.fTrigger === "interval"
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: String(face.fInterval)
                        onTextChanged: face.fInterval = parseInt(text) || 0
                    }
                }
                Flow {
                    visible: face.fTrigger === "event"
                    width: parent.width; spacing: th.spXS
                    Repeater {
                        model: [
                            { v: "boot", t: "开机" }, { v: "idle", t: "空闲" },
                            { v: "classStart", t: "上课" }, { v: "classEnd", t: "下课" },
                            { v: "screenLock", t: "锁屏" }, { v: "screenUnlock", t: "解锁" }
                        ]
                        delegate: Rectangle {
                            width: 52; height: 26; radius: th.rPill
                            color: face.fEvent === modelData.v ? th.accent : (th.light ? "#FFFFFF" : "#2A2A2A")
                            Text {
                                anchors.centerIn: parent; text: modelData.t
                                color: face.fEvent === modelData.v ? (th.light ? "#FFFFFF" : "#1E1E1E") : th.sub
                                font.pixelSize: th.fsNote
                            }
                            MouseArea { anchors.fill: parent; onClicked: face.fEvent = modelData.v }
                        }
                    }
                }

                // 动作类型
                Text { text: "动作"; color: th.sub; font.pixelSize: th.fsNote }
                Flow {
                    width: parent.width; spacing: th.spXS
                    Repeater {
                        model: [
                            { v: "notify", t: "通知" }, { v: "set_volume", t: "音量" },
                            { v: "mute", t: "静音" }, { v: "unmute", t: "取消静音" },
                            { v: "lock", t: "锁屏" }, { v: "screenshot", t: "截图" },
                            { v: "launch_app", t: "启动" }
                        ]
                        delegate: Rectangle {
                            width: 56; height: 26; radius: th.rPill
                            color: face.fAction === modelData.v ? th.accent : (th.light ? "#FFFFFF" : "#2A2A2A")
                            Text {
                                anchors.centerIn: parent; text: modelData.t
                                color: face.fAction === modelData.v ? (th.light ? "#FFFFFF" : "#1E1E1E") : th.sub
                                font.pixelSize: th.fsNote
                            }
                            MouseArea { anchors.fill: parent; onClicked: face.fAction = modelData.v }
                        }
                    }
                }
                // 动作参数（提示同样走 hint 小字，理由见上方「触发参数」注释）
                Text {
                    visible: face.fAction === "notify"
                    text: "通知标题（必填，空则规则无法点火）"; color: th.sub; font.pixelSize: th.fsNote
                }
                Rectangle {
                    visible: face.fAction === "notify"
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: face.fTitle
                        onTextChanged: face.fTitle = text
                    }
                }
                Text {
                    visible: face.fAction === "set_volume" || face.fAction === "unmute"
                    text: "音量 0-100"; color: th.sub; font.pixelSize: th.fsNote
                }
                Rectangle {
                    visible: face.fAction === "set_volume" || face.fAction === "unmute"
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: String(face.fVolume)
                        onTextChanged: face.fVolume = parseInt(text) || 0
                    }
                }
                Text {
                    visible: face.fAction === "launch_app"
                    text: "程序名或路径（受启动白名单约束）"; color: th.sub; font.pixelSize: th.fsNote
                }
                Rectangle {
                    visible: face.fAction === "launch_app"
                    width: parent.width; height: 32; radius: th.rRow
                    color: th.light ? "#FFFFFF" : "#2A2A2A"
                    border.width: 1; border.color: th.light ? "#E0E0E0" : "#3A3A3A"
                    TextInput {
                        anchors.fill: parent; anchors.leftMargin: th.spS; anchors.rightMargin: th.spS
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg; font.pixelSize: th.fsBody
                        text: face.fTarget
                        onTextChanged: face.fTarget = text
                    }
                }
            }
        }

        // 保存 / 取消
        Row {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            spacing: th.spS
            Rectangle {
                width: (parent.width - th.spS) / 2; height: 34; radius: th.rRow
                color: th.light ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1, 0.06)
                Text { anchors.centerIn: parent; text: "取消"; color: th.sub; font.pixelSize: th.fsBody }
                MouseArea { anchors.fill: parent; onClicked: face.editing = false }
            }
            Rectangle {
                width: (parent.width - th.spS) / 2; height: 34; radius: th.rRow; color: th.accent
                Text { anchors.centerIn: parent; text: "保存"; color: th.light ? "#FFFFFF" : "#1E1E1E"
                    font.pixelSize: th.fsBody; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: face.saveEditor() }
            }
        }
    }
}
