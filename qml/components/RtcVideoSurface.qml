// 星集控 · 管理端 WebRTC 实时画面显示组件（2026-10-09 · 独立组件）
//
// 用途：消费 ldc_receiver 的 frameReady(QImage) 信号，在 QML 里显示
// 被控端实时画面（libdatachannel 收流，替换原 QWebEngineView 离屏流）。
// 独立 QObject 对接层不做在这里——viewerbackend 接线时把信号喂给本组件的
// frameReady 方法即可；或由 QML 直接 connect。
//
// 设计：
//   · 无头环境可用（纯显示组件，不依赖真实视频源）
//   · 空帧显示占位（"暂无实时画面"），有帧才亮
//   · 维护最近一帧 + 帧统计（诊断用）
//
// 用法（等 Main.qml 工作区解禁后挂）：
//   RtcVideoSurface { theme: root.th; frameProvider: backend; }  // 接线层调
//   provider 侧（viewerbackend）在 frameReady 时调 surface.onFrame(img)
import QtQuick

Item {
    id: surf

    // ── 外部接口 ──
    property var theme: ({})
    property int framesReceived: 0      // 累计帧数（诊断）
    property bool connected: false

    // 自有 tick：每次有帧进来就 +1，驱动 image://frames/f?<tick> 重新取图。
    // 2026-10-11 修 #171：原实现 `videoImg.source = img`（QImage 直赋）在 Qt 6.12
    // 会报 "Cannot assign QImage to QUrl"，且 img.width 在 QML 侧取不到（QVariant 包装）。
    // 改为复用已验证可用的 FrameImageProvider（image://frames/f），RTC 帧现在也会落进
    // backend.m_frame（见 emitLdcFrame），所以这里照 jpeg 路径的取图方式即可显示。
    property int tick: 0

    // 由接线层调用：每帧进来 → 触发重取 provider 图
    function onFrame(img) {
        // img 可为 QImage（QVariant）或任意真值；只要非空就换 tick 拉新帧。
        // 这里不再读 img.width（QVariant 包装的 QImage 取不到），改用 provider 自证：
        // 帧有效时 backend 已写入 m_frame，provider 会回真实尺寸。
        if (img === undefined || img === null) return
        surf.tick++
        surf.framesReceived++
        videoImg.source = "image://frames/f?" + surf.tick
    }

    function onStateChanged(on) {
        surf.connected = on
    }

    // ── 显示 ──
    Rectangle {
        anchors.fill: parent
        radius: surf.theme.rCard || 12
        color: surf.theme.cream || "#141414"
        border.color: surf.theme.stroke || "#242424"
        border.width: 1
        clip: true

        Image {
            id: videoImg
            anchors.fill: parent
            anchors.margins: 4
            fillMode: Image.PreserveAspectFit
            visible: surf.framesReceived > 0
            smooth: true
        }

        // 占位（无帧）
        Column {
            anchors.centerIn: parent
            spacing: 6
            visible: surf.framesReceived === 0
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: surf.connected ? "正在建立实时画面…" : "暂无实时画面"
                color: surf.theme.fg4 || "#707070"
                font.pixelSize: 12
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "（选择设备后启动摄像头推流）"
                color: surf.theme.fg4 || "#707070"
                font.pixelSize: 10
            }
        }

        // 角标：连接状态 + 帧计数
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            radius: 4
            // 状态色读主题令牌（彩色规范 2026-10-09）：ok=在线 / err=离线；
            // fallback 深色版，浅色模式由 Main.qml 的 th 切换提供
            color: surf.connected ? (surf.theme.ok || "#2E9E5B") : (surf.theme.err || "#D64545")
            width: chipRow.width + 12
            height: 18
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 4
                Text {
                    text: surf.connected ? "实时" : "离线"
                    color: "#FFFFFF"
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
                Text {
                    text: surf.framesReceived > 0 ? ("· " + surf.framesReceived + " 帧") : ""
                    color: "#EEEEEE"
                    font.pixelSize: 10
                    visible: surf.framesReceived > 0
                }
            }
        }
    }
}