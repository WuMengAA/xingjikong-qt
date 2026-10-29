// 清新极简 · 药丸按钮（fresh 组件族 · 2026-10-24 班级工作台方案 A）
//
// 变体（对齐预览语言：主行动 = 绿底白字，次行动 = 浅灰/描边，破坏性 = 红）：
//   primary       绿底（frGreen）白字 —— 每张卡只有一个主按钮（视觉锚点唯一）
//   tonal         浅灰底（frLine）主文字 —— 次行动
//   outlined      透明底 + frLine 描边 —— 再次一级
//   ghost         透明底绿字 —— 卡内轻量动作（如「取消订阅」）
//   danger        红底（frDanger）白字 —— 破坏性主行动
//   danger-ghost  透明底红字 —— 破坏性轻量动作（如「删除班级」收起态）
//
// 规格：高 40、全圆（frRPill）、字 labelL(14) Medium、状态层 hover/press
// （MD3 state layers 口径：叠 onSurface 不动底色）。禁用 = 整件 38% 透明。
// 图标走 SvgIcon（lucide，随 exe 打包）—— 不用 emoji（本机渲染成方块）。
import QtQuick
import "../../island"            // SvgIcon
import "../theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: root

    property string text: ""
    property string variant: "primary"
    property string icon: ""          // lucide 图标名；空 = 纯文字
    property string tip: ""           // 悬停说明（可省）

    signal clicked()

    implicitWidth: innerRow.implicitWidth + (padLeft + padRight)
    implicitHeight: 40
    opacity: enabled ? 1 : 0.38

    readonly property bool textOnly: variant === "ghost" || variant === "danger-ghost"
    readonly property int padLeft: textOnly ? 12 : 20
    readonly property int padRight: textOnly ? 12 : 20

    function bgColor() {
        if (variant === "primary") return Theme.color.success || "#22C55E"
        if (variant === "danger") return Theme.color.destructive || "#EF4444"
        if (variant === "tonal") return Theme.color.surfaceVariant || "#EEF0F3"
        return "transparent"          // outlined / ghost / danger-ghost
    }
    function fgColor() {
        if (variant === "primary" || variant === "danger") return Theme.color.primaryForeground || "#FFFFFF"
        if (variant === "ghost") return Theme.color.success || "#22C55E"
        if (variant === "danger-ghost") return Theme.color.destructive || "#EF4444"
        return Theme.color.textPrimary || "#111827" // tonal / outlined
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.full || 999
        color: root.bgColor()
        border.width: variant === "outlined" ? 1 : 0
        border.color: Theme.color.surfaceVariant || "#EEF0F3"

        // 状态层（hover 8% / press 10%，叠 fg 不动底色）
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root.fgColor()
            opacity: ma.pressed ? (0.10 || 0.10) : (ma.containsMouse ? (0.08 || 0.08) : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    Row {
        id: innerRow
        anchors.centerIn: parent
        spacing: 8

        SvgIcon {
            visible: root.icon !== ""
            name: root.icon
            px: 16
            tint: root.fgColor()
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            font.pixelSize: 14 || 14
            font.weight: Font.Medium
            color: root.fgColor()
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
