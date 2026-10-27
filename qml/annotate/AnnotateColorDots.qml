// 底部工具条的六色笔（2026-10-27 用户点单：「显示颜色之类的应该在底部」）
//
// 为什么是六颗大圆点，而不是色盘/滑块：
//   讲台上一只手拿翻页笔、另一只手要立刻点到目标色。色盘要点准、滑块拖不准，
//   圆点是唯一"盲按也不会错"的形态；六个常用色也足够覆盖批注场景
//   （白笔是留给黑板/绿板用的，透明批注层上白笔看不见——但老师切到黑板上就会要它）。
import QtQuick

Row {
    id: dots

    property var canvas: null
    property int dotSize: 22
    spacing: 5

    readonly property var palette: [
        { hex: "#e53935" },   // 红（默认）
        { hex: "#fb8c00" },   // 橙
        { hex: "#fdd835" },   // 黄
        { hex: "#43a047" },   // 绿
        { hex: "#1e88e5" },   // 蓝
        { hex: "#ffffff" }    // 白（黑板/绿板用）
    ]
    // 以设备为准：岛和管理端都能改色，本地记一份必然显示错
    readonly property string cur: dots.canvas ? String(dots.canvas.colorHex) : ""

    Repeater {
        model: dots.palette
        delegate: Rectangle {
            required property var modelData

            width: dots.dotSize
            height: dots.dotSize
            radius: width / 2
            color: modelData.hex
            border.width: dots.cur === modelData.hex ? 2 : 0
            border.color: "#FFFFFF"
            scale: ma.containsMouse ? 1.12 : 1.0
            Behavior on scale { NumberAnimation { duration: 90 } }

            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton
                onClicked: { if (dots.canvas) dots.canvas.setColor(modelData.hex) }
            }
        }
    }
}
