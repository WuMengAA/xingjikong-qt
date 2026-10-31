// A档（2026-10-31）：H264 远控流小窗（image://h264/live）。
// ⚠️ 设计：H264ImageProvider 是"按需取最新帧"的 provider，QML 必须周期性重新请求
//   才能拉到新帧 → 用 Timer 每 100ms 递增 tick 触发 requestImage。
// 真机 MF 解码出图时此窗实时显示；沙箱 MF 0 产出时 provider 缓存空 → 显示空白（A 档预期）。
// 遵循 §10.9：QML 只取图、不解析；遵循 §9.2 令牌从 Theme 取。
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property bool active: backend.h264Available   // 有 h264-data 到达才显示
    visible: active
    clip: true

    property int tick: 0
    Timer {
        id: h264Tick
        interval: 100
        running: root.active
        repeat: true
        onTriggered: root.tick++
    }

    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        cache: false
        source: root.active ? ("image://h264/live?" + root.tick) : ""
    }

    // 角标：标明这是 H264 通道（与 VNC 主画面区分）
    Rectangle {
        anchors { right: parent.right; top: parent.top; margins: 4 }
        width: badge.width + 10; height: badge.height + 6
        radius: 6
        color: "#2FB8C6"
        visible: root.active
        Text {
            id: badge
            anchors.centerIn: parent
            text: "H264"
            color: "#FFFFFF"
            font { pixelSize: 11; bold: true }
        }
    }
}
