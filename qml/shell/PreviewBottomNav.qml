// 新版外壳 · 竖屏底部导航（2026-11-01 · QtShadcn 全量）
//
// 转正时从经典外壳搬来：与 PreviewNavRail 同一份 navItems 的横排形态。
// 竖屏（shell.portrait）时左导航轨让位给本件，横屏时本件不占空间。
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 之前每项是手画 SvgIcon + Text + TapHandler。本版换成库组件：
//   · 每项 **ShadcnButton**(variant Ghost / Secondary, checkable) —— 自带 hover/press/focus 状态层
//   · 分隔线 **ShadcnSeparator**
// 令牌统一走 Theme.*。
import QtQuick
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

    Row {
        anchors.centerIn: parent
        spacing: Theme.spacing.sm

        Repeater {
            model: root.navItems
            delegate: ShadcnButton {
                required property var modelData
                width: 88
                height: 56
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
