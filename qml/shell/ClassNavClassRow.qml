// 班级导航 · 班级行（ClassNav 的子件）
//
// 抽出来是因为 ClassNav 自己已经 280+ 行（门禁 200）。本件只管"一行班级长什么样"：
// 班名 + 在线/总数 + 选中态（工业青左规 + secondaryContainer 底）。
// 计数由调用方算好传入（countOf 在 ClassNav，那里才有 devices）。
import QtQuick
import "../components/md3"
import "../components/theme"   // Md3 令牌（pragma Singleton）

Rectangle {
    id: row

    property string code: ""
    property string name: ""
    property int onCount: 0
    property int totalCount: 0
    property bool active: false

    signal clicked()

    readonly property int rowH: 44
    readonly property int padX: Md3.spM

    height: rowH
    radius: Md3.rM
    color: active
           ? (Md3.cSecondaryCont)
           : (ma.containsMouse ? (Md3.cContHigh) : "transparent")
    Behavior on color { ColorAnimation { duration: Md3.durShort} }

    // 选中态：终末地工业青左规（与 Sidebar 同一语言）
    Rectangle {
        visible: row.active
        width: 3
        radius: 1.5
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        color: Md3.cAccent
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: row.padX
        anchors.right: cnt.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: row.name !== "" ? row.name : row.code
        elide: Text.ElideRight
        font.pixelSize: Md3.body13
        font.weight: row.active ? Font.Medium : Font.Normal
        color: row.active
               ? (Md3.cOnSecondaryCont)
               : (Md3.cOnSurface)
    }

    // 人数读数：等宽字（技术读数），有在线才染色，否则灰
    Text {
        id: cnt
        anchors.right: parent.right
        anchors.rightMargin: row.padX
        anchors.verticalCenter: parent.verticalCenter
        text: row.onCount + "/" + row.totalCount
        font.pixelSize: Md3.labelS
        font.family: "monospace"
        color: row.onCount > 0
               ? (Md3.cOk)
               : (Md3.cOutline)
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
