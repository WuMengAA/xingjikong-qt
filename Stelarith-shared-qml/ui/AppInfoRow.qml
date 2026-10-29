// 星集控 · 信息行（AppInfoRow）—— 2026-10-30
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 「左标签 / 右值」是设置页最高频的排版（7 个分区在用被控端自造的
// `ash/AshInfoRow.qml`）。ash 按用户 2026-10-30 裁决走废弃路线 ⇒ 这条是它的
// App 侧对应件。
//
// ── 为什么不用 AppListItem 顶替 ──────────────────────────────────────────
// AppListItem 是「可点的列表项」（带 hover、带 chevron、有最小高度 44）；
// 信息行是**纯展示**、行高随内容、不需要 hover 反馈。用错件会让整页出现
// 一堆"看起来能点其实点不动"的行 —— 正是规范要避免的交互欺骗。
//
// ── ⚠️ 布局反模式（2026-10-30 实测踩到，别改回去）────────────────────────
// 第一版把 `Layout.maximumWidth` 写成 `root.width * 0.62`、并塞了一个弹簧 Item。
// 后果是运行期刷 `Qt Quick Layouts: Detected recursive rearrange. Aborting after
// two iterations.` —— 因为根的 width ← 子项 implicitWidth ← maximumWidth ← 根 width，
// 形成反馈环（RowLayout 会反复重排直到放弃）。
// ⇒ 铁律：**Layout 内部不得用 root.width/root.height 反算子项宽度**。
//   正确做法是让「值列」`Layout.fillWidth: true` 自己去吃掉剩余空间：
//   父给的宽度 → 值列填满 → 右对齐 + elide，全程无环。

import QtQuick
import QtQuick.Layouts
import "theme"

RowLayout {
    id: root

    /** 左侧标签。 */
    property string label: ""
    /** 右侧值。 */
    property string value: ""
    /** 值是否换行（默认单行 + 省略号，避免长 ws 地址把布局撑破）。 */
    property bool wrapValue: false

    spacing: Theme.spacing.md

    Text {
        text: root.label
        font.pixelSize: 13
        color: Theme.color.textSecondary
        Layout.alignment: Qt.AlignVCenter
    }

    // 吃掉剩余宽度：这是「右值贴右」的实现方式（不是弹簧 Item，也不是宽度反算）
    Text {
        text: root.value
        font.pixelSize: 13
        color: Theme.color.textPrimary
        horizontalAlignment: Text.AlignRight
        wrapMode: root.wrapValue ? Text.Wrap : Text.NoWrap
        elide: root.wrapValue ? Text.ElideNone : Text.ElideRight
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
    }
}
