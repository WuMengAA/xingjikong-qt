import QtQuick 2.15
import "."

// 灵动岛 · 「设」面（2026-10-27 · C 批）
//
// ── 为什么这一面必须在岛上（用户 07:36 裁决）────────────────────────
//   「这些功能大多是加入到被控端，而且大部分和灵动岛有关」。
//   系统音量 / 通知音量 / 通知黑名单是老师上课当场要调的，管理端不在手边，岛才是他的手。
//
// ── 数据从哪来 ──────────────────────────────────────────────────
//   全部来自 IslandBridge 的属性（systemVolume / notifyVolume / notifyBlacklist），
//   按钮走 bridge.settingsAction(cmd)。本面**不存权威状态**：真值在 C++ 域 / QSettings，
//   QML 只做显示 + 拖动时即时跟手 + 转发。
//
// ── 为什么滑块只推不拉 ──────────────────────────────────────────
//   滑块 value 不绑回 bridge 属性：系统音量 TTS 前后会被抬到 75%/恢复到 50%，若把
//   滑块绑成桥属性，下一次 changed 就会把滑块拽回真实值，和老师手指打架。所以只在一
//   次性打开时从桥取初值，之后以本地拖动为准；下次打开再重新取当前设备真实值。
//
// ── 布局（重要）──────────────────────────────────────────────────
//   行用「锚」布局（label 锚左 / value 锚右 / 滑块锚中间剩余空间），**不**用 Row 子项
//   写宽度——Row 自身无显式宽度时，子项写 `parent.width - N` 会陷入循环绑定，结果滑
//   块宽度算错、贴边被 IslandSurface 圆角裁剪（实测两次）。锚布局无此坑。
Item {
    id: face

    property var th: null
    property var bridge: null

    // 一次性初值（打开面时从桥拉一次；之后本地跟手，不再回写）
    property int sysVol:    face.bridge ? face.bridge.systemVolume : -1
    property int notifyVol: face.bridge ? face.bridge.notifyVolume : 75
    property var blacklist: face.bridge ? face.bridge.notifyBlacklist : []

    function act(cmd) {
        if (face.bridge && face.bridge.settingsAction) face.bridge.settingsAction(cmd)
        // 黑名单会因增删而变 ⇒ 拉一次最新（滑块值由本地维护，不在这里回写）
        face.blacklist = face.bridge.notifyBlacklist
    }

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 10

        // ── 系统音量 ───────────────────────────────────────────
        Item {
            width: parent.width
            height: 28
            Text {
                id: sysLbl
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "系统音量"
                color: face.th ? face.th.fg : "#FFFFFF"
                font.pixelSize: 12
                width: 56
            }
            Text {
                id: sysVal
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: (face.sysVol < 0 ? "—" : face.sysVol + "%")
                color: face.th ? face.th.sub : "#9A9A9A"
                font.pixelSize: 12
                width: 40
                horizontalAlignment: Text.AlignRight
            }
            IslandVolSlider {
                anchors.left: sysLbl.right
                anchors.leftMargin: 8
                anchors.right: sysVal.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                th: face.th
                Component.onCompleted: value = (face.sysVol < 0 ? 0 : face.sysVol)
                onValueChanged: face.act("sysvol:" + value)
            }
        }

        // ── 通知音量 ───────────────────────────────────────────
        Item {
            width: parent.width
            height: 28
            Text {
                id: nfyLbl
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "通知音量"
                color: face.th ? face.th.fg : "#FFFFFF"
                font.pixelSize: 12
                width: 56
            }
            Text {
                id: nfyVal
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: (face.notifyVol < 0 ? "—" : face.notifyVol + "%")
                color: face.th ? face.th.sub : "#9A9A9A"
                font.pixelSize: 12
                width: 40
                horizontalAlignment: Text.AlignRight
            }
            IslandVolSlider {
                anchors.left: nfyLbl.right
                anchors.leftMargin: 8
                anchors.right: nfyVal.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                th: face.th
                Component.onCompleted: value = (face.notifyVol < 0 ? 75 : face.notifyVol)
                onValueChanged: face.act("notifyvol:" + value)
            }
        }

        // ── 通知黑名单 ─────────────────────────────────────────
        Column {
            width: parent.width
            spacing: 6

            Text {
                text: "通知黑名单（来源名）"
                color: face.th ? face.th.fg : "#FFFFFF"
                font.pixelSize: 12
            }

            Repeater {
                model: face.blacklist
                Item {
                    required property var modelData
                    width: parent.width
                    height: 24
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: 56
                        height: 24
                        radius: 4
                        color: (face.th && face.th.light) ? "#F0F0F0" : "#2A2A2A"
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: face.th ? face.th.fg : "#FFFFFF"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: parent.width - 16
                        }
                    }
                    ActionButton {
                        anchors.right: parent.right
                        height: 24
                        label: "移除"
                        onClicked: face.act("blacklist:remove:" + modelData)
                    }
                }
            }

            Item {
                width: parent.width
                height: 24
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: 56
                    height: 24
                    radius: 4
                    border.width: 1
                    border.color: (face.th && face.th.light) ? "#D0D0D0" : "#3C3C3C"
                    TextInput {
                        id: blInput
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: face.th ? face.th.fg : "#FFFFFF"
                        font.pixelSize: 12
                        clip: true
                    }
                }
                ActionButton {
                    anchors.right: parent.right
                    height: 24
                    label: "添加"
                    onClicked: {
                        const t = blInput.text.trim()
                        if (t !== "") { face.act("blacklist:add:" + t); blInput.text = "" }
                    }
                }
            }

            ActionButton {
                label: "清空黑名单"
                onClicked: face.act("blacklist:clear")
            }
        }
    }
}
