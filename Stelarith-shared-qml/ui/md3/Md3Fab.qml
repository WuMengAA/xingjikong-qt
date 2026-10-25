// MD3 FAB：regular 56×56 / mini 40×40；label 非空时自动 extended。
// 规范：primaryContainer 底 + onPrimaryContainer 前景，圆角 16。
import QtQuick
import "../theme"

Item {
    id: root
    property string icon: "✏️"
    property string label: ""
    property string size: "regular"   // regular | mini
    signal clicked()

    implicitWidth: label === "" ? (size === "mini" ? 40 : 56) : fabRow.implicitWidth + 32
    implicitHeight: size === "mini" ? 40 : 56

    Rectangle {
        anchors.fill: parent
        radius: Md3.rL
        color: Md3.cPrimaryCont
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Md3.cOnPrimaryCont
            opacity: ma.pressed ? Md3.stPress : (ma.containsMouse ? Md3.stHover : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Row {
            id: fabRow
            anchors.centerIn: parent
            spacing: 8
            Text {
                visible: root.icon !== ""
                text: root.icon
                font.pixelSize: root.size === "mini" ? 18 : 24
                color: Md3.cOnPrimaryCont
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: root.label !== ""
                text: root.label
                font.pixelSize: Md3.labelL
                font.weight: Font.Medium
                color: Md3.cOnPrimaryCont
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
