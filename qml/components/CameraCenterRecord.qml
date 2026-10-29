// 监控中心 · 「录制回放」页（2026-10-29 抽出）
// 原样保留：本机开录/停录 + 录制列表（按 uid 分组）。全部经 `cc` 取状态/调逻辑。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../island"
import "md3"

Rectangle {
    id: recCard
    property var cc
    anchors.fill: parent
    color: cc.tCard
    radius: cc.tRM

    Column {
        anchors.fill: parent
        anchors.margins: cc.tSpM
        spacing: cc.tSpS

        // 标题行：录制回放（本机） + 开录/停录 + 刷新
        Item {
            width: parent.width
            height: 32
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "录制回放（本机）"
                color: cc.tFg3
                font.pixelSize: cc.tLabelM
                font.weight: Font.Medium
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpS
                Md3Btn {
                    radius: cc.tRM
                    variant: cc.recording ? "outlined" : "filled"
                    text: cc.recording ? "停录" : "开录"
                    enabled: cc.selUid !== ""
                    tip: cc.selUid === "" ? "先在「实时」页选一台设备"
                                          : "让被控端录一段摄像头，停录后自动上传云端"
                    onClicked: cc.recording ? cc.stopRec() : cc.startRec()
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    radius: cc.tRM
                    color: recRefMa.containsMouse ? cc.tCardHi : "transparent"
                    SvgIcon {
                        anchors.centerIn: parent
                        name: "refresh-cw"
                        px: 16
                        tint: cc.tFg3
                    }
                    MouseArea {
                        id: recRefMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { cc.backend.fetchRecordings(); cc.status = "已刷新录制列表" }
                    }
                    ToolTip {
                        visible: recRefMa.containsMouse
                        delay: 400
                        text: "刷新录制列表"
                    }
                }
            }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: cc.tOutlineVar
        }
        // 回放列表（按 uid 分组）
        Flickable {
            width: parent.width
            height: parent.height - 32 - 1 - 2 * cc.tSpS
            clip: true
            contentHeight: recCol.height
            Column {
                id: recCol
                width: parent.width
                spacing: cc.tSpS
                Repeater {
                    model: Object.keys(cc.backend.recordings || {}).sort()
                    delegate: Rectangle {
                        width: parent.width
                        height: 32
                        radius: cc.tRM
                        color: cc.tCardHi
                        SvgIcon {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: "video"
                            px: 14
                            tint: cc.tFg3
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 32
                            anchors.right: recCountTx.left
                            anchors.rightMargin: cc.tSpS
                            anchors.verticalCenter: parent.verticalCenter
                            text: cc.deviceName(modelData)
                            color: cc.tFg
                            font.pixelSize: cc.tLabelM
                            elide: Text.ElideRight
                        }
                        Text {
                            id: recCountTx
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: ((cc.backend.recordings[modelData] || []).length || 0) + " 段"
                            color: cc.tFg3
                            font.pixelSize: cc.tLabelM
                        }
                    }
                }
                Text {
                    visible: Object.keys(cc.backend.recordings || {}).length === 0
                    text: "暂无回放"
                    color: cc.tFg4
                    font.pixelSize: cc.tLabelM
                    ToolTip {
                        delay: 400
                        text: "在本页点「开录」让被控端录一段，停录后自动上传云端，再点刷新"
                    }
                }
            }
        }
    }
}
