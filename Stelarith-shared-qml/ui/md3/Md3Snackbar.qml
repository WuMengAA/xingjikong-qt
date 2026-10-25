// MD3 Snackbar：inverseSurface 底、圆角 4、高 48，可选 action 按钮，4s 自动消失。
import QtQuick
import "../theme"

Item {
    id: root
    property alias text: snackText.text
    property string actionText: ""
    property bool opened: false
    signal actionClicked()

    visible: opened
    implicitWidth: snackRow.implicitWidth + 32
    implicitHeight: 48

    Timer {
        id: autoHide
        interval: 4000
        onTriggered: root.opened = false
    }
    function show(t, act) {
        snackText.text = t
        root.actionText = (act === undefined) ? "" : act
        root.opened = true
        autoHide.restart()
    }
    function hide() { root.opened = false }

    Rectangle {
        anchors.fill: parent
        radius: Md3.rXS
        color: Md3.cInverseSurf
        Row {
            id: snackRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            spacing: 8
            Text {
                id: snackText
                font.pixelSize: Md3.bodyM
                color: Md3.cInverseOn
                maximumLineCount: 2
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 480)
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: root.actionText !== ""
                text: root.actionText
                font.pixelSize: Md3.labelL
                font.weight: Font.Medium
                color: Md3.cPrimaryCont
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    onClicked: { root.actionClicked(); root.opened = false }
                }
            }
        }
    }
}
