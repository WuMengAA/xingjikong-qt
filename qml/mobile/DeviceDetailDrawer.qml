// 星集控 · 手机端设备详情抽屉（2026-10-11 · 独立 QML 方案，文档 5.3 Drawer 底部抽屉）
// 点设备行打开：详情信息 + 轻指令操作（锁屏/重启/停止画面/关机）。
// 关机走 shutdownRequested 信号 → Main 开确认弹窗；其余直接下发 + toast 信号。
import QtQuick
import QtQuick.Controls

Drawer {
    id: drawer
    edge: Qt.BottomEdge
    width: parent.width
    height: Math.min(420, parent.height * 0.6)
    modal: true
    dragMargin: 0
    background: Rectangle { color: drawer.th.panel }

    property var th: ({})
    property var dev: ({})       // 当前设备（Main 打开时注入）
    signal shutdownRequested(string uid)
    signal toast(string msg)
    // #5（2026-10-12）：手机端此前**完全没有文件传输入口**——FilePushDialog 只在桌面
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
            anchors.margins: 18
            spacing: 14

            // 设备名 + 在线态
            Row {
                width: parent.width; spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: (drawer.dev.online !== false) ? drawer.th.ok : drawer.th.err
                }
                Text {
                    text: (drawer.dev.name && drawer.dev.name !== "") ? drawer.dev.name : drawer.dev.uid
                    color: drawer.th.fg; font.pixelSize: 18; font.weight: Font.Medium
                }
            }
            Text {
                text: "UID " + drawer.dev.uid + (drawer.dev.version ? "  ·  v" + drawer.dev.version : "")
                color: drawer.th.fg3; font.pixelSize: 12; wrapMode: Text.Wrap; width: parent.width
            }
            Rectangle { width: parent.width; height: 1; color: drawer.th.sepline }

            // 操作：全宽触摸按钮（>40px 高）
            Rectangle {
                width: parent.width; height: 44; radius: drawer.th.rCtrl
                color: drawer.th.hover; border.color: drawer.th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "锁屏"; color: drawer.th.fg; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; onClicked: { backend.sendAction("lock", {}); drawer.close(); drawer.toast("已下发锁屏 → " + drawer.dev.uid) } }
            }
            Rectangle {
                width: parent.width; height: 44; radius: drawer.th.rCtrl
                color: drawer.th.hover; border.color: drawer.th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "重启"; color: drawer.th.fg; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; onClicked: { backend.sendAction("restart", {}); drawer.close(); drawer.toast("已下发重启 → " + drawer.dev.uid) } }
            }
            Rectangle {
                width: parent.width; height: 44; radius: drawer.th.rCtrl
                color: drawer.th.hover; border.color: drawer.th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "停止画面（关闭摄像头推流）"; color: drawer.th.fg; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; onClicked: { backend.sendAction("camera_stop", {}); drawer.close(); drawer.toast("已停止画面 → " + drawer.dev.uid) } }
            }
            // ── 传文件（#5）──
            Rectangle {
                width: parent.width; height: 44; radius: drawer.th.rCtrl
                color: drawer.th.hover; border.color: drawer.th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "传文件"; color: drawer.th.fg; font.pixelSize: 14 }
                MouseArea {
                    anchors.fill: parent
                    // 先关抽屉再发信号：否则抽屉盖在弹窗上面，弹窗点不到
                    onClicked: { drawer.close(); drawer.filePushRequested(drawer.dev.uid) }
                }
            }
            Rectangle {
                width: parent.width; height: 44; radius: drawer.th.rCtrl
                color: drawer.th.err
                Text { anchors.centerIn: parent; text: "关机"; color: "#fff"; font.pixelSize: 14 }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { drawer.close(); drawer.shutdownRequested(drawer.dev.uid) }
                }
            }
        }
    }
}
