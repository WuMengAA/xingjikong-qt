// 星集控 · 手机端设备详情抽屉（2026-10-13 · MD3 迁移批）
// 点设备行打开：详情信息 + 轻指令操作（锁屏/重启/停止画面/关机）。
// 关机走 shutdownRequested 信号 → Main 开确认弹窗；其余直接下发 + toast 信号。
// MD3：底部抽屉（containerLow + 顶部 28 圆角 + 拖拽手柄）+ 列表项 + filled/error 按钮。
import QtQuick
import QtQuick.Controls
import "../components/md3"

Drawer {
    id: drawer
    edge: Qt.BottomEdge
    width: parent.width
    height: Math.min(440, parent.height * 0.62)
    modal: true
    dragMargin: 0
    // 只圆顶角：整圆角 + 同色矩形盖住下半
    background: Rectangle {
        color: drawer.md3.cContLow
        radius: drawer.md3.rXL
        Rectangle {
            width: parent.width
            height: parent.height / 2
            anchors.top: parent.top
            color: parent.color
        }
    }

    property var md3: ({})
    property var dev: ({})       // 当前设备（Main 打开时注入）
    signal shutdownRequested(string uid)
    signal toast(string msg)
    // #5（2026-10-12）：手机端此前完全没有文件传输入口——FilePushDialog 只在桌面
    // Main.qml 被实例化，MobileMain 从 Main.qml 拆出去时这条没跟着迁，于是手机上
    // 「选系统文件」整个消失了。这里补入口：抽屉里发信号，由 mobile/Main.qml 弹窗。
    signal filePushRequested(string uid)

    contentItem: Flickable {
        contentHeight: col.height + 40
        clip: true
        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 6

            // 拖拽手柄（MD3 抽屉规范）
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32; height: 4; radius: 2
                color: drawer.md3.cOutlineVar
            }

            // 设备名 + 在线态
            Row {
                width: parent.width
                spacing: 10
                anchors.topMargin: 10
                Rectangle {
                    width: 12; height: 12; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: (drawer.dev.online !== false) ? drawer.md3.cOk : drawer.md3.cError
                }
                Text {
                    text: (drawer.dev.name && drawer.dev.name !== "") ? drawer.dev.name : drawer.dev.uid
                    color: drawer.md3.cOnSurface
                    font.pixelSize: drawer.md3.headlineS
                    font.weight: Font.Medium
                }
            }
            Text {
                text: "UID " + drawer.dev.uid + (drawer.dev.version ? "  ·  v" + drawer.dev.version : "")
                color: drawer.md3.cOnSurfaceVar
                font.pixelSize: drawer.md3.bodyS
                width: parent.width
                wrapMode: Text.Wrap
            }

            // 操作列表（MD3 列表项：leading 容器 + trailing 箭头）
            Md3ListItem {
                md3: drawer.md3
                width: parent.width
                icon: "🔒"
                headline: "锁屏"
                divider: true
                onClicked: { backend.sendAction("lock", {}); drawer.close(); drawer.toast("已下发锁屏 → " + drawer.dev.uid) }
            }
            Md3ListItem {
                md3: drawer.md3
                width: parent.width
                icon: "🔄"
                headline: "重启"
                divider: true
                onClicked: { backend.sendAction("restart", {}); drawer.close(); drawer.toast("已下发重启 → " + drawer.dev.uid) }
            }
            Md3ListItem {
                md3: drawer.md3
                width: parent.width
                icon: "📹"
                headline: "停止画面（关闭摄像头推流）"
                divider: true
                onClicked: { backend.sendAction("camera_stop", {}); drawer.close(); drawer.toast("已停止画面 → " + drawer.dev.uid) }
            }
            // ── 传文件（#5）──
            Md3ListItem {
                md3: drawer.md3
                width: parent.width
                icon: "📄"
                headline: "文件分发"
                divider: true
                onClicked: {
                    // 先关抽屉再发信号：否则抽屉盖在弹窗上面，弹窗点不到
                    drawer.close()
                    drawer.filePushRequested(drawer.dev.uid)
                }
            }

            // 关机（MD3 filled error 全宽按钮）
            Md3Btn {
                md3: drawer.md3
                width: parent.width
                text: "关机"
                variant: "error"
                anchors.topMargin: 10
                onClicked: { drawer.close(); drawer.shutdownRequested(drawer.dev.uid) }
            }
        }
    }
}
