// 星集控 · 通用卡片
//
// 界面上所有"一张一块"的区域都走这个组件（概览/控制的空态、设置页的四张卡、关于…），
// 以前每张卡自己写一句 color/border/radius，四张卡就有四套值 —— 改主题时总漏一张。
// 卡片只有**一种**长相：panel 底 + card 边 + 12 圆角 + 16 内距，别再给某一张特殊待遇。
//
// 用法：
//   Card { theme: root.th; title: "账户"
//          Column { anchors.fill: parent; anchors.margins: 16; ... } }
import QtQuick
import QtQuick.Controls

Rectangle {
    id: card
    property var theme: ({ })
    property alias title: titleText.text       // 11px 辅助色，一行
    property alias hint: hintText.text         // 标题下面那行小字（可选）
    property int pad: 16                       // 内距：别让某张卡单独拍一个 14/18

    radius: card.theme && card.theme.rCard !== undefined ? card.theme.rCard : 12
    color: card.theme && card.theme.panel !== undefined ? card.theme.panel : "#101010"
    border.color: card.theme && card.theme.card !== undefined ? card.theme.card : "#181818"
    border.width: 1

    default property alias content: inner        // 直接写子组件即可，不用再包一层 Column

    Column {
        id: inner
        anchors.fill: parent
        anchors.margins: card.pad
        spacing: 10
    }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: card.pad
        anchors.topMargin: card.pad
        visible: text !== ""
        color: card.theme && card.theme.fg3 !== undefined ? card.theme.fg3 : "#5A5A5A"
        font.pixelSize: 11
    }

    Text {
        id: hintText
        anchors.left: parent.left
        anchors.top: titleText.bottom
        anchors.leftMargin: card.pad
        anchors.topMargin: 2
        visible: text !== ""
        wrapMode: Text.Wrap
        width: card.width - card.pad * 2
        color: card.theme && card.theme.fg4 !== undefined ? card.theme.fg4 : "#3A3A3A"
        font.pixelSize: 11
    }
}
