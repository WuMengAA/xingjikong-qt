// 首次启动引导 · 主按钮（2026-10-20）
// 为什么不用 Md3Btn：MD3 规范的按钮高 40，而触屏/教室机场景要求**主操作至少 48**，
// 且本向导的主按钮是「整行通栏」——Md3Btn 是药丸自适宽，撑不出通栏效果。
// 自绘一个（不用 Qt Button，遵项目纪律），尺寸/状态层按 MD3 取。
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

    // MD3 状态层：hover 8% / press 10%
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
