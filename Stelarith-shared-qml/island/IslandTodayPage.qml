import QtQuick 2.15
import "."

// 灵动岛 · 今天看板（2026-11-01：提升功能密度 —— 今日课表全量，now/next 高亮）
//
// 数据：dash = bridge.todayDash（PanelDomain::build("timetable")）——
//   { week, nowTime, count, lessons:[{period,subject,start,end,state}], nowIndex, nextIndex }
//   state ∈ done / now / next / ""（未到）。
// 纯排版；依赖显式声明 property（隐式作用域不跟着搬进独立文件）。
Item {
    id: f

    property var th: null
    property var dash: ({})

    readonly property var lessons: (f.dash && f.dash.lessons) ? f.dash.lessons : []
    readonly property string weekText: (f.dash && f.dash.week) ? f.dash.week : ""
    readonly property string nowTime: (f.dash && f.dash.nowTime) ? f.dash.nowTime : ""
    readonly property color cFg: f.th ? f.th.fg : "#161616"
    readonly property color cSub: f.th ? f.th.sub : "#6E6E6E"
    readonly property color cAcc: f.th ? f.th.accent : "#6750A4"

    // ── 头部：今天 · 周次 …… 现在时刻 ──
    Item {
        id: head
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 18

        Text {
            id: hTitle
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "今天"
            color: f.cFg
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Text {
            anchors.left: hTitle.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: f.weekText
            color: f.cSub
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: f.nowTime
            color: f.cSub
            font.pixelSize: 11
            font.family: f.cFg === "#161616" ? "" : ""
        }
    }

    // ── 空态 ──
    Text {
        anchors.centerIn: parent
        visible: f.lessons.length === 0
        text: "今天没有课表"
        color: f.cSub
        font.pixelSize: 12
    }

    // ── 课表列表（纵向滚动；外层分页器是横向，方向不同不打架）──
    Flickable {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: head.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: col.height
        boundsBehavior: Flickable.StopAtBounds
        visible: f.lessons.length > 0

        Column {
            id: col
            width: list.width
            spacing: 2
            Repeater {
                model: f.lessons
                delegate: Rectangle {
                    required property var modelData
                    width: col.width
                    height: 28
                    radius: 6
                    property bool isNow: modelData.state === "now"
                    property bool isNext: modelData.state === "next"
                    property bool isDone: modelData.state === "done"
                    color: isNow
                           ? (f.th ? (f.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.09))
                                   : Qt.rgba(0.4, 0.4, 0.4, 0.12))
                           : Qt.rgba(0, 0, 0, 0)

                    // 左侧「现在」竖条
                    Rectangle {
                        visible: isNow
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        width: 3
                        radius: 1.5
                        color: f.cAcc
                    }
                    // 节次
                    Text {
                        id: periodT
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34
                        text: modelData.period
                        color: parent.isDone ? f.cSub : f.cSub
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    // 科目
                    Text {
                        anchors.left: periodT.right
                        anchors.leftMargin: 6
                        anchors.right: timeT.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.subject
                        color: parent.isNow ? f.cAcc : (parent.isDone ? f.cSub : f.cFg)
                        font.pixelSize: 13
                        font.weight: (parent.isNow || parent.isNext) ? Font.Bold : Font.Medium
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                    // 时间
                    Text {
                        id: timeT
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.start + "–" + modelData.end
                        color: f.cSub
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
