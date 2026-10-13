// 星集控 · 手机端概览页（2026-10-13 · MD3 迁移批）
// 页 0：教室在线 / 连接状态 / 账户。纯展示，backend context 直接读。
// MD3：filled 卡片（containerHigh）+ 字阶（displayS 大数字 / labelM 标签 / bodyS 辅助）。
import QtQuick
import "../components/md3"

Item {
    id: page
    property var md3: ({})

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + 24
        clip: true
        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 12

            // 教室在线（大数字卡）
            Md3Card {
                md3: page.md3
                variant: "filled"
                width: parent.width
                Row {
                    width: parent.width
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "教室在线"; color: page.md3.cOnSurfaceVar; font.pixelSize: page.md3.labelM }
                        Text {
                            text: backend.onlineCount + " 台"
                            color: page.md3.cOnSurface
                            font.pixelSize: page.md3.displayS
                            font.weight: Font.Medium
                        }
                    }
                    Item { width: parent.width - 200; height: 1 }   // 左推
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "设备总数"; color: page.md3.cOnSurfaceVar; font.pixelSize: page.md3.labelM }
                        Text {
                            text: backend.devices.length + " 台"
                            color: page.md3.cOnSurface
                            font.pixelSize: page.md3.titleL
                        }
                    }
                }
            }

            // 连接状态 + 账户
            Md3Card {
                md3: page.md3
                variant: "filled"
                width: parent.width
                Row {
                    width: parent.width
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "连接状态"; color: page.md3.cOnSurfaceVar; font.pixelSize: page.md3.labelM }
                        Text {
                            text: backend.loggedIn ? (backend.connected ? "云端就绪" : "连接断开") : "未登录"
                            color: backend.connected ? page.md3.cOk : page.md3.cError
                            font.pixelSize: page.md3.titleM
                            font.weight: Font.Medium
                        }
                    }
                    Item { width: parent.width - 200; height: 1 }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "账户"; color: page.md3.cOnSurfaceVar; font.pixelSize: page.md3.labelM }
                        Text {
                            text: backend.accountName ? backend.accountName : "未登录"
                            color: page.md3.cOnSurface
                            font.pixelSize: page.md3.bodyM
                            elide: Text.ElideRight
                            width: 90
                        }
                    }
                }
            }

            Text {
                text: "手机端为管理端伴侣：查看教室状态、发轻指令。\n远控 / 语音 / 广播 / 课表请用桌面管理端。"
                width: parent.width
                wrapMode: Text.Wrap
                color: page.md3.cOutline
                font.pixelSize: page.md3.bodyS
            }
        }
    }
}
