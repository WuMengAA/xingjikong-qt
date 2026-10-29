// 星集控 · 卡片标题行（AppCardTitle）—— 2026-10-30
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 设置页 9 个分区各自要一行「分区标题 + 一句说明」。原来用的是被控端自造的
// `ash/AshCardTitle.qml`；ash 体系按用户 2026-10-30 裁决走废弃路线，这条是它的
// App 侧对应件。与 `AppCard` 的内层 ColumnLayout 天然兼容（本件根就是 ColumnLayout，
// 直接写在 AppCard 里即可，不需要再包一层）。
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 无边框、无底色：标题行只靠字重与字色分层（规范：分区只靠背景色差/字阶）
// · 间距：`Theme.spacing.xs`(4)，不写裸数字
// · 颜色：仅 `Theme.color.textPrimary` / `textSecondary`
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppCard {
//       AppCardTitle { text: "这台设备"; supporting: "改动即时生效" }
//       AppInfoRow { label: "当前版本"; value: "0.6.50" }
//   }

import QtQuick
import QtQuick.Layouts
import "theme"

ColumnLayout {
    id: root

    /** 主标题（14 Medium）。 */
    property string text: ""
    /** 副标题/说明（12 次文字色）。空串 = 不占位。 */
    property string supporting: ""

    spacing: Theme.spacing.xs

    Text {
        Layout.fillWidth: true
        text: root.text
        font.pixelSize: 14
        font.weight: Font.Medium
        color: Theme.color.textPrimary
        wrapMode: Text.Wrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.supporting !== ""
        text: root.supporting
        font.pixelSize: 12
        color: Theme.color.textSecondary
        wrapMode: Text.Wrap
    }
}
