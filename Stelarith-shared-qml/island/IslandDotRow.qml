// IslandDotRow.qml —— 轮播圆点指示（自 IslandRoot 抽出，2026-11-03 · 行数门禁）
// 跟随拖动实时高亮当前页；平铺态没有"页"的概念（宿主 visible:false 整行隐掉）。
import QtQuick

Row {
    id: dotRow

    property var th: null
    property int pageCount: 0
    property int curPage: 0
    readonly property int cur: Math.max(0, Math.min(pageCount - 1, curPage))
    spacing: 6

    Repeater {
        model: dotRow.pageCount
        delegate: Rectangle {
            required property int index
            width: index === dotRow.cur ? 14 : 6
            height: 6
            radius: 3
            color: index === dotRow.cur
                   ? (dotRow.th ? dotRow.th.accent : "#6750A4")
                   : (dotRow.th ? (dotRow.th.light ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.22)) : "#888888")
            // 指示条宽度跟随（2026-11-02）：原来写死 220，全岛令牌化时漏了这处。
            // 220 本来就不在阶梯上，就近落到 durMid(200)，观感几乎无差。
            Behavior on width {
                NumberAnimation { duration: dotRow.th ? dotRow.th.durMid : 200; easing.type: Easing.OutCubic }
            }
        }
    }
}
