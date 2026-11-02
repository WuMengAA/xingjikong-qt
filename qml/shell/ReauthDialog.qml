// 重认证弹窗（Reauth Dialog）· QtShadcn 规范（2026-10-29 从 MainWindow 抽件）
//
// 职责（2026-10-24 安全门）：敏感操作被云端 REAUTH_REQUIRED 拦（长时间未验证密码）：
// 连接**不断**，只提示"该再验一次密码了"→ 点「重新验证」走 backend.loginWithSite()
// （OAuth 授权流程 = 站点验证密码）→ 新票据到位后一切自动恢复。
//
// ── 2026-10-29 迁移要点（原 MainWindow 内联 71 行）──────────────────
// · 自绘遮罩理由同 ToastLayer 文件头：骨架页不依赖灵动岛/组件弹窗挂载层级
// · 内部连 backend.onReauthRequired —— 宿主无需再写 Connections
import QtQuick
import QtQuick.Layouts
import "../components"        // AppButton
import "../components/theme"  // Theme 令牌

Item {
    id: root

    anchors.fill: parent
    visible: false

    Rectangle {
        anchors.fill: parent
        color: Theme.color.scrim          // 50% 黑遮罩

        Rectangle {
            id: reauthCard
            width: Math.min(360, parent.width - 48)
            anchors.centerIn: parent
            radius: Theme.radius.lg
            color: Theme.color.surface

            Column {
                width: parent.width - 32
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                spacing: Theme.spacing.md

                Text {
                    text: "需要重新验证密码"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                Text {
                    id: reauthMsg
                    width: parent.width
                    text: "长时间未验证，敏感操作被云端拦截。"
                    font.pixelSize: 13
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: "连接不会断开，重新验证一次即可恢复全部操作。"
                    font.pixelSize: 12
                    color: Theme.color.disabled
                    wrapMode: Text.WordWrap
                }

                Row {
                    width: parent.width
                    height: 40
                    spacing: Theme.spacing.md
                    layoutDirection: Qt.RightToLeft

                    AppButton {
                        text: "重新验证"
                        variant: "primary"
                        onClicked: {
                            root.visible = false
                            if (backend) backend.loginWithSite()
                        }
                    }
                    AppButton {
                        text: "稍后"
                        variant: "ghost"
                        onClicked: root.visible = false
                    }
                }
            }
        }
    }

    Connections {
        target: backend
        function onReauthRequired(message) {
            if (message) reauthMsg.text = message
            root.visible = true
        }
    }
}
