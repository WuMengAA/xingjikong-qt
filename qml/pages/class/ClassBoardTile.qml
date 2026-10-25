// 班级展板 · 只读设备格（2026-10-25 · 从 ClassBoardPane 抽出）
//
// 与 ClassDeviceTile 的区别：**完全不可交互** —— 没有 MouseArea、不弹手型、
// 不亮 hover、点了没有任何反应是设计如此。展板是给教室大屏看的旁观视图，
// 所以不复用那件"可点选"的格子，免得再出现"点了没反应"的错觉。
//
// 数据真源：ClassDeviceModel（typed）—— 本件只收标量属性，不碰 backend。
import QtQuick
import "../../island"          // SvgIcon

Item {
    id: root

    property var md3: ({})
    property string uid: ""
    property string name: ""
    property bool online: false
    property bool hasThumb: false
    property int thumbRev: 0

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: md3.rS
        color: md3.frCard || "#FFFFFF"
        border.width: 1
        border.color: md3.frLine || "#EEF0F3"

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            // 画面区：有缩略图给真图，没有给稳定底色 + 说明（不闪、不装故障）
            Rectangle {
                width: parent.width
                height: Math.max(48, parent.height - 30)
                radius: md3.rS
                color: md3.cCont
                clip: true

                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    asynchronous: true
                    visible: root.hasThumb
                    source: root.hasThumb
                            ? ("image://classthumb/" + root.uid + "?t" + root.thumbRev)
                            : ""
                }

                Column {
                    anchors.centerIn: parent
                    visible: !root.hasThumb
                    spacing: 4

                    SvgIcon {
                        name: "monitor"
                        px: 18
                        tint: md3.cOutline
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: root.online ? "暂无画面" : "未开机"
                        font.pixelSize: md3.bodyS
                        color: md3.cOutline
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                // 在线徽标点（右下角）
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.online ? md3.cOk : md3.cOutline
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                }
            }

            Text {
                width: parent.width
                text: root.name !== "" ? root.name : root.uid
                font.pixelSize: md3.bodyS
                font.weight: Font.Medium
                color: md3.frInk || "#111827"
                elide: Text.ElideRight
            }
        }
    }
}
