// 新版外壳 · 竖屏底部导航（2026-11-01 转正 · 2026-11-03 窄屏重排）
//
// 转正时从经典外壳搬来：与 PreviewNavRail 同一份 navItems 的横排形态。
// 竖屏（shell.portrait）时左导航轨让位给本件，横屏时本件不占空间。
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 每项用 **ShadcnButton**(variant Ghost / Secondary, checkable) —— 自带 hover/press/focus 状态层。
//
// ── 2026-11-03 修窄屏溢出（本件是真正的手机外壳，改这里才有用）────────────
// 改前：`Row { Repeater { ShadcnButton { width: 88 } } }` —— 5 项 × 88 + 4 × 8 = **472px**。
//   Typical Android 竖屏 390pt ⇒ 整排被右边界裁掉，第 5 项「系统设置」根本点不到。
//   这不是"不好看"，是**功能不可达**：手机用户从底部导航永远进不去系统设置。
// 改法：RowLayout + 每项 `Layout.fillWidth`，宽度由容器均分而不是写死。
//   spacing 从 sm(8) 降到 xs(4)：390 宽下 5 项每项还能有 ~70px，图标 + 中文两字够用。
//   下限用 `Layout.minimumWidth: 56`（触摸目标下限，Android 无障碍是 48，留余量）。
import QtQuick
import QtQuick.Layouts
import QtShadcn
import "../components/theme"

Rectangle {
    id: root

    property var navItems: []
    property string section: "dash"
    signal navActivated(string key)

    height: 80
    color: Theme.color.surface

    ShadcnSeparator {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        orientation: Qt.Horizontal
    }

    RowLayout {
        objectName: "navRow"        // 探针锚点
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.xs
        anchors.rightMargin: Theme.spacing.xs
        spacing: Theme.spacing.xs

        Repeater {
            model: root.navItems
            delegate: ShadcnButton {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 56
                Layout.topMargin: Theme.spacing.xs + 2
                Layout.bottomMargin: Theme.spacing.xs + 2
                text: modelData.label
                iconName: modelData.icon
                checkable: true
                checked: root.section === modelData.key
                variant: checked ? ShadcnButton.Variant.Secondary
                                 : ShadcnButton.Variant.Ghost
                size: ShadcnButton.Size.Small
                onClicked: root.navActivated(modelData.key)
            }
        }
    }
}
