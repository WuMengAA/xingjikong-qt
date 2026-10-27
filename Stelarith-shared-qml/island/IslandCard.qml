import QtQuick 2.15
import "."

// 灵动岛 · 卡片容器（统一布局规范 · 基元二）
//
// 替代各 face 手搓的「Rectangle + radius + border」独立容器：统一用 IslandSurface
// （G2 曲率连续圆角，规避 Rectangle.radius 的折角）打底，再给一层标准内边距的内容区。
// 各 face 之前散落的 10/8/6 圆角与 0.05/0.06 透明度底色，现统一引用 th.rCard 与
// 规范默认 fill/stroke。
//
// 接口：
//   th          主题令牌（必传）
//   radius      圆角（默认 th.rCard）
//   fill        底色（默认：比 shell 略深/浅一档，与历史面板一致）
//   stroke      描边色（默认：比 shell 略强一档，有区分度）
//   pad         内容内边距（默认 th.spM）
//   clipContent 内容是否裁剪到圆角（列表类容器置 true）
Item {
    id: card
    property var th: null
    property real radius: (th ? th.rCard : 10)
    property color fill: (th ? (th.light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.06)) : "#222222")
    property color stroke: (th ? (th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)) : "#333333")
    property real pad: (th ? th.spM : 12)
    property bool clipContent: false

    default property alias content: body.data

    IslandSurface {
        anchors.fill: parent
        radius: card.radius
        fillColor: card.fill
        strokeColor: card.stroke
        strokeWidth: 1
        th: card.th
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: card.pad
        clip: card.clipContent
    }
}
