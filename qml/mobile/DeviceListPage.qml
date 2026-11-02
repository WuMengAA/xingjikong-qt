// 页 1：ListView，行高 76（触摸友好 ≥64）。行点击 → openDetail 信号（Main 开抽屉）；
// 行内 ping / 关机快捷（shutdownRequested 信号 → Main 开确认弹窗）。
import QtQuick
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: page
    signal openDetail(var dev)
    signal shutdownRequested(string uid)

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: backend.devices
        spacing: 8
        delegate: Item {
            width: list.width
            height: 76
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                radius: Theme.radius.md
                color: Theme.color.surface
                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: (modelData.online !== false) ? Theme.color.success : Theme.color.destructive
                    }
                    Column {
                        width: parent.width - 170
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                            color: Theme.color.textPrimary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Text {
                            text: (modelData.version ? "v" + modelData.version : "") + " · " + modelData.uid
                            color: Theme.color.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Rectangle {
                            width: 52; height: 36; radius: 18
                            color: pingMa.pressed ? Theme.color.surfaceVariant : (pingMa.containsMouse ? Theme.color.surfaceVariant : "transparent")
                            Text { anchors.centerIn: parent; text: "ping"; color: Theme.color.primary; font.pixelSize: 14; font.weight: Font.Medium }
                            MouseArea { id: pingMa; anchors.fill: parent; hoverEnabled: true; onClicked: backend.sendAction("ping", {}) }
                        }
                        Rectangle {
                            width: 52; height: 36; radius: 18
                            color: sdMa.pressed ? Theme.color.destructive : (sdMa.containsMouse ? Theme.color.destructive : "transparent")
                            Text { anchors.centerIn: parent; text: "关机"; color: Theme.color.destructive; font.pixelSize: 14; font.weight: Font.Medium }
                            MouseArea { id: sdMa; anchors.fill: parent; hoverEnabled: true; onClicked: page.shutdownRequested(modelData.uid) }
                        }
                    }
                    // 整行点击 → 设备详情抽屉
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.openDetail(modelData)
                    }
                }
            }
        }
    }
}
