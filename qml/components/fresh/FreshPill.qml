// 清新极简 · 状态 pill（fresh 组件族 · 2026-10-24 班级工作台方案 A）
//
// 用途：身份栏的订阅态、设备行的在线态等「一眼状态」。色板三档：
//   ok   绿底软色（frGreenSoft）+ 绿字 —— 已订阅 / 在线
//   wait 黄系软底 + 黄字 —— 订阅中 / 过渡态
//   off  灰软底 + 灰字 —— 未订阅 / 离线
// 高 24、全圆、字 labelM(12)。左侧可选 8px 状态点（调用方传 dot 色）。
import QtQuick
import "../theme"   // Theme 设计令牌单例（pragma Singleton）

Rectangle {
    id: root

    property string text: ""
    /** ok / wait / off（默认 off；颜色解析集中在此，消费方不持色值）。 */
    property string tone: "off"
    /** 状态点颜色（空 = 不画点；通常传 Md3.frGreen / Md3.cWarn / Md3.frOffDot）。 */
    property color dotColor: "transparent"
    /** 是否可点（静态状态 pill 不挂 MouseArea —— 点了没反馈比不能点更糟）。 */
    property bool interactive: false

    signal clicked()

    implicitWidth: pillRow.implicitWidth + 20
    implicitHeight: 24
    radius: Theme.radius.full || 999
    color: root.bgColor()

    function bgColor() {
        if (tone === "ok") return Theme.color.surfaceVariant || "#E9FBF0"
        if (tone === "wait") return "#F5E6C8"   // 黄软底（MD3 无 warn 容器令牌，字值随 fr 组）
        return Theme.color.surfaceVariant || "#EEF0F3"
    }
    function fgColor() {
        if (tone === "ok") return Theme.color.success || "#16A34A"
        if (tone === "wait") return Theme.color.warning || "#A87C0A"
        return Theme.color.textSecondary || "#6B7280"
    }

    Row {
        id: pillRow
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: root.dotColor !== "transparent" && root.dotColor !== ""
            width: 8
            height: 8
            radius: 4
            color: root.dotColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            font.pixelSize: 14 || 12
            font.weight: Font.Medium
            color: root.fgColor()
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: root.interactive
        onClicked: root.clicked()
    }
}
