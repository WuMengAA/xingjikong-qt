// 星集控 · 开关行
//
// 设置页那两个「提醒」以前是**画**的：两个空心的圆，点了没反应，看着像做了一半。
// 2026-10-06 换成真开关，状态由后端（ViewerBackend + QSettings）管，重开程序还在。
//
// 视觉规矩：开关本体 36×20，圆点在 14 直径的圆角方里走，开=反白、关=描边空。
// 文字在左，说明小字在下方 —— 说明不跟着右边走，否则窄面板上一行会挤成三行。
import QtQuick
import QtQuick.Controls

Item {
    id: row
    property var theme: ({ })
    property alias text: label.text
    property alias note: note.text
    property bool checked
    signal toggled(bool on)

    height: row.note === "" ? 26 : 42

    Text {
        id: label
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 5
        text: row.text
        color: row.theme && row.theme.fg !== undefined ? row.theme.fg : "#FAFAFA"
        font.pixelSize: 12
    }

    Text {
        id: note
        anchors.left: parent.left
        anchors.top: label.bottom
        visible: text !== ""
        text: row.note
        color: row.theme && row.theme.fg4 !== undefined ? row.theme.fg4 : "#3A3A3A"
        font.pixelSize: 11
    }

    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 20; radius: 10
        color: row.checked ? (row.theme.inv || "#F0F0F0") : "transparent"
        border.color: row.checked ? (row.theme.inv || "#F0F0F0")
                                  : (row.theme.stroke || "#242424")
        border.width: 1

        Rectangle {
            id: knob
            width: 14; height: 14; radius: 4
            x: row.checked ? track.width - knob.width - 3 : 3
            y: 2
            color: row.checked ? (row.theme.win || "#0A0A0A") : (row.theme.fg4 || "#3A3A3A")
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                row.checked = !row.checked
                row.toggled(row.checked)
            }
        }
    }
}
