// MD3 列表项：leading 图标（emoji 占位）+ headline + supporting +
// trailing（文字 / 开关 / 无）。单行 56、双行 72；可选底部分隔线。
import QtQuick
import "../theme"

Item {
    id: root
    property string icon: ""
    property string headline: ""
    property string supporting: ""
    property string trailingText: ""
    property bool trailingSwitch: false
    property bool checked: false
    property bool divider: true
    signal clicked()
    signal toggled(bool value)

    implicitWidth: 360
    implicitHeight: supporting === "" ? 56 : 72

    Rectangle {
        anchors.fill: parent
        color: ma.pressed ? Md3.cContHigh : (ma.containsMouse ? Md3.cContLow : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    // leading 图标容器（40×40 圆 20，secondaryContainer）
    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        visible: root.icon !== ""
        width: 40; height: 40; radius: 20
        color: Md3.cSecondaryCont
        Text {
            anchors.centerIn: parent
            text: root.icon
            font.pixelSize: 18
        }
    }
    Text {
        anchors.left: root.icon !== "" ? parent.left : parent.left
        anchors.leftMargin: root.icon !== "" ? 72 : 16
        anchors.right: (root.trailingText !== "" || root.trailingSwitch) ? trailCol.left : parent.right
        anchors.rightMargin: (root.trailingText !== "" || root.trailingSwitch) ? 16 : 16
        anchors.verticalCenter: root.supporting === "" ? parent.verticalCenter : undefined
        y: root.supporting === "" ? 0 : 12
        text: root.headline
        font.pixelSize: Md3.bodyL
        color: Md3.cOnSurface
        elide: Text.ElideRight
    }
    Text {
        anchors.left: root.icon !== "" ? parent.left : parent.left
        anchors.leftMargin: root.icon !== "" ? 72 : 16
        anchors.right: (root.trailingText !== "" || root.trailingSwitch) ? trailCol.left : parent.right
        anchors.rightMargin: (root.trailingText !== "" || root.trailingSwitch) ? 16 : 16
        anchors.top: parent.top
        anchors.topMargin: 36
        visible: root.supporting !== ""
        text: root.supporting
        font.pixelSize: Md3.bodyS
        color: Md3.cOnSurfaceVar
        elide: Text.ElideRight
    }
    Item {
        id: trailCol
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: root.trailingSwitch ? 52 : trailTxt.implicitWidth
        height: root.trailingSwitch ? 32 : trailTxt.implicitHeight
        Text {
            id: trailTxt
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.trailingText !== ""
            text: root.trailingText
            font.pixelSize: Md3.labelM
            color: Md3.cPrimary
        }
        // trailing 开关（与 Md3SwitchRow 同规范）
        Rectangle {
            anchors.fill: parent
            visible: root.trailingSwitch
            radius: 16
            color: root.checked ? Md3.cPrimary : Md3.cSurfaceVar
            border.width: root.checked ? 0 : 2
            border.color: Md3.cOutline
            Behavior on color { ColorAnimation { duration: Md3.durShort } }
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
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.divider
        height: 1
        color: Md3.cOutlineVar
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: !root.trailingSwitch
        onClicked: root.clicked()
    }
}
