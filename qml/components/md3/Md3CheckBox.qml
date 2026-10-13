// MD3 复选框：18×18 圆角 2，选中 primary 底 + ✓；未选 outline 描边。
import QtQuick

Item {
    id: root
    property var md3: ({})
    property bool checked: false
    signal toggled(bool value)

    implicitWidth: 18; implicitHeight: 18
    opacity: ma.pressed ? 0.9 : 1

    Rectangle {
        anchors.fill: parent
        radius: 2
        color: root.checked ? md3.cPrimary : "transparent"
        border.width: root.checked ? 0 : 2
        border.color: md3.cOutline
        Behavior on color { ColorAnimation { duration: md3.durShort } }
        Text {
            anchors.centerIn: parent
            visible: root.checked
            text: "✓"
            font.pixelSize: 12
            font.weight: Font.Bold
            color: md3.cOnPrimary
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        onClicked: { root.checked = !root.checked; root.toggled(root.checked) }
    }
}
