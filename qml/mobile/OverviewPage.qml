// 星集控 · 手机端概览页（2026-10-11 · 独立 QML 方案）
// 页 0：教室在线 / 连接状态 / 账户。纯展示，backend context 直接读。
import QtQuick

Item {
    id: page
    property var th: ({})

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + 24
        clip: true
        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            Rectangle {
                width: parent.width
                height: 84
                radius: page.th.rCard
                color: page.th.panel
                border.color: page.th.stroke
                border.width: 1
                Row {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text { text: "教室在线"; color: page.th.fg3; font.pixelSize: 12 }
                        Text { text: backend.onlineCount + " 台"; color: page.th.fg; font.pixelSize: 22; font.weight: Font.Medium }
                    }
                    Column {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text { text: "设备总数"; color: page.th.fg3; font.pixelSize: 12 }
                        Text { text: backend.devices.length + " 台"; color: page.th.fg; font.pixelSize: 18 }
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: 84
                radius: page.th.rCard
                color: page.th.panel
                border.color: page.th.stroke
                border.width: 1
                Row {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text { text: "连接状态"; color: page.th.fg3; font.pixelSize: 12 }
                        Text {
                            text: backend.loggedIn ? (backend.connected ? "云端就绪" : "连接断开") : "未登录"
                            color: backend.connected ? page.th.ok : page.th.err
                            font.pixelSize: 18; font.weight: Font.Medium
                        }
                    }
                    Column {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text { text: "账户"; color: page.th.fg3; font.pixelSize: 12 }
                        Text { text: backend.accountName ? backend.accountName : "未登录"; color: page.th.fg; font.pixelSize: 13; elide: Text.ElideRight }
                    }
                }
            }
            Text {
                text: "手机端为管理端伴侣：查看教室状态、发轻指令。\n远控 / 语音 / 广播 / 课表请用桌面管理端。"
                width: parent.width
                wrapMode: Text.Wrap
                color: page.th.fg4
                font.pixelSize: 11
            }
        }
    }
}
