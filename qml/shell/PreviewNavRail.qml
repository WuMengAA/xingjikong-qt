// 新版外壳 · 左导航轨（2026-11-01 · QtShadcn 全量）
//
// 从 PreviewConsole.qml 拆出（该文件超 200 行红线，见 §10.5）。
// 观感照设计稿 qml/layout/DesktopConsole.qml：品牌区 + 图标导航项 + 底部用户卡。
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 导航项之前是手画 Rectangle + HoverHandler + TapHandler —— 与裁决不符。本版换成库组件：
//   · 导航项：手画 → **ShadcnButton**(variant Ghost / Secondary，checked 态由 checkable 驱动)
//     —— 自带 hover/press/focus 状态层，不用自己写 ColorAnimation
//   · 品牌方块：手画 Rectangle → **ShadcnAvatar**(size Large, text "星")
//   · 分隔线：Rectangle → **ShadcnSeparator**
//   · 底部用户卡头像：手画 → **ShadcnAvatar**
// 令牌仍统一走 Theme.*（唯一真源，见 ui/theme/Theme.qml 头注释）。
import QtQuick
import QtShadcn
import "../components/theme"

Rectangle {
    id: root

    // ── 输入 ──────────────────────────────────────────────
    property var navItems: []
    property string section: "dash"
    property string accountTitle: ""
    property string accountSub: ""
    property string accountAvatarText: ""
    signal navActivated(string key)
    signal avatarClicked()

    width: 240
    color: Theme.color.surface

    // 品牌区
    Item {
        id: brand
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing.md
            ShadcnAvatar {
                anchors.verticalCenter: parent.verticalCenter
                size: ShadcnAvatar.Size.Small
                text: "星"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "星集控"
                color: Theme.color.textPrimary
                font.pixelSize: 16
                font.weight: Font.Medium
            }
        }
        ShadcnSeparator {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            orientation: Qt.Horizontal
        }
    }

    // 导航项（ShadcnButton：Ghost 常态 / Secondary 选中，自带状态层）
    Column {
        anchors.top: brand.bottom
        anchors.topMargin: Theme.spacing.md
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacing.xs

        Repeater {
            model: root.navItems
            delegate: Item {
                required property var modelData
                width: parent.width
                height: 48

                ShadcnButton {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.sm
                    anchors.rightMargin: Theme.spacing.sm
                    text: modelData.label
                    iconName: modelData.icon
                    checkable: true
                    checked: root.section === modelData.key
                    variant: checked ? ShadcnButton.Variant.Secondary
                                     : ShadcnButton.Variant.Ghost
                    size: ShadcnButton.Size.Medium
                    // ⚠️ ShadcnButton 的 contentItem 固定居中（图标+文字），
                    //    没有 horizontalAlignment —— 导航项沿用居中观感，
                    //    要左对齐就得覆写 contentItem（等于自己重画，违背用库组件的初衷）。
                    onClicked: root.navActivated(modelData.key)
                }
            }
        }
    }

    // 底部用户卡（ShadcnAvatar + 文案；整块可点 → 个人中心）
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 72
        color: Theme.color.surfaceVariant

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing.md
            ShadcnAvatar {
                anchors.verticalCenter: parent.verticalCenter
                size: ShadcnAvatar.Size.Small
                text: root.accountAvatarText || "?"
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: root.accountTitle || "未登录"
                    color: Theme.color.textPrimary
                    font.pixelSize: 14
                }
                Text {
                    text: root.accountSub
                    color: Theme.color.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: 120
                }
            }
        }
        TapHandler { onTapped: root.avatarClicked() }
    }
}
