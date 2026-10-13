// MD3 Chip（filter 变体）：高 32、圆角 8。选中 secondaryContainer + 前导 ✓；
// 未选 outline 描边 + onSurfaceVariant 文字。
import QtQuick

Item {
    id: root
    property var md3: ({})
    property string text: ""
    property bool selected: false
    signal clicked()

    implicitWidth: innerRow.implicitWidth + 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: md3.rS
        color: root.selected ? md3.cSecondaryCont : "transparent"
        border.width: root.selected ? 0 : 1
        border.color: md3.cOutline
        Behavior on color { ColorAnimation { duration: md3.durShort } }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: md3.cOnSurface
            opacity: ma.pressed ? md3.stPress : (ma.containsMouse ? md3.stHover : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }
    Row {
        id: innerRow
        anchors.centerIn: parent
        spacing: 6
        Text {
            visible: root.selected
            text: "✓"
            font.pixelSize: 14
            color: md3.cOnSecondaryCont
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            font.pixelSize: md3.labelL
            font.weight: Font.Medium
            color: root.selected ? md3.cOnSecondaryCont : md3.cOnSurfaceVar
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
