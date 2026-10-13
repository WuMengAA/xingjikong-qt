// 星集控 · 手机端监控页（2026-10-13 · MD3 迁移批）
// 页 2：设备横条选择 + RtcVideoSurface 实时画面 + 停止。
// 自身接 backend.rtcFrameReady（不依赖 Main 接线——内聚）。
// MD3：filter chip 选设备 + mini FAB 停止。
import QtQuick
import "../components"
import "../components/md3"

Item {
    id: page
    property var md3: ({})

    // 设备选择横条（MD3 filter chip）
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        color: page.md3.cSurface
        Row {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Text {
                text: "看哪台："
                color: page.md3.cOnSurfaceVar
                font.pixelSize: page.md3.labelL
                anchors.verticalCenter: parent.verticalCenter
            }
            ListView {
                id: watchList
                orientation: ListView.Horizontal
                clip: true
                width: parent.width - 76
                height: 36
                anchors.verticalCenter: parent.verticalCenter
                model: backend.devices
                spacing: 8
                delegate: Md3Chip {
                    md3: page.md3
                    text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                    selected: backend.currentUid === modelData.uid
                    onClicked: { backend.currentUid = modelData.uid; backend.sendAction("camera_start", {}) }
                }
            }
        }
    }

    // 实时画面（RtcVideoSurface：WorkBuddy #171 修复后走 provider tick 取帧）
    RtcVideoSurface {
        id: vidSurf
        anchors.top: parent.top
        anchors.topMargin: 66
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        theme: page.md3        // legacy 别名桥：RtcVideoSurface 读 th.*，Md3 全量提供
    }

    // 停止画面（MD3 mini FAB）
    Md3Fab {
        md3: page.md3
        anchors.top: parent.top
        anchors.topMargin: 66 + 10
        anchors.right: parent.right
        anchors.rightMargin: 20
        size: "mini"
        icon: "⏹"
        onClicked: backend.sendAction("camera_stop", {})
    }

    Component.onCompleted: {
        // 实时画面接线：viewerbackend rtcFrameReady → RtcVideoSurface
        backend.rtcFrameReady.connect(function (img) { vidSurf.onFrame(img) })
    }
}
