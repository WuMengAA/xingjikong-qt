import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 时间轴甘特图：垂直时间线 + 可拖拽时间块
Rectangle {
    id: root
    color: "#1e1e1e"

    // 比例尺：每分钟对应的像素高度
    property real pixelsPerMinute: 2.0

    // 时间 -> Y 坐标（0 起点 = 午夜）
    function timeToY(totalMin) {
        return totalMin * root.pixelsPerMinute
    }
    // Y 坐标 -> 分钟数（自午夜）
    function yToMinutes(y) {
        return Math.max(0, y / root.pixelsPerMinute)
    }
    // 分钟数 -> "HH:mm"
    function minutesToText(totalMin) {
        const m = Math.floor(totalMin) % 1440
        const hh = String(Math.floor(m / 60)).padStart(2, '0')
        const mm = String(m % 60).padStart(2, '0')
        return hh + ":" + mm
    }
    // 吸附到 5 分钟
    function snapTo5(totalMin) {
        return Math.round(totalMin / 5) * 5
    }

    // 内容总高度：取最晚结束时间
    property real contentHeight: {
        var maxEnd = 480
        for (var i = 0; i < timeSlotModel.count; ++i) {
            var endMin = timeSlotModel.endMinAt(i)
            if (endMin > maxEnd) maxEnd = endMin
        }
        return maxEnd * root.pixelsPerMinute + 60
    }

    // 左侧时间刻度
    Rectangle {
        id: timeAxis
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 60
        color: "#252526"

        Text {
            anchors.centerIn: parent
            rotation: -90
            text: qsTr("时间轴")
            color: "#888"
            font.pixelSize: 12
        }

        Flickable {
            anchors.fill: parent
            contentHeight: root.contentHeight
            clip: true
            Repeater {
                model: 24  // 24 小时刻度
                Rectangle {
                    anchors.left: parent.left
                    y: index * (root.pixelsPerMinute * 60)
                    width: 60
                    height: root.pixelsPerMinute * 60
                    color: "transparent"
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(index).padStart(2, '0') + ":00"
                        color: "#666"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    // 时间块区域（可滚动）
    Flickable {
        id: blockFlick
        anchors.left: timeAxis.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        clip: true
        contentHeight: root.contentHeight

        Repeater {
            model: timeSlotModel
            delegate: TimeAxisBlock {
                width: blockFlick.width - 16
                x: 8
                timeAxisRef: root
                y: root.timeToY(timeSlotModel.startMinAt(index))
                height: Math.max(16, (timeSlotModel.endMinAt(index) - timeSlotModel.startMinAt(index)) * root.pixelsPerMinute)
            }
        }
    }
}
