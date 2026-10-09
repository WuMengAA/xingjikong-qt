// 手机端卡片（2026-10-09 · 触控友好 56px 级）
import QtQuick

Rectangle {
    id: card

    property var theme: ({})
    property string title: ""
    property string sub: ""
    property string value: ""

    width: parent ? parent.width : 0
    height: 72
    radius: card.theme.rCard || 12
    color: card.theme.panel || "#101010"
    border.color: card.theme.stroke || "#333333"
    border.width: 1

    Row {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        Text {
            id: valueText
            text: card.value
            anchors.verticalCenter: parent.verticalCenter
            color: card.theme.fg || "#FAFAFA"
            font.pixelSize: 15
            font.weight: Font.Medium
        }
        Column {
            width: parent.width - valueText.width - 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                text: card.title
                color: card.theme.fg || "#FAFAFA"
                font.pixelSize: 15
                font.weight: Font.Medium
                elide: Text.ElideRight
                width: parent.width
            }
            Text {
                text: card.sub
                visible: card.sub !== ""
                color: card.theme.fg3 || "#8A8A8A"
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }
}