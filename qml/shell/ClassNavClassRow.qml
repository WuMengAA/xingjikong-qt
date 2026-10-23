// 班级导航 · 班级行（ClassNav 的子件）
//
// 抽出来是因为 ClassNav 自己已经 280+ 行（门禁 200）。本件只管"一行班级长什么样"：
// 班名 + 在线/总数 + 选中态（工业青左规 + secondaryContainer 底）。
// 计数由调用方算好传入（countOf 在 ClassNav，那里才有 devices）。
import QtQuick
import "../components/md3"

Rectangle {
    id: row

    property var md3: ({})
    property string code: ""
    property string name: ""
    property int onCount: 0
    property int totalCount: 0
    property bool active: false

    signal clicked()

    readonly property int rowH: 44
    readonly property int padX: md3 ? md3.spM : 16

    height: rowH
    radius: md3 ? md3.rM : 12
    color: active
           ? (md3 ? md3.cSecondaryCont : "#E8DEF8")
           : (ma.containsMouse ? (md3 ? md3.cContHigh : "#ECE6F0") : "transparent")
    Behavior on color { ColorAnimation { duration: md3 ? md3.durShort : 200 } }

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
        color: md3 ? md3.cAccent : "#2FB8C6"
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: row.padX
        anchors.right: cnt.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: row.name !== "" ? row.name : row.code
        elide: Text.ElideRight
        font.pixelSize: md3 ? md3.body13 : 13
        font.weight: row.active ? Font.Medium : Font.Normal
        color: row.active
               ? (md3 ? md3.cOnSecondaryCont : "#1D192B")
               : (md3 ? md3.cOnSurface : "#1C1B1F")
    }

    // 人数读数：等宽字（技术读数），有在线才染色，否则灰
    Text {
        id: cnt
        anchors.right: parent.right
        anchors.rightMargin: row.padX
        anchors.verticalCenter: parent.verticalCenter
        text: row.onCount + "/" + row.totalCount
        font.pixelSize: md3 ? md3.labelS : 11
        font.family: "monospace"
        color: row.onCount > 0
               ? (md3 ? md3.cOk : "#1B7A43")
               : (md3 ? md3.cOutline : "#79747E")
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
