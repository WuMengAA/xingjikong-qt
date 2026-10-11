// #171 管理器侧 sink 自检 QML（headless 夹具）
// 放在 qml/ 下，与 Main.qml 同目录 —— 这样 import "components" 能像 Main.qml 一样解析到
// RtcVideoSurface.qml；接线方式与 MobileMain.qml:566 逐字一致。
// 验收点：onFrame 被调用后 framesReceived 递增（证明「rtcFrameReady → 组件计数」链路通）。
// 注：真实显示走 image://frames/f provider（本夹具无 provider，故只验到计数这一层）。
import QtQuick
import "components"

Item {
    id: root
    objectName: "rootItem"
    width: 320; height: 200

    property int signalHits: 0

    RtcVideoSurface {
        id: vidSurf
        objectName: "vidSurf"
        theme: ({})              // 组件内所有令牌都有 || fallback，空对象即可
        anchors.fill: parent
    }

    // 与 MobileMain.qml 完全相同的接线方式
    Component.onCompleted: {
        mockBackend.rtcFrameReady.connect(function (img) {
            root.signalHits += 1
            vidSurf.onFrame(img)
        })
        pumpTimer.start()
    }

    Timer {
        id: pumpTimer
        interval: 80
        repeat: false
        onTriggered: {
            mockBackend.pump()
            console.log("[probe] after pump, signalHits=" + root.signalHits
                        + " framesReceived=" + vidSurf.framesReceived
                        + " tick=" + vidSurf.tick)
        }
    }
}
