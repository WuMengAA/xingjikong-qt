// 班级导航 · 系统行（ClassNav 的子件）
//
// 固定三项（仪表盘 / 运行监控 / 系统设置），与 Sidebar 的菜单项视觉一致：
// 高 56、全圆选中底、图标走 SvgIcon（不用 emoji，本机会渲染成方块）。
import QtQuick
import "../components/md3"
import "../island"          // SvgIcon

Rectangle {
    id: row

    property var md3: ({})
    property string icon: ""
    property string label: ""
    property bool active: false

    signal clicked()

    readonly property int rowH: 56
    readonly property int padX: md3 ? md3.spM : 16
    readonly property int iconPx: 20

    height: rowH
    radius: md3 ? md3.rFull : 999
    color: active
           ? (md3 ? md3.cSecondaryCont : "#E8DEF8")
           : (ma.containsMouse ? (md3 ? md3.cContHigh : "#ECE6F0") : "transparent")
    Behavior on color { ColorAnimation { duration: md3 ? md3.durShort : 200 } }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: row.padX
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        SvgIcon {
            name: row.icon
            px: row.iconPx
            tint: row.active
                  ? (md3 ? md3.cOnSecondaryCont : "#1D192B")
                  : (md3 ? md3.cOnSurfaceVar : "#49454F")
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: row.label
            font.pixelSize: md3 ? md3.bodyM : 14
            font.weight: row.active ? Font.Medium : Font.Normal
            color: row.active
                   ? (md3 ? md3.cOnSecondaryCont : "#1D192B")
                   : (md3 ? md3.cOnSurfaceVar : "#49454F")
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
