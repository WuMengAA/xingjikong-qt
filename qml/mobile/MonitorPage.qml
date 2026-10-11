// 星集控 · 手机端监控页（2026-10-11 · 独立 QML 方案）
// 页 2：设备横条选择 + RtcVideoSurface 实时画面 + 停止。
// 自身接 backend.rtcFrameReady（不依赖 Main 接线——内聚）。
import QtQuick
import "../components"

Item {
    id: page
    property var th: ({})

    // 设备选择横条
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: page.th.body
        Row {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Text { text: "看哪台："; color: page.th.fg3; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            ListView {
                id: watchList
                orientation: ListView.Horizontal
                clip: true
                width: parent.width - 48
                height: 34
                anchors.verticalCenter: parent.verticalCenter
                model: backend.devices
                delegate: Rectangle {
                    height: 30
                    width: Math.max(64, devLbl.implicitWidth + 16)
                    radius: 15
                    anchors.verticalCenter: parent.verticalCenter
                    color: (backend.currentUid === modelData.uid) ? page.th.inv : page.th.hover
                    border.color: (backend.currentUid === modelData.uid) ? page.th.inv : page.th.stroke
                    border.width: 1
                    Text {
                        id: devLbl
                        anchors.centerIn: parent
                        text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                        color: (backend.currentUid === modelData.uid) ? page.th.win : page.th.fg3
                        font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { backend.currentUid = modelData.uid; backend.sendAction("camera_start", {}) }
                    }
                }
            }
        }
    }

    // 实时画面（RtcVideoSurface：WorkBuddy #171 修复后走 provider tick 取帧）
    RtcVideoSurface {
        id: vidSurf
        anchors.top: parent.top
        anchors.topMargin: 50
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        theme: page.th
    }

    // 停止画面按钮
    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 50 + 8
        anchors.right: parent.right
        anchors.rightMargin: 14
        width: 64; height: 26; radius: 13
        color: page.th.hover; border.color: page.th.stroke; border.width: 1
        Text { anchors.centerIn: parent; text: "停止"; color: page.th.fg3; font.pixelSize: 11 }
        MouseArea { anchors.fill: parent; onClicked: backend.sendAction("camera_stop", {}) }
    }

    Component.onCompleted: {
        // 实时画面接线：viewerbackend rtcFrameReady → RtcVideoSurface
        backend.rtcFrameReady.connect(function (img) { vidSurf.onFrame(img) })
    }
}
