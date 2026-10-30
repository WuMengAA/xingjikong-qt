// 监控中心 · 「实时」页左卡：设备选择 + 摄像头枚举/绑定（2026-10-29 抽出）
//
// 原样保留：自绘设备下拉（Popup + ListView）、摄像头列表、＋绑定选中。
// 全部状态/逻辑经 `cc`（CameraController）取用；绑定写入抽成 cc.bindCamera(dev)。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../island"

Rectangle {
    id: camCard
    property var cc
    color: cc.tCard
    radius: cc.tRM

    Column {
        id: camTopCol
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: cc.tSpM
        spacing: cc.tSpS

        // 设备选择（已拆出为 CameraDevicePicker）
        CameraDevicePicker {
            cc: camCard.cc
        }

        // 小节标题行：摄像头列表 + 右侧刷新（图标按钮）
        Item {
            width: parent.width
            height: 24
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "摄像头列表"
                color: cc.tFg3
                font.pixelSize: cc.tLabelM
                font.weight: Font.Medium
            }
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                radius: cc.tRM
                color: refMa.containsMouse ? cc.tCardHi : "transparent"
                SvgIcon {
                    anchors.centerIn: parent
                    name: "refresh-cw"
                    px: 16
                    tint: cc.tFg3
                }
                MouseArea {
                    id: refMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cc.refreshCams()
                }
                ToolTip {
                    visible: refMa.containsMouse
                    delay: 400
                    text: "重新枚举摄像头"
                }
            }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: cc.tOutlineVar
        }
        // 计数短文案（长说明移「诊断」页）
        Text {
            width: parent.width
            text: cc.loadingCams ? "枚举中…"
                 : (cc.cameras.length === 0 ? "未枚举" : cc.cameras.length + " 个摄像头")
            color: cc.tFg4
            font.pixelSize: cc.tLabelS
            elide: Text.ElideRight
            ToolTip {
                id: camCountTip
                delay: 400
                visible: camCountTip.text !== ""
                text: cc.loadingCams ? ""
                     : (cc.cameras.length === 0
                        ? "点右上角刷新重新枚举；或确认摄像头已插好、没被其它程序占用"
                        : "")
            }
        }
    }

    // 列表区（填满卡片中部）
    Rectangle {
        anchors.top: camTopCol.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bindBtn.top
        anchors.topMargin: cc.tSpS
        anchors.leftMargin: cc.tSpM
        anchors.rightMargin: cc.tSpM
        anchors.bottomMargin: cc.tSpS
        color: "transparent"
        ListView {
            id: camList
            anchors.fill: parent
            clip: true
            model: cc.cameras
            delegate: Item {
                width: camList.width
                height: 30
                Rectangle {
                    anchors.fill: parent
                    radius: cc.tRM
                    color: camList.currentIndex === index
                           ? cc.tSegBg
                           : (rowMa.containsMouse ? cc.tCardHi : "transparent")
                }
                SvgIcon {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "video"
                    px: 14
                    tint: camList.currentIndex === index ? cc.tSegFg : cc.tFg3
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: cc.camLabel(index, modelData)
                    color: camList.currentIndex === index ? cc.tSegFg : cc.tFg
                    font.pixelSize: cc.tLabelM
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: camList.currentIndex = index
                }
            }
        }
    }

    // 绑定选中的摄像头（主操作，filled，统一 12 圆角）
    AppButton {
        id: bindBtn
        radius: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: cc.tSpM
        variant: "primary"
        text: "＋ 绑定选中"
        enabled: camList.currentIndex >= 0
        onClicked: {
            if (cc.selUid === "" || camList.currentIndex < 0) return
            cc.bindCamera(cc.cameras[camList.currentIndex])
        }
    }
}
