// 班级导航 · 系统行（ClassNav 的子件）
//
// 固定三项（仪表盘 / 运行监控 / 系统设置），与 Sidebar 的菜单项视觉一致：
// 高 56、全圆选中底、图标走 SvgIcon（不用 emoji，本机会渲染成方块）。
import QtQuick
import "../components/md3"
import "../island"          // SvgIcon
import "../components/theme"   // Md3 令牌（pragma Singleton）

Rectangle {
    id: row

    property string icon: ""
    property string label: ""
    property bool active: false

    signal clicked()

    readonly property int rowH: 56
    readonly property int padX: Md3.spM
    readonly property int iconPx: 20

    height: rowH
    radius: Md3.rFull
    color: active
           ? (Md3.cSecondaryCont)
           : (ma.containsMouse ? (Md3.cContHigh) : "transparent")
    Behavior on color { ColorAnimation { duration: Md3.durShort} }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: row.padX
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        SvgIcon {
            name: row.icon
            px: row.iconPx
            tint: row.active
                  ? (Md3.cOnSecondaryCont)
                  : (Md3.cOnSurfaceVar)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: row.label
            font.pixelSize: Md3.bodyM
            font.weight: row.active ? Font.Medium : Font.Normal
            color: row.active
                   ? (Md3.cOnSecondaryCont)
                   : (Md3.cOnSurfaceVar)
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
