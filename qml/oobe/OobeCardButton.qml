// 首次启动引导 · 方式选择卡片（2026-10-20）
// 触屏友好：整卡点击区（不是只有文字可点），最小高度 132。
import QtQuick
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）

Rectangle {
    id: root
    property string glyph: ""        // 汉字/符号字形（教室机 emoji 有 tofu 风险 ⇒ 用汉字）
    property string title: ""
    property string subtitle: ""
    property bool recommended: false
    signal clicked()

    implicitHeight: 132
    implicitWidth: 320
    radius: Theme.radius.md
    color: Theme.color.surfaceVariant

    // 底部微弱阴影（用户要求「纯白/主色卡片 + 底部微阴影」）
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: -6
        width: parent.width - 24
        height: 12
        radius: 6
        color: Qt.rgba(0, 0, 0, 0.06)
        z: -1
    }

    Row {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // 图标底盘（主色浅底 + 一个汉字字形）
        Rectangle {
            width: 48
            height: 48
            radius: 24
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.color.surfaceVariant
            Text {
                anchors.centerIn: parent
                text: root.glyph
                font.pixelSize: 20
                color: Theme.color.textPrimary
            }
        }
        Column {
            width: parent.width - 64 - 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Row {
                spacing: 8
                Text {
                    text: root.title
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                Rectangle {
                    visible: root.recommended
                    width: 44
                    height: 20
                    radius: 10
                    color: Theme.color.surfaceVariant
                    Text {
                        anchors.centerIn: parent
                        text: "推荐"
                        font.pixelSize: 11
                        color: Theme.color.textPrimary
                    }
                }
            }
            Text {
                width: parent.width
                text: root.subtitle
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
