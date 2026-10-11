// 星集控 · 手机端设备列表页（2026-10-11 · 独立 QML 方案）
// 页 1：ListView，行高 68（触摸友好 ≥64）。行点击 → openDetail 信号（Main 开抽屉）；
// 行内 ping 快捷 + 关机（shutdownRequested 信号 → Main 开确认弹窗）。
import QtQuick

Item {
    id: page
    property var th: ({})
    signal openDetail(var dev)
    signal shutdownRequested(string uid)

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: backend.devices
        delegate: Item {
            width: list.width
            height: 68
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                radius: page.th.rCard
                color: page.th.panel
                border.color: page.th.stroke
                border.width: 1
                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: (modelData.online !== false) ? page.th.ok : page.th.err
                    }
                    Column {
                        width: parent.width - 150
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                            color: page.th.fg; font.pixelSize: 14; elide: Text.ElideRight; width: parent.width
                        }
                        Text {
                            text: (modelData.version ? "v" + modelData.version : "") + " · " + modelData.uid
                            color: page.th.fg3; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width
                        }
                    }
                    // 轻指令：ping / 关机（行内小按钮）
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Rectangle {
                            width: 40; height: 26; radius: 13
                            color: page.th.hover; border.color: page.th.stroke; border.width: 1
                            Text { anchors.centerIn: parent; text: "ping"; color: page.th.acc; font.pixelSize: 11 }
                            MouseArea { anchors.fill: parent; onClicked: backend.sendAction("ping", {}) }
                        }
                        Rectangle {
                            width: 40; height: 26; radius: 13
                            color: page.th.err
                            Text { anchors.centerIn: parent; text: "关机"; color: "#fff"; font.pixelSize: 11 }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: page.shutdownRequested(modelData.uid)
                            }
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
