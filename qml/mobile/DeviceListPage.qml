// 星集控 · 手机端设备列表页（2026-10-13 · MD3 迁移批）
// 页 1：ListView，行高 76（触摸友好 ≥64）。行点击 → openDetail 信号（Main 开抽屉）；
// 行内 ping / 关机快捷（shutdownRequested 信号 → Main 开确认弹窗）。
// MD3：filled 卡片行（containerHigh / 圆角 12）+ 状态点 + text 按钮（primary / error）。
import QtQuick
import "../components/theme"   // Md3 令牌（pragma Singleton）

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
                radius: Md3.rM
                color: Md3.cContHigh
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
                        color: (modelData.online !== false) ? Md3.cOk : Md3.cError
                    }
                    Column {
                        width: parent.width - 170
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                            color: Md3.cOnSurface
                            font.pixelSize: Md3.bodyL
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Text {
                            text: (modelData.version ? "v" + modelData.version : "") + " · " + modelData.uid
                            color: Md3.cOnSurfaceVar
                            font.pixelSize: Md3.bodyS
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    // 轻指令：ping / 关机（MD3 text 按钮：labelL + primary/error）
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Rectangle {
                            width: 52; height: 36; radius: 18
                            color: pingMa.pressed ? Md3.cContHighest : (pingMa.containsMouse ? Md3.cContHighest : "transparent")
                            Text { anchors.centerIn: parent; text: "ping"; color: Md3.cPrimary; font.pixelSize: Md3.labelL; font.weight: Font.Medium }
                            MouseArea { id: pingMa; anchors.fill: parent; hoverEnabled: true; onClicked: backend.sendAction("ping", {}) }
                        }
                        Rectangle {
                            width: 52; height: 36; radius: 18
                            color: sdMa.pressed ? Md3.cErrorCont : (sdMa.containsMouse ? Md3.cErrorCont : "transparent")
                            Text { anchors.centerIn: parent; text: "关机"; color: Md3.cError; font.pixelSize: Md3.labelL; font.weight: Font.Medium }
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
