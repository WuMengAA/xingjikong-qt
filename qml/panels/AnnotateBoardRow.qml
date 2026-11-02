// 批注面板 · 背景与清空（2026-10-27 · H 批）
//
// 拆出来的理由同 AnnotateToolRow（体积门禁 + 将来讲台浮动条要复用）。
//
// 口径：
// · 四档背景就是被控端 setBoard 认的四个值（""/white/black/green），
//   绿色用传统绿板色 #1B5E3A —— 与教室里的绿板一个观感。
// · 「清空」是**可撤销**的（Canvas::clearAll 会先压历史栈），所以不做二次确认弹窗：
//   讲台上多一次确认就多一次打断，而撤销就在旁边。
import QtQuick
import QtQuick.Layouts
import "../components"

RowLayout {
    id: row

    property var theme: null
    property string boardBg: ""      // "" = 透明批注层

    signal picked(string bg)
    signal undo()
    signal redo()
    signal clearAll()

    readonly property var boards: [
        { key: "",       label: "透明" },
        { key: "white",  label: "白板" },
        { key: "black",  label: "黑板" },
        { key: "green",  label: "绿板" }
    ]

    spacing: 8

    Text {
        text: "背景"
        color: row.theme ? row.theme.fg3 : "#CAC4D0"
        font.pixelSize: 12
    }

    Repeater {
        model: row.boards
        Rectangle {
            readonly property bool on: row.boardBg === modelData.key
            Layout.preferredWidth: bl.implicitWidth + 22
            Layout.preferredHeight: 26
            radius: 13
            color: on ? Qt.rgba(row.theme ? row.theme.acc.r : 0.4,
                                row.theme ? row.theme.acc.g : 0.3,
                                row.theme ? row.theme.acc.b : 0.85, 0.18)
                      : "transparent"
            border.width: 0
            border.color: on ? (row.theme ? row.theme.acc : "#FAFAFA")
                             : (row.theme ? row.theme.stroke2 : "#71717A")
            Text {
                id: bl
                anchors.centerIn: parent
                text: modelData.label
                color: parent.on ? (row.theme ? row.theme.acc : "#FAFAFA")
                                 : (row.theme ? row.theme.fg3 : "#CAC4D0")
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: row.picked(modelData.key)
            }
        }
    }

    Item { Layout.fillWidth: true }

    Btn {
        Layout.preferredWidth: 62
        theme: row.theme
        text: "撤销"
        tip: "也可以按 Ctrl+Z（在被控端上按）"
        onClicked: row.undo()
    }
    Btn {
        Layout.preferredWidth: 62
        theme: row.theme
        text: "重做"
        tip: "也可以按 Ctrl+Y"
        onClicked: row.redo()
    }
    Btn {
        Layout.preferredWidth: 62
        theme: row.theme
        text: "清空"
        tip: "清空当前页（可撤销）"
        onClicked: row.clearAll()
    }
}
