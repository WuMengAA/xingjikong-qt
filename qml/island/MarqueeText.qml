import QtQuick 2.15

// 滚动文本条（替代 src/island.cpp 的 m_marqueeX 手绘滚动）
//
// 行为对齐（island.cpp 148-159 + 展开态描述绘制）：
//   · 文本宽 > 可视宽才滚；不超宽时静止左对齐（不滚）
//   · 循环进场：文本右缘滚出左缘后从右侧重新进场（无停顿）
//   · 速度 32px/s（对齐 C++ 的 16px/500ms）
//   · 持续直到人为停止；paused 停住
//
// 用法：MarqueeText { width: 200; text: "..."; theme: root.th }
Item {
    id: marquee
    clip: true

    property string text: ""
    property var theme
    property bool paused: false
    property int speed: 32          // px/s
    property int px: 13             // 字号
    property bool bold: false

    readonly property bool running: text !== "" && !paused
                                    && label.implicitWidth > marquee.width
    property real xOff: 0

    // ⚠️ 用真实 elapsed 算位移，不用固定 16ms/帧。
    //    Timer 抖了（掉帧/系统忙）时固定步长会漂移，用 Date.now() 保证速度恒定。
    property real _lastTs: 0

    implicitHeight: label.implicitHeight

    Text {
        id: label
        x: marquee.running ? marquee.xOff : 0
        text: marquee.text
        color: marquee.theme ? marquee.theme.fg : "#FAFAFA"
        font.pixelSize: marquee.px
        font.bold: marquee.bold
        font.weight: marquee.bold ? Font.Bold : Font.Normal
        elide: Text.ElideNone
        wrapMode: Text.NoWrap
        verticalAlignment: Text.AlignVCenter
    }

    Timer {
        interval: 16
        repeat: true
        running: marquee.running
        onTriggered: {
            var now = Date.now()
            var dt = marquee._lastTs > 0 ? (now - marquee._lastTs) / 1000 : 0.016
            marquee._lastTs = now
            marquee.xOff -= marquee.speed * dt
            if (marquee.xOff + label.implicitWidth < 0)
                marquee.xOff = marquee.width
        }
    }

    onTextChanged: reset()
    onWidthChanged: reset()
    onRunningChanged: { if (running) _lastTs = 0 }
    function reset() {
        xOff = (text === "" || label.implicitWidth <= width) ? 0 : width
        _lastTs = 0
    }
}
