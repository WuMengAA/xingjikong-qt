// 监控中心 · 实时页左卡 · 设备选择下拉（2026-10-30 从 CameraCenterDevice 拆出）
//
// 原样保留：自绘设备下拉（Popup + ListView）。全部状态/逻辑经 `cc`（CameraController）取用。
import QtQuick
import QtQuick.Controls

Item {
    id: picker
    // 控制器（CameraController：selUid / deviceUids / deviceName / refreshCams / t* 令牌）
    property var cc

    width: parent.width
    height: 32

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: "设备"
        color: cc.tFg3
        font.pixelSize: cc.tLabelM
    }
    Rectangle {
        id: devSel
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 44
        height: 32
        radius: cc.tRM
        color: devMa.containsMouse ? cc.tCardHighest : cc.tCardHi
        Text {
            anchors.left: parent.left
            anchors.leftMargin: cc.tSpM
            anchors.right: caret.left
            anchors.rightMargin: cc.tSpS
            anchors.verticalCenter: parent.verticalCenter
            text: cc.selUid !== "" ? cc.deviceName(cc.selUid)
                                 : (cc.deviceUids().length === 0 ? "（无在线设备）" : "请选择设备")
            color: cc.tFg
            font.pixelSize: cc.tLabelM
            elide: Text.ElideRight
        }
        Text {
            id: caret
            anchors.right: parent.right
            anchors.rightMargin: cc.tSpS
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: cc.tFg3
            font.pixelSize: cc.tLabelM
        }
        MouseArea {
            id: devMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: devMenu.opened ? devMenu.close() : devMenu.open()
        }
        Popup {
            id: devMenu
            x: 0
            y: devSel.height + 4
            width: devSel.width
            padding: 4
            background: Rectangle {
                color: cc.tCardHighest
                radius: cc.tRM
            }
            contentItem: ListView {
                implicitHeight: Math.min(contentHeight, 220)
                clip: true
                model: cc.deviceUids()
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 28
                    radius: cc.tRM
                    color: (cc.selUid === modelData)
                           ? cc.tSegBg
                           : (devItemMa.containsMouse ? cc.tCardHi : "transparent")
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: cc.tSpS
                        anchors.right: parent.right
                        anchors.rightMargin: cc.tSpS
                        text: modelData
                        color: (cc.selUid === modelData) ? cc.tSegFg : cc.tFg
                        font.pixelSize: cc.tLabelM
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: devItemMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            cc.selUid = modelData
                            devMenu.close()
                            cc.refreshCams()
                        }
                    }
                }
            }
        }
    }
}
