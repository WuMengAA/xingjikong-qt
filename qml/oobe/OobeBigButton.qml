// 首次启动引导 · 主按钮（2026-10-20）
import QtQuick
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）

Rectangle {
    id: root
    property string text: ""
    property bool primary: true
    property bool active: true          // ⚠️ 不能叫 enabled：会盖掉 Item.enabled（项目已踩过的坑）
    signal clicked()

    implicitHeight: 52                    // ≥48：教室机触摸友好
    implicitWidth: 200
    radius: Theme.radius.full
    color: primary ? (active ? Theme.accentStrong : Theme.color.surface)
                   : (active ? Theme.color.surface : Theme.color.surfaceVariant)
    opacity: active ? 1 : 0.6

    Text {
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 16
        font.weight: Font.Medium
        color: root.primary ? Theme.accentStrongForeground : Theme.color.textPrimary
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Theme.color.textPrimary
        opacity: mouse.pressed ? 0.10 : (mouse.containsMouse ? 0.06 : 0)
        visible: opacity > 0
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.active
        cursorShape: root.active ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
