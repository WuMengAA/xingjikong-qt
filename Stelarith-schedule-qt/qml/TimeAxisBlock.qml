import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: block
    color: model.TimeTypeRole === 0 ? "#0078d4" : "#555"
    radius: 4

    property real dragStartY: 0
    property real dragStartHeight: 0

    // 时间显示
    Text {
        id: timeText
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 8
        text: qsTr("%1 - %2").arg(model.StartTimeRole).arg(model.EndTimeRole)
        color: "#fff"
        font.pixelSize: 11
        font.bold: true
    }

    // 名称显示
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        text: qsTr("%1 (%2分钟)").arg(model.NameRole).arg(model.DurationMinutesRole)
        color: "#ddd"
        font.pixelSize: 10
    }

    // 顶部拖拽手柄
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 8
        color: "transparent"

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startY: 0
            property real startHeight: 0

            onPressed: (mouse) => {
                startY = mouse.y + parent.y
                startHeight = block.height
            }
            onPositionChanged: (mouse) => {
                var deltaY = mouse.y + parent.y - startY
                block.y += deltaY
                block.height = startHeight - deltaY
                // 更新 y 坐标
                if (block.y < 0) {
                    block.y = 0
                }
            }
            onReleased: () => {
                // 拖拽结束，触发模型更新
                // TODO: 调用 timeSlotModel.setSlotTime()
            }
        }
    }

    // 底部拖拽手柄
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 8
        color: "transparent"

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startY: 0
            property real startHeight: 0

            onPressed: (mouse) => {
                startY = mouse.y + parent.y
                startHeight = block.height
            }
            onPositionChanged: (mouse) => {
                var deltaY = mouse.y + parent.y - startY
                block.height += deltaY
                if (block.height < 20) {
                    block.height = 20
                }
            }
            onReleased: () => {
                // 拖拽结束，触发模型更新
                // TODO: 调用 timeSlotModel.setSlotTime()
            }
        }
    }

    // 悬停感知（先声明，供高亮块引用）
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }

    // 选中高亮
    Rectangle {
        anchors.fill: parent
        color: "#000"
        opacity: 0.3
        visible: hoverArea.hovered === true
    }
}
