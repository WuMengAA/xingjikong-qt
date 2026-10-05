import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#1e1e1e"

    // 比例尺：每分钟对应的像素高度
    property real pixelsPerMinute: 2.0

    // 总高度（基于最晚结束时间）
    property real totalHeight: 480 * pixelsPerMinute  // 假设 480 分钟覆盖一整天

    // 左侧时间轴
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

        Repeater {
            model: 24  // 24 小时
            Rectangle {
                anchors.left: parent.left
                y: index * (pixelsPerMinute * 60)
                width: 60
                height: pixelsPerMinute * 60
                color: "#252526"
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

    // 时间块列表
    ListView {
        id: blockList
        anchors.left: timeAxis.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 8
        anchors.bottomMargin: 8

        model: timeSlotModel
        clip: true

        delegate: TimeAxisBlock {
            width: blockList.width
            y: model.StartSecsRole / (60 * 60) * (pixelsPerMinute * 60) - (pixelsPerMinute * 60)  // 转换为相对 y 坐标
            height: (model.DurationMinutesRole) * pixelsPerMinute
        }
    }
}
