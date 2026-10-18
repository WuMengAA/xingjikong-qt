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
    // ⚠️ 2026-10-18 用户反馈「显示不了更多滑动时间轴」第二个根因：
    //    原实现取「最晚结束 + 60」，且最小兜底只有 480 分钟(=8:00)。
    //    课表若只排到上午，滚区高 = 8 小时 ⇒ **滚不到下午**，看起来"滑不动/滑不出更多"。
    //    修法：下限抬到完整 24 小时（1440 分钟），保证任何排期都能滚到任意时刻；
    //    上限仍按实际最晚结束（排到 23:00 也不会多出无意义空白）。
    property real contentHeight: {
        var maxEnd = 1440            // 一整天（分钟）
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
            id: labelFlick
            anchors.fill: parent
            contentHeight: root.contentHeight
            clip: true
            // ⚠️ 2026-10-18 用户反馈「显示不了更多滑动时间轴」根因：
            //    左侧刻度与本区是**两个独立 Flickable**，各自滚动互不联动 ——
            //    滚左边刻度时右边时间块不动、滚右边块时左边刻度不动，
            //    于是"滑动时间轴"看起来只动一边、像是滑不动。
            //    修法：contentY 双向绑定（interactive 只留一边接手势，避免两个同时吃拖动）。
            interactive: false
            contentY: blockFlick.contentY
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
            // 用纯数字 model（timeSlotModel.count），避免 QAbstractListModel role 注入问题
            model: timeSlotModel.count
            delegate: TimeAxisBlock {
                width: blockFlick.width - 16
                x: 8
                timeAxisRef: root
                blockIndex: index
                blockName: timeSlotModel.nameAt(index)
                blockStartMin: timeSlotModel.startMinAt(index)
                blockEndMin: timeSlotModel.endMinAt(index)
                blockType: timeSlotModel.timeTypeAt(index)
                y: root.timeToY(timeSlotModel.startMinAt(index))
                height: Math.max(16, (timeSlotModel.endMinAt(index) - timeSlotModel.startMinAt(index)) * root.pixelsPerMinute)
            }
        }
    }
}
