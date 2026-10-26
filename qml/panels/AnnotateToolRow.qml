// 批注面板 · 笔（2026-10-27 · H 批）
//
// 拆出来的理由：AnnotateDialog 撞 QML 单文件 200 行红线；而且"工具 + 颜色 + 粗细"
// 这三件事后面要复用到白板工具条上（讲台上的浮动条），独立成件才好复用。
//
// 口径：
// · 工具只有三种（笔 / 荧光笔 / 橡皮）—— 与 annotate_domain.h 的 Canvas::setTool 白名单
//   逐字对应，多一个都不给（给了也是被被控端拒绝，界面上就是"点了没反应"）。
// · 颜色是**即时生效**的：点色块 = 换成这支笔的颜色，不需要确定。
// · 粗细用三档而不是滑块：讲台上没人在意 4.5px 和 5px 的区别，但会在意"点得准不准"。
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: row

    property var theme: null
    property string tool: "pen"
    property string penColor: "#E53935"
    property int penWidth: 4

    signal pickedTool(string name)
    signal pickedColor(string hex)
    signal pickedWidth(int w)

    readonly property var tools: [
        { key: "pen",         label: "笔" },
        { key: "highlighter", label: "荧光笔" },
        { key: "eraser",      label: "橡皮" }
    ]
    readonly property var colors: [
        { hex: "#E53935", name: "红" },
        { hex: "#FDD835", name: "黄" },
        { hex: "#1E88E5", name: "蓝" },
        { hex: "#43A047", name: "绿" },
        { hex: "#FFFFFF", name: "白" },
        { hex: "#212121", name: "黑" }
    ]
    readonly property var widths: [
        { w: 2, label: "细" },
        { w: 4, label: "中" },
        { w: 8, label: "粗" }
    ]

    spacing: 8

    // ── 工具三档 ──────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            text: "笔"
            color: row.theme ? row.theme.fg3 : "#CAC4D0"
            font.pixelSize: 12
        }

        Repeater {
            model: row.tools
            Rectangle {
                readonly property bool on: row.tool === modelData.key
                Layout.preferredWidth: lbl.implicitWidth + 24
                Layout.preferredHeight: 28
                radius: 14
                color: on ? Qt.rgba(row.theme ? row.theme.acc.r : 0.4,
                                    row.theme ? row.theme.acc.g : 0.3,
                                    row.theme ? row.theme.acc.b : 0.85, 0.18)
                          : "transparent"
                border.width: 1
                border.color: on ? (row.theme ? row.theme.acc : "#D0BCFF")
                                 : (row.theme ? row.theme.stroke2 : "#79747E")
                Text {
                    id: lbl
                    anchors.centerIn: parent
                    text: modelData.label
                    color: parent.on ? (row.theme ? row.theme.acc : "#D0BCFF")
                                     : (row.theme ? row.theme.fg3 : "#CAC4D0")
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: row.pickedTool(modelData.key)
                }
            }
        }

        Item { Layout.fillWidth: true }

        // ── 粗细三档 ──────────────────────────────────────────────
        Repeater {
            model: row.widths
            Rectangle {
                readonly property bool on: row.penWidth === modelData.w
                Layout.preferredWidth: 34
                Layout.preferredHeight: 28
                radius: 14
                color: on ? Qt.rgba(row.theme ? row.theme.acc.r : 0.4,
                                    row.theme ? row.theme.acc.g : 0.3,
                                    row.theme ? row.theme.acc.b : 0.85, 0.18)
                          : "transparent"
                border.width: 1
                border.color: on ? (row.theme ? row.theme.acc : "#D0BCFF")
                                 : (row.theme ? row.theme.stroke2 : "#79747E")
                // 用点的大小表示粗细，比写"细/中/粗"一眼看懂
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.max(3, modelData.w * 1.4)
                    height: width
                    radius: width / 2
                    color: row.theme ? row.theme.fg : "#E6E1E5"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: row.pickedWidth(modelData.w)
                }
            }
        }
    }

    // ── 颜色 ──────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: "颜色"
            color: row.theme ? row.theme.fg3 : "#CAC4D0"
            font.pixelSize: 12
        }

        Repeater {
            model: row.colors
            Rectangle {
                readonly property bool on: row.penColor.toUpperCase() === modelData.hex.toUpperCase()
                Layout.preferredWidth: on ? 26 : 22
                Layout.preferredHeight: on ? 26 : 22
                Layout.alignment: Qt.AlignVCenter
                radius: width / 2
                color: modelData.hex
                // 选中的用主题色描边；白笔在黑底上也能看见
                border.width: on ? 2 : 1
                border.color: on ? (row.theme ? row.theme.acc : "#D0BCFF")
                                 : (row.theme ? row.theme.stroke2 : "#79747E")
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 120 } }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: row.pickedColor(modelData.hex)
                }
            }
        }

        Item { Layout.fillWidth: true }
    }
}
