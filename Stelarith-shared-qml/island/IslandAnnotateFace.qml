import QtQuick 2.15
import "."

// 灵动岛 · 「笔」面（2026-10-27 · H 批）
//
// ── 为什么这一面必须在岛上（用户 07:36 裁决）────────────────────────
//   「这些功能大多是加入到被控端，而且大部分和灵动岛有关」。
//   批注原本只挂在管理端面板里 —— 老师站在讲台，管理端不在手边，等于没有。
//   岛是他手边唯一的本机交互面，所以「开始批注 / 换笔 / 清空」长在这里。
//
// ── 数据从哪来 ──────────────────────────────────────────────────
//   全部来自 IslandBridge 的四个属性（annotateActive / annotateTool /
//   annotatePage / annotateHint），按钮走 bridge.annotateAction(cmd)。
//   ⚠️ 本面**不存任何状态**：批注层的真状态在 C++ 域里，QML 只负责显示 + 转发。
//      否则岛显示"正在批注"而域其实没起来，老师会以为写了却什么都没发生。
//
// ── 为什么覆盖层起来后岛还在上面 ────────────────────────────────
//   开始批注时桥会把岛的窗口注册为「必须压在我之上」（Overlay::setWindowAboveMe），
//   所以批注层铺满屏幕后，岛这一面仍然点得到 —— 否则换一次笔就得先按 ESC 退出。
Item {
    id: face

    property var th: null
    property var bridge: null

    readonly property bool on: face.bridge ? (face.bridge.annotateActive === true) : false
    readonly property string toolName: face.bridge ? ("" + face.bridge.annotateTool)
                                                  : "pen"
    readonly property string pageText: face.bridge ? ("" + face.bridge.annotatePage) : "1/1"
    readonly property string hintText: face.bridge ? ("" + face.bridge.annotateHint) : ""

    readonly property var colors: [
        { hex: "#E53935", name: "红" },
        { hex: "#FDD835", name: "黄" },
        { hex: "#1E88E5", name: "蓝" },
        { hex: "#43A047", name: "绿" },
        { hex: "#FFFFFF", name: "白" }
    ]

    function act(cmd) {
        if (face.bridge && face.bridge.annotateAction)
            face.bridge.annotateAction(cmd)
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 10

        // ── 第一行：开关 + 工具 + 页 ─────────────────────────────
        Row {
            spacing: 8

            ActionButton {
                theme: face.th
                label: face.on ? "结束批注" : "开始批注"
                role: "primary"
                onClicked: face.act("toggle")
            }
            ActionButton {
                theme: face.th
                label: "笔"
                role: face.toolName === "pen" ? "primary" : "secondary"
                onClicked: face.act("pen")
            }
            ActionButton {
                theme: face.th
                label: "荧光笔"
                role: face.toolName === "highlighter" ? "primary" : "secondary"
                onClicked: face.act("highlighter")
            }
            ActionButton {
                theme: face.th
                label: "橡皮"
                role: face.toolName === "eraser" ? "primary" : "secondary"
                onClicked: face.act("eraser")
            }

            Item { width: 8; height: 1 }

            // 页码 + 翻页（批注页，不是 PPT 页）
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: face.pageText + " 页"
                color: face.th ? face.th.sub : "#9A9A9A"
                font.pixelSize: 12
            }
            ActionButton {
                theme: face.th
                label: "上一页"
                onClicked: face.act("prev")
            }
            ActionButton {
                theme: face.th
                label: "下一页"
                onClicked: face.act("next")
            }
        }

        // ── 第二行：颜色 + 撤销/清空 ─────────────────────────────
        Row {
            spacing: 8

            Repeater {
                model: face.colors
                Rectangle {
                    required property var modelData
                    width: 24
                    height: 24
                    radius: 12
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.hex
                    border.width: 1
                    // 描边沿用 ActionButton 的描边色（主题里没有专门的 line 令牌）
                    border.color: face.th && face.th.light ? "#D0D0D0" : "#3C3C3C"
                    MouseArea {
                        anchors.fill: parent
                        onClicked: face.act("color:" + modelData.hex)
                    }
                }
            }

            Item { width: 8; height: 1 }

            ActionButton {
                theme: face.th
                label: "撤销"
                onClicked: face.act("undo")
            }
            ActionButton {
                theme: face.th
                label: "清空"
                onClicked: face.act("clear")
            }
        }

        // ── 第三行：一句话状态 ───────────────────────────────────
        Text {
            width: parent.width
            text: face.hintText !== "" ? face.hintText
                                       : "点「开始批注」后可在屏幕任意处圈画，不抢课件键盘焦点"
            color: face.th ? face.th.sub : "#9A9A9A"
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
