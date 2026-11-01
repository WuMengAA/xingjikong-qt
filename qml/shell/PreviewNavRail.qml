// 新版外壳 · 左导航轨（2026-11-01）
//
// 从 PreviewConsole.qml 拆出（该文件超 200 行红线，见 §10.5）。
// 观感照设计稿 qml/layout/DesktopConsole.qml：品牌区 + 图标导航项 + 底部用户卡。
// 图标用 lucide SVG（image://svgicon）而非设计稿的 emoji —— 本机 emoji 渲染成方块。
import QtQuick
import "../components/theme"
import "../island"          // SvgIcon

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
            spacing: Theme.spacing.sm
            Rectangle {
                width: 32; height: 32; radius: Theme.radius.md
                color: Theme.color.primary
                Text {
                    anchors.centerIn: parent
                    text: "星"
                    color: Theme.color.primaryForeground
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "星集控"
                color: Theme.color.textPrimary
                font.pixelSize: 16
                font.weight: Font.Medium
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.color.accent
        }
    }

    // 导航项
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
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.sm
                    anchors.rightMargin: Theme.spacing.sm
                    radius: Theme.radius.full
                    color: root.section === modelData.key
                           ? Theme.color.primary
                           : (hover.hovered ? Theme.color.surfaceVariant : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                    HoverHandler { id: hover }
                }
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing.lg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing.md
                    SvgIcon {
                        name: modelData.icon
                        px: 20
                        tint: root.section === modelData.key
                               ? Theme.color.primaryForeground
                               : Theme.color.textSecondary
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        font.pixelSize: 14
                        color: root.section === modelData.key
                              ? Theme.color.primaryForeground
                              : Theme.color.textPrimary
                    }
                }
                TapHandler {
                    onTapped: root.navActivated(modelData.key)
                }
            }
        }
    }

    // 底部用户卡
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
            Rectangle {
                width: 32; height: 32; radius: Theme.radius.full
                color: Theme.color.primary
                Text {
                    anchors.centerIn: parent
                    text: root.accountAvatarText || "?"
                    color: Theme.color.primaryForeground
                    font.pixelSize: 14
                }
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
