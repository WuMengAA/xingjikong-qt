// 星集控 · 手机端设备详情抽屉 · QtShadcn 规范（2026-10-29 迁移）
// 点设备行打开：详情信息 + 轻指令操作（锁屏/重启/停止画面/文件分发/关机）。
// 关机走 shutdownRequested 信号 → Main 开确认弹窗；其余直接下发 + toast 信号。
//
// ── 为什么仍用 QQC Drawer ───────────────────────────────────────────────
// 规范禁的是"裸用原生控件**默认样式**"（Button/TextField）。Drawer 提供的是
// edge / modal / dragMargin 这类**交互骨架**，不是视觉；其 background 已被
// 我们完全接管（见下）。自绘一套抽屉等于重写手势与遮罩管理，收益为零。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · 字号走规范 12/13/14/16；`Column`/`Row` → Layout（本件拖拽手柄等处保留 anchors 定位）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"          // AppButton / AppListItem
import "../components/theme"    // Theme 令牌

Drawer {
    id: drawer
    edge: Qt.BottomEdge
    width: parent.width
    height: Math.min(440, parent.height * 0.62)
    modal: true
    dragMargin: 0
    // 只圆顶角：整圆角 + 同色矩形盖住下半
    background: Rectangle {
        color: Theme.color.surface
        radius: Theme.radius.xl
        Rectangle {
            width: parent.width
            height: parent.height / 2
            anchors.top: parent.top
            color: parent.color
        }
    }

    property var dev: ({})       // 当前设备（Main 打开时注入）
    signal shutdownRequested(string uid)
    signal toast(string msg)
    // #5（2026-10-12）：手机端此前完全没有文件传输入口 —— 抽屉里发信号，
    // 由 mobile/Main.qml 弹窗。
    signal filePushRequested(string uid)

    contentItem: Flickable {
        contentHeight: col.implicitHeight + Theme.spacing.xl
        clip: true
        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.md
            spacing: Theme.spacing.xs

            // 拖拽手柄（抽屉规范；用 surfaceVariant 而非描边）
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.spacing.xs
                width: 32; height: 4; radius: 2
                color: Theme.color.surfaceVariant
            }

            // 设备名 + 在线态
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing.sm
                spacing: Theme.spacing.sm
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: (drawer.dev.online !== false) ? Theme.color.success
                                                         : Theme.color.destructive
                }
                Text {
                    Layout.fillWidth: true
                    text: (drawer.dev.name && drawer.dev.name !== "") ? drawer.dev.name : drawer.dev.uid
                    color: Theme.color.textPrimary
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
            Text {
                Layout.fillWidth: true
                text: "UID " + drawer.dev.uid + (drawer.dev.version ? "  ·  v" + drawer.dev.version : "")
                color: Theme.color.textSecondary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Item { Layout.preferredHeight: Theme.spacing.sm }

            // 操作列表
            AppListItem {
                Layout.fillWidth: true
                icon: "🔒"
                headline: "锁屏"
                onClicked: { backend.sendAction("lock", {}); drawer.close(); drawer.toast("已下发锁屏 → " + drawer.dev.uid) }
            }
            AppListItem {
                Layout.fillWidth: true
                icon: "🔄"
                headline: "重启"
                onClicked: { backend.sendAction("restart", {}); drawer.close(); drawer.toast("已下发重启 → " + drawer.dev.uid) }
            }
            AppListItem {
                Layout.fillWidth: true
                icon: "📹"
                headline: "停止画面"
                supporting: "关闭摄像头推流"
                onClicked: { backend.sendAction("camera_stop", {}); drawer.close(); drawer.toast("已停止画面 → " + drawer.dev.uid) }
            }
            AppListItem {
                Layout.fillWidth: true
                icon: "📄"
                headline: "文件分发"
                supporting: "从系统选择文件下发"
                onClicked: {
                    // 先关抽屉再发信号：否则抽屉盖在弹窗上面，弹窗点不到
                    drawer.close()
                    drawer.filePushRequested(drawer.dev.uid)
                }
            }

            Item { Layout.preferredHeight: Theme.spacing.sm }

            // 关机（危险操作，全宽）
            AppButton {
                Layout.fillWidth: true
                text: "关机"
                variant: "destructive"
                onClicked: { drawer.close(); drawer.shutdownRequested(drawer.dev.uid) }
            }
        }
    }
}
