// MD3 开关行：label + 52×32 轨道开关。selected 轨道 primary、thumb onPrimary；
// 未选轨道 surfaceVariant + outline 描边、thumb outline。thumb 16→24 带 emphasized 缓动。
import QtQuick
import "../theme"

Item {
    id: root
    property string text: ""
    property bool checked: false
    property string supporting: ""
    signal toggled(bool value)

    implicitWidth: 320
    implicitHeight: supporting === "" ? 56 : 72

    // 单行：标签垂直居中；两行：标签居上（y:12）、说明在下（y:38）。
    // ⚠️ 曾经把标签写 anchors.verticalCenter + 说明 anchors.top —— 两行态直接重叠。
    Text {
        anchors.left: parent.left
        anchors.right: switchBox.left
        anchors.rightMargin: 16
        y: root.supporting === "" ? (parent.height - implicitHeight) / 2 : 12
        text: root.text
        font.pixelSize: Md3.bodyL
        color: Md3.cOnSurface
        elide: Text.ElideRight
    }
    Text {
        anchors.left: parent.left
        anchors.right: switchBox.left
        anchors.rightMargin: 16
        y: 38
        visible: root.supporting !== ""
        text: root.supporting
        font.pixelSize: Md3.bodyS
        color: Md3.cOnSurfaceVar
        elide: Text.ElideRight
    }
    Item {
        id: switchBox
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 52; height: 32
        Rectangle {
            anchors.fill: parent
            radius: 16
            color: root.checked ? Md3.cPrimary : Md3.cSurfaceVar
            border.width: root.checked ? 0 : 2
            border.color: Md3.cOutline
            Behavior on color { ColorAnimation { duration: Md3.durShort } }
        }
        Rectangle {
            x: root.checked ? 24 : 6
            anchors.verticalCenter: parent.verticalCenter
            width: root.checked ? 24 : 16
            height: width
            radius: width / 2
            color: root.checked ? Md3.cOnPrimary : Md3.cOutline
            Behavior on x { NumberAnimation { duration: Md3.durShort; easing.type: Easing.Bezier; easing.bezierCurve: Md3.easeEmph } }
            Behavior on width { NumberAnimation { duration: Md3.durShort; easing.type: Easing.Bezier; easing.bezierCurve: Md3.easeEmph } }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: { root.checked = !root.checked; root.toggled(root.checked) }
        }
    }
}
