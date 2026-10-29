// 星集控 · 手机端概览页（2026-10-13 · MD3 迁移批）
// 页 0：教室在线 / 连接状态 / 账户。纯展示，backend context 直接读。
// MD3：filled 卡片（containerHigh）+ 字阶（displayS 大数字 / labelM 标签 / bodyS 辅助）。
import QtQuick
import QtQuick.Layouts
import "../components"         // AppCard
import "../components/md3"
import "../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: page

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
            AppCard {
                shadow: false                  // 原 variant: "filled"（无阴影）
                width: parent.width
                Row {
                    Layout.fillWidth: true
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "教室在线"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
                        Text {
                            text: backend.onlineCount + " 台"
                            color: Md3.cOnSurface
                            font.pixelSize: Md3.displayS
                            font.weight: Font.Medium
                        }
                    }
                    Item { width: parent.width - 200; height: 1 }   // 左推
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "设备总数"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
                        Text {
                            text: backend.devices.length + " 台"
                            color: Md3.cOnSurface
                            font.pixelSize: Md3.titleL
                        }
                    }
                }
            }

            // 连接状态 + 账户
            AppCard {
                shadow: false                  // 原 variant: "filled"（无阴影）
                width: parent.width
                Row {
                    Layout.fillWidth: true
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "连接状态"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
                        Text {
                            text: backend.loggedIn ? (backend.connected ? "云端就绪" : "连接断开") : "未登录"
                            color: backend.connected ? Md3.cOk : Md3.cError
                            font.pixelSize: Md3.titleM
                            font.weight: Font.Medium
                        }
                    }
                    Item { width: parent.width - 200; height: 1 }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { text: "账户"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
                        Text {
                            text: backend.accountName ? backend.accountName : "未登录"
                            color: Md3.cOnSurface
                            font.pixelSize: Md3.bodyM
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
                color: Md3.cOutline
                font.pixelSize: Md3.bodyS
            }
        }
    }
}
