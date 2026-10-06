import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 时间块：显示时间段，顶部/底部手柄可拖拽调整时长（松手写回模型）
Rectangle {
    id: block
    color: blockType === 0 ? "#0078d4" : "#555"
    radius: 4

    // 由 TimeAxis 传入
    required property int blockIndex
    required property var timeAxisRef
    required property string blockName
    required property int blockStartMin
    required property int blockEndMin
    required property int blockType

    // 当前时间段（分钟自午夜，随模型更新）
    readonly property int curStartMin: timeSlotModel.startMinAt(blockIndex)
    readonly property int curEndMin: timeSlotModel.endMinAt(blockIndex)

    // 显示用临时值（拖拽中更新）
    property int dragStartMin: blockStartMin
    property int dragEndMin: blockEndMin

    // 时间显示
    Text {
        id: timeText
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 8
        text: qsTr("%1 - %2").arg(timeAxisRef.minutesToText(block.dragStartMin)).arg(timeAxisRef.minutesToText(block.dragEndMin))
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
        text: qsTr("%1 (%2分钟)").arg(block.blockName).arg(Math.round(block.dragEndMin - block.dragStartMin))
        color: "#ddd"
        font.pixelSize: 10
    }

    // 顶部拖拽手柄：改开始时间
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 10
        color: "transparent"
        visible: parent.height > 24

        MouseArea {
            id: topHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startMouseY: 0

            onPressed: (mouse) => {
                startMouseY = mouse.y + parent.y
            }
            onPositionChanged: (mouse) => {
                var dy = mouse.y + parent.y - startMouseY
                var newStartY = block.y + dy
                var min = timeAxisRef.yToMinutes(newStartY)
                if (min >= block.curEndMin - 5) min = block.curEndMin - 5
                block.dragStartMin = min
                block.y = timeAxisRef.timeToY(min)
                block.height = Math.max(16, (block.curEndMin - min) * timeAxisRef.pixelsPerMinute)
            }
            onReleased: () => {
                var newStart = timeAxisRef.snapTo5(block.dragStartMin)
                if (newStart < block.curEndMin - 5) {
                    timeSlotModel.setSlotByMinutes(block.blockIndex, newStart, block.curEndMin)
                }
                block.dragStartMin = block.curStartMin
                block.dragEndMin = block.curEndMin
            }
        }
    }

    // 底部拖拽手柄：改结束时间
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 10
        color: "transparent"
        visible: parent.height > 24

        MouseArea {
            id: bottomHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startMouseY: 0

            onPressed: (mouse) => {
                startMouseY = mouse.y + parent.y
            }
            onPositionChanged: (mouse) => {
                var dy = mouse.y + parent.y - startMouseY
                var newEndY = block.y + block.height + dy
                var min = timeAxisRef.yToMinutes(newEndY - block.y)
                if (min <= block.curStartMin + 5) min = block.curStartMin + 5
                block.dragEndMin = min
                block.height = Math.max(16, (min - block.curStartMin) * timeAxisRef.pixelsPerMinute)
            }
            onReleased: () => {
                var newEnd = timeAxisRef.snapTo5(block.dragEndMin)
                if (newEnd > block.curStartMin + 5) {
                    timeSlotModel.setSlotByMinutes(block.blockIndex, block.curStartMin, newEnd)
                }
                block.dragStartMin = block.curStartMin
                block.dragEndMin = block.curEndMin
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
