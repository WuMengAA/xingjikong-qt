import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 时间块：显示时间段，顶部/底部手柄可拖拽调整时长（松手写回模型）
Rectangle {
    id: block
    color: blockType === 0 ? "#0078d4" : "#555"
    radius: 4

    // ── 拖拽手柄的可见性（2026-10-18 · 用户反馈「没有拖拽式编辑功能」根因）──
    // 手柄原本 `color: "transparent"` ⇒ **完全看不见**。功能其实一直在（上有
    // topHandle/bottomHandle 改起止时间、走 undoManager），但老师看不出哪里能拖，
    // 感受上就是"没有拖拽编辑"。
    // 修法：给手柄加抓握条（hover/pressed/拖拽中显现），命中区 10px → 14px。
    property bool handlesHint: blockMa.containsMouse
    readonly property bool handleVisible: dragActive || handlesHint

    MouseArea {
        id: blockMa
        anchors.fill: parent
        hoverEnabled: true
        // 只做 hover 探测，不消费点击（块的点击由内层手柄处理）
        acceptedButtons: Qt.NoButton
    }

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
    // 拖拽中吸附预览：release 位置会被吸附到的 5 分钟刻度
    property bool dragActive: false
    property int snapPreviewMin: -1

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
    // 2026-10-18：命中区 10 → 14px，并加可见抓握条（原 color:transparent 完全看不见）
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 14
        color: "transparent"
        visible: parent.height > 24

        // 抓握条：hover/拖拽时显现，告诉老师"这里能拖"
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 4
            width: 24
            height: 3
            radius: 2
            color: "#FFFFFF"
            opacity: block.handleVisible ? 0.75 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }

        MouseArea {
            id: topHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startMouseY: 0

            onPressed: (mouse) => {
                startMouseY = mouse.y + parent.y
                block.dragActive = true
            }
            onPositionChanged: (mouse) => {
                var dy = mouse.y + parent.y - startMouseY
                var newStartY = block.y + dy
                var min = timeAxisRef.yToMinutes(newStartY)
                if (min >= block.curEndMin - 5) min = block.curEndMin - 5
                block.dragStartMin = min
                block.y = timeAxisRef.timeToY(min)
                block.height = Math.max(16, (block.curEndMin - min) * timeAxisRef.pixelsPerMinute)
                block.snapPreviewMin = timeAxisRef.snapTo5(min)
            }
            onReleased: () => {
                var newStart = timeAxisRef.snapTo5(block.dragStartMin)
                if (newStart < block.curEndMin - 5) {
                    // 走撤销栈（undoManager 统一）
                    undoManager.setSlotTime(block.blockIndex, newStart, block.curEndMin)
                }
                block.dragStartMin = block.curStartMin
                block.dragEndMin = block.curEndMin
                block.dragActive = false
                block.snapPreviewMin = -1
            }
        }
    }

    // 底部拖拽手柄：改结束时间
    // 2026-10-18：命中区 10 → 14px，并加可见抓握条（同顶部）
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 14
        color: "transparent"
        visible: parent.height > 24

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: 24
            height: 3
            radius: 2
            color: "#FFFFFF"
            opacity: block.handleVisible ? 0.75 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }

        MouseArea {
            id: bottomHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startMouseY: 0

            onPressed: (mouse) => {
                startMouseY = mouse.y + parent.y
                block.dragActive = true
            }
            onPositionChanged: (mouse) => {
                var dy = mouse.y + parent.y - startMouseY
                var newEndY = block.y + block.height + dy
                var min = timeAxisRef.yToMinutes(newEndY - block.y)
                if (min <= block.curStartMin + 5) min = block.curStartMin + 5
                block.dragEndMin = min
                block.height = Math.max(16, (min - block.curStartMin) * timeAxisRef.pixelsPerMinute)
                block.snapPreviewMin = timeAxisRef.snapTo5(min)
            }
            onReleased: () => {
                var newEnd = timeAxisRef.snapTo5(block.dragEndMin)
                if (newEnd > block.curStartMin + 5) {
                    undoManager.setSlotTime(block.blockIndex, block.curStartMin, newEnd)
                }
                block.dragStartMin = block.curStartMin
                block.dragEndMin = block.curEndMin
                block.dragActive = false
                block.snapPreviewMin = -1
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

    // 拖拽吸附预览：显示 release 位置会被吸附到的 5 分钟刻度线
    Rectangle {
        id: snapLine
        anchors.left: parent.left
        anchors.right: parent.right
        y: {
            if (block.snapPreviewMin < 0) return -100
            var rel = timeAxisRef.timeToY(block.snapPreviewMin) - block.y
            // 顶部拖：线在块上边缘附近；底部拖：线在下边缘
            return Math.max(0, Math.min(parent.height - 2, rel))
        }
        height: 2
        color: "#ffd33d"
        opacity: block.dragActive ? 0.9 : 0
        visible: opacity > 0
    }
}
