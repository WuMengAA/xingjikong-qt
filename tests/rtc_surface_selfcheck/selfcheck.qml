// #171 管理器侧 sink 自检 QML（headless 用）
// 复刻 Main.qml 的接线：Connections 接 mockBackend.rtcFrameReady → vidSurf.onFrame(img)
import QtQuick

Item {
    id: root
    objectName: "rootItem"
    width: 320; height: 200

    RtcVideoSurface {
        id: vidSurf
        objectName: "vidSurf"
        theme: ({})              // 组件内所有令牌都有 || fallback，空对象即可
        anchors.fill: parent
    }

    // 与 Main.qml 相同接法（仅 target 换成 mockBackend）
    Connections {
        target: mockBackend
        onRtcFrameReady: vidSurf.onFrame(img)
    }
}
