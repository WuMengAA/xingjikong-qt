// 星集控 · 主按钮（AppButton）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 高度：md = 40 / sm = 32
// · 圆角：Theme.radius.md（可用 `radius` 覆写 —— 见下方「局部例外」）
// · **无边框**（不写 border.width / border.color，区域靠底色区分）
// · 颜色全部走 Theme.color.*，本文件无任何硬编码 Hex
// · 动效：hover scale 1.02（150ms）；按下 scale 0.95 + Easing.OutBack（300ms）
// · 变体：primary / secondary / destructive（+ ghost，说明见下）
//
// ── 局部例外：radius 覆写（2026-10-29 用户裁决 C 案）────────────────────
// 用户 2026-10-19 曾明确点单「监控中心 / ActionBar 的按钮统一 12px 方角」
// （AGENTS.md §9.3 已登记）。规范要求全局 `Theme.radius.md`，但那条点单是**局部**的：
//   ⇒ `radius: -1`（默认）= 跟随 Theme.radius.md；显式 `radius: 12` = 只在该处覆写。
// 不要全局改成 12，也不要在别处随手覆写 —— 例外只属于当初点单的那几处。
//
// ── 关于 ghost 变体 ─────────────────────────────────────────────────────
// 规范列了 primary / secondary / destructive 三档，但存量界面有「弱化、不抢主操作」
// 的第三类按钮（如 ActionBar 竖屏的「返回经典界面」）。若强行用 secondary（有底色），
// 它会和两个主操作抢视觉重量 —— 正是原注释要避免的。故补 `ghost`：
// 透明底 + 主色文字，hover 才出浅底。这是 shadcn 自带的 variant，属补齐而非另创。

import QtQuick
import "theme"

Item {
    id: root

    property string text: ""
    /** primary | secondary | destructive | ghost */
    property string variant: "primary"
    /** md | sm */
    property string size: "md"
    /** 圆角覆写：-1 = 跟随 Theme.radius.md；>=0 = 局部例外（见文件头说明）。 */
    property int radius: -1

    signal clicked()

    readonly property bool _sm: size === "sm"
    readonly property int _h: _sm ? 32 : 40
    readonly property int _hPad: _sm ? 12 : 16
    readonly property int _fontSize: _sm ? 12 : 14
    readonly property int _r: radius >= 0 ? radius : Theme.radius.md

    implicitWidth: label.implicitWidth + _hPad * 2
    implicitHeight: _h

    // 底色：变体 → 语义 token（无边框，纯底色区分）
    readonly property color _bg: variant === "destructive" ? Theme.color.destructive
                               : variant === "secondary"   ? Theme.color.surfaceVariant
                               : variant === "ghost"       ? "transparent"
                                                           : Theme.color.primary
    readonly property color _fg: variant === "destructive" ? Theme.color.destructiveForeground
                               : variant === "ghost"       ? Theme.color.textPrimary
                               : variant === "secondary"   ? Theme.color.textPrimary
                                                           : Theme.color.primaryForeground

    opacity: enabled ? 1.0 : 0.45
    scale: ma.pressed ? Theme.motion.pressScale
         : ma.containsMouse ? Theme.motion.hoverScale : 1.0

    // 果冻回弹：按下/抬起用 OutBack（有 overshoot），hover 用短时长无回弹
    Behavior on scale {
        NumberAnimation {
            duration: ma.pressed ? Theme.motion.normal : Theme.motion.fast
            easing.type: ma.pressed ? Easing.OutBack : Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root._r
        color: root._bg
        // hover/pressed 用叠色表现（不改 base color，避免 RGBA 插值出中间灰）
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root._fg
            // ghost 无底色，hover 需要更明显的浅底
            opacity: !root.enabled ? 0
                   : ma.pressed ? 0.12
                   : ma.containsMouse ? (root.variant === "ghost" ? 0.08 : 0.06) : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: root._fontSize
        font.weight: Font.Medium
        color: root.enabled ? root._fg : Theme.color.disabled
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
