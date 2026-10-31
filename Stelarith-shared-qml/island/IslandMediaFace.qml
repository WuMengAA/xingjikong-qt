import QtQuick 2.15
import "."

// 灵动岛 · 媒体面（2026-11-01 从 IslandRoot 内联抽出，供分页器做独立页）
//
// 纯排版：数据全由调用方传入（root 的 face* 属性 / faceTimeText()）。
// ⚠️ 依赖显式声明成 property —— 隐式作用域不跟着搬进独立文件（D 档拆分踩过）。
Item {
    id: f

    property var th: null
    property bool useLocal: true
    property string title: ""
    property string artist: ""
    property real dur: 0
    property real pos: 0
    property string timeText: ""
    property string next: ""

    SvgIcon {
        id: icon
        name: "music"
        tint: f.th ? f.th.accent : "#6750A4"
        px: 18
        anchors.left: parent.left
        anchors.top: parent.top
    }
    Text {
        anchors.left: icon.right
        anchors.leftMargin: 6
        anchors.verticalCenter: icon.verticalCenter
        text: f.useLocal ? "本机播放" : "VoiceHub"
        color: f.th ? f.th.sub : "#6E6E6E"
        font.pixelSize: 10
    }
    Text {
        id: titleT
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: icon.bottom
        anchors.topMargin: 6
        text: f.title || "正在播放"
        color: f.th ? f.th.fg : "#161616"
        font.pixelSize: 19
        font.weight: Font.Bold
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.WrapAnywhere
    }
    Text {
        id: artistT
        visible: f.artist !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleT.bottom
        anchors.topMargin: 2
        text: f.artist
        color: f.th ? f.th.sub : "#6E6E6E"
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: timeT.top
        anchors.bottomMargin: 4
        height: 4
        radius: 2
        color: f.th ? (f.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14))
                    : Qt.rgba(0, 0, 0, 0.10)
        Rectangle {
            width: f.dur > 0 ? Math.max(0, Math.min(1, f.pos / f.dur)) * parent.width : 0
            height: parent.height
            radius: 2
            color: f.th ? f.th.accent : "#6750A4"
        }
    }
    Text {
        id: timeT
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: nextT.visible ? nextT.top : parent.bottom
        anchors.bottomMargin: nextT.visible ? 2 : 0
        text: f.timeText
        color: f.th ? f.th.sub : "#6E6E6E"
        font.pixelSize: 11
    }
    Text {
        id: nextT
        visible: f.next !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 44
        anchors.bottom: parent.bottom
        text: "接下来：" + f.next
        color: f.th ? f.th.sub : "#6E6E6E"
        font.pixelSize: 11
        elide: Text.ElideRight
    }
}
