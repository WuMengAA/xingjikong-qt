//
// 内部规格升到 Material Design 3：开关 52×32 轨道、thumb 16→24 emphasized 缓动、
// 开=primary 轨道 + onPrimary thumb、关=surfaceVariant 轨道 + outline 描边；
// 文字 bodyM(14)/cOnSurface，说明 bodyS(12)/cOnSurfaceVar。
// **调用方 API 一字未改**（theme/text/note/checked/toggled）——设置页 6 处零改动。
import QtQuick
import QtQuick.Controls

Item {
    id: row
    property var theme: ({ })
    property alias text: label.text
    property alias note: note.text
    property bool checked
    signal toggled(bool on)

    height: row.note === "" ? 56 : 72

    Text {
        id: label
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: 16
        y: row.note === "" ? (parent.height - implicitHeight) / 2 : 12
        text: row.text
        color: row.theme && row.theme.fg !== undefined ? row.theme.fg : "#1C1B1F"
        font.pixelSize: row.theme && row.theme.bodyM !== undefined ? row.theme.bodyM : 14
        elide: Text.ElideRight
    }

    Text {
        id: note
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: 16
        y: 38
        visible: text !== ""
        text: row.note
        color: row.theme && row.theme.fg4 !== undefined ? row.theme.fg4 : "#49454F"
        font.pixelSize: row.theme && row.theme.bodyS !== undefined ? row.theme.bodyS : 12
        elide: Text.ElideRight
    }

    Item {
        id: sw
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 52; height: 32

        Rectangle {
            anchors.fill: parent
            radius: 16
            color: row.checked ? (row.theme.acc || "#18181B") : (row.theme.seg || "#F4F4F5")
            // 无边框：开关轨道未选态靠 track 底色区分
            Behavior on color { ColorAnimation { duration: 200 } }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "#FFFFFF"
                opacity: swMa.pressed ? 0.10 : (swMa.containsMouse ? 0.08 : 0)
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        Rectangle {
            x: row.checked ? 24 : 6
            anchors.verticalCenter: parent.verticalCenter
            width: row.checked ? 24 : 16
            height: width
            radius: width / 2
            color: row.checked ? (row.theme.win || "#FFFFFF") : (row.theme.fg4 || "#71717A")
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.Bezier; easing.bezierCurve: [0.05, 0.7, 0.1, 1.0] } }
            Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.Bezier; easing.bezierCurve: [0.05, 0.7, 0.1, 1.0] } }
        }
        MouseArea {
            id: swMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                row.checked = !row.checked
                row.toggled(row.checked)
            }
        }
    }
}
