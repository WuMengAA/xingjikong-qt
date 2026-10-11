import QtQuick 2.15

// 时钟（替代 src/island.cpp 644-689 自绘的 HH:mm + 状态小字）
//
// 行为对齐：
//   · HH:mm 13px Medium，右对齐（island.cpp timeRect AlignRight）
//   · 500ms 刷新（分钟级足够，不逐秒重绘 —— 对齐 m_clockTick）
//   · status 为空时不显示状态行（对齐 IslandClock::setStatus 空则不画）
Item {
    id: clock

    property var theme
    property string status: ""
    property int timePx: 13
    property bool bold: false

    // 供外部绑定的当前时间文本
    readonly property string timeText: _t
    property string _t: ""

    implicitWidth: Math.max(timeLabel.implicitWidth, statusLabel.implicitWidth)
    implicitHeight: timeLabel.implicitHeight
                    + (status !== "" ? statusLabel.implicitHeight : 0)

    Text {
        id: timeLabel
        anchors.right: parent.right
        anchors.verticalCenter: status === "" ? parent.verticalCenter : undefined
        anchors.top: status === "" ? undefined : parent.top
        text: clock._t
        color: clock.theme ? clock.theme.fg : "#FAFAFA"
        font.pixelSize: clock.timePx
        font.weight: clock.bold ? Font.Bold : Font.Medium
    }

    Text {
        id: statusLabel
        visible: clock.status !== ""
        anchors.right: parent.right
        anchors.top: timeLabel.bottom
        text: clock.status
        color: clock.theme ? clock.theme.sub : "#C8C8C8"
        font.pixelSize: Math.max(8, Math.round(clock.timePx / 2))
    }

    Timer {
        interval: 500
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: clock._t = Qt.formatTime(new Date(), "HH:mm")
    }
}
