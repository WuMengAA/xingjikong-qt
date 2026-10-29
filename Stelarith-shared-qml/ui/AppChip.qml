// 星集控 · 筛选胶囊（AppChip）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · **无边框**：选中态用主色实底，未选中用 surfaceVariant 底，靠底色差区分
// · 圆角 Theme.radius.full（药丸）
// · 高 36（8 的倍数附近，与 chip 常规尺寸一致）
// · 动效：按下 scale 0.95 果冻回弹
//
// ── 为什么不用 Md3Chip ──────────────────────────────────────────────────
// Md3Chip 选中态是描边 + 浅底，属"靠线条分区"，与规范的去边框铁律冲突。
// 本件改为「实底主色 vs 浅底」二态。
//
// 用法：
//   AppChip { text: "1班"; selected: true; onClicked: ... }

import QtQuick
import "theme"

Item {
    id: root

    property string text: ""
    property bool selected: false

    signal clicked()

    implicitWidth: label.implicitWidth + Theme.spacing.md * 2
    implicitHeight: 36

    scale: ma.pressed ? Theme.motion.pressScale : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: Theme.motion.normal
            easing.type: Easing.OutBack
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.full
        color: root.selected ? Theme.accentStrong : Theme.color.surfaceVariant
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

        // hover 叠色（未选中才需要；选中的主色底已足够醒目）
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.textPrimary
            opacity: (!root.selected && ma.containsMouse) ? 0.06 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 13
        font.weight: root.selected ? Font.Medium : Font.Normal
        color: root.selected ? Theme.accentStrongForeground : Theme.color.textPrimary
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
