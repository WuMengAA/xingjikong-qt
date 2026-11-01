// 新版外壳 · 顶栏（2026-11-01）
//
// 从 PreviewConsole.qml 拆出（该文件超 200 行红线，见 §10.5）。
// 观感照设计稿 qml/layout/DesktopConsole.qml：班级下拉胶囊 + 搜索框 + 右上头像。
import QtQuick
import "../components/theme"
import "../island"          // SvgIcon

Item {
    id: root

    property string currentClass: ""
    property string accountAvatarText: ""
    signal avatarClicked()

    height: 64

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.color.accent
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacing.md

        // 班级下拉胶囊
        Rectangle {
            width: 132; height: 40; radius: Theme.radius.full
            color: Theme.color.surface
            border.color: Theme.color.accent
            border.width: 1
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                text: root.currentClass || "全部班级"
                font.pixelSize: 14
                color: Theme.color.textPrimary
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                text: "▾"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }
        }

        // 搜索框
        Rectangle {
            width: 240; height: 40; radius: Theme.radius.md
            color: Theme.color.surfaceVariant
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.sm
                SvgIcon { name: "search"; px: 18; tint: Theme.color.textSecondary }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "搜索设备 / UID"
                    font.pixelSize: 14
                    color: Theme.color.textSecondary
                }
            }
        }
    }

    // 头像（右上）
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36; radius: Theme.radius.full
        color: Theme.color.primary
        Text {
            anchors.centerIn: parent
            text: root.accountAvatarText || "?"
            color: Theme.color.primaryForeground
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        TapHandler { onTapped: root.avatarClicked() }
    }
}
