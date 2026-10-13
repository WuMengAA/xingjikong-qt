// MD3 FAB：regular 56×56 / mini 40×40；label 非空时自动 extended。
// 规范：primaryContainer 底 + onPrimaryContainer 前景，圆角 16。
import QtQuick

Item {
    id: root
    property var md3: ({})
    property string icon: "✏️"
    property string label: ""
    property string size: "regular"   // regular | mini
    signal clicked()

    implicitWidth: label === "" ? (size === "mini" ? 40 : 56) : fabRow.implicitWidth + 32
    implicitHeight: size === "mini" ? 40 : 56

    Rectangle {
        anchors.fill: parent
        radius: md3.rL
        color: md3.cPrimaryCont
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: md3.cOnPrimaryCont
            opacity: ma.pressed ? md3.stPress : (ma.containsMouse ? md3.stHover : 0)
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
                color: md3.cOnPrimaryCont
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: root.label !== ""
                text: root.label
                font.pixelSize: md3.labelL
                font.weight: Font.Medium
                color: md3.cOnPrimaryCont
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
