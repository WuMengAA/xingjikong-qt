// MD3 单选：20 圆，选中 primary 描边 + 10 实心点；未选 outline 描边。
import QtQuick
import "../theme"

Item {
    id: root
    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 20; implicitHeight: 20

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: "transparent"
        border.width: 2
        border.color: root.checked ? Md3.cPrimary : Md3.cOutline
        Behavior on border.color { ColorAnimation { duration: Md3.durShort } }
        Rectangle {
            anchors.centerIn: parent
            width: root.checked ? 10 : 0
            height: width
            radius: width / 2
            color: Md3.cPrimary
            Behavior on width { NumberAnimation { duration: Md3.durShort; easing.type: Easing.Bezier; easing.bezierCurve: Md3.easeEmph } }
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: { root.checked = !root.checked; root.toggled(root.checked) }
    }
}
