// 全局状态条（Status Bar）· QtShadcn 规范（2026-10-29 从 MainWindow 抽件）
//
// 职责（H4）：全宽贴底的全局信息带 —— 现算文案「班级 X · N 台 · M 台在线
// [· 正在查看 Y]」。朝向无关，横竖屏都在最底。
//
// ── 2026-10-29 迁移要点（原 MainWindow 内联 18 行）──────────────────
// · `th.*`（Md3 实例）→ `Theme.*` 令牌，零硬编码 hex
// · 外部依赖只有三个：`text`（现算文案）、`height`（骨架高度）、信号无。
//   文案的计算留在 MainWindow._statusText()（它知道班级/设备真源），
//   本件只负责渲染 —— 纯展示组件，不碰数据。
// · 删 border：底色用 surfaceVariant，与内容区 surface 靠色差分区。
import QtQuick
import QtQuick.Layouts
import "../components/theme"   // Theme 令牌

Rectangle {
    id: root

    // 文案由宿主现算传入（MainWindow._statusText()）
    property string text: ""
    /** 骨架高度（MainWindow.statusBarH，默认 32）。 */
    property int barHeight: 32

    Layout.fillWidth: true
    Layout.preferredHeight: root.barHeight
    color: Theme.color.surfaceVariant

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.md
        anchors.rightMargin: Theme.spacing.md
        spacing: Theme.spacing.md

        Text {
            Layout.fillWidth: true
            text: root.text
            // Theme 无字体组（迁移文件惯例：字号按层级硬编码 12/13/16）
            font.pixelSize: 12
            color: Theme.color.textSecondary
            elide: Text.ElideRight
        }
    }
}
