// 监控中心 · 「实时」页右卡：实时画面台（屏幕 + 摄像头双画面，2026-10-29 抽出）
//
// 持有 camSync()（按需启停单一收口：期望推流设备 = 摄像头格可见且有选中设备时的 selUid）。
// camSync 下沉到此文件，因为它依赖 camPane.visible；由 camPane 的首次挂载 / 可见性变化，
// 以及 cc.selUid 变化三处触发（原 onSelUidChanged 改为 Connections 转发）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../island"
import "md3"

Rectangle {
    id: liveCard
    property var cc
    // 由父（CameraCenterLive）注入：Live 标签本身是否可见（切到别的标签时停推流）。
    property bool liveRootVisible: true
    color: cc.tCard
    radius: cc.tRM

    Column {
        anchors.fill: parent
        anchors.margins: cc.tSpM
        spacing: cc.tSpS

        // 模式切换：共享组件 Md3SegmentedButton
        Md3SegmentedButton {
            id: segRow
            anchors.horizontalCenter: parent.horizontalCenter
            model: [
                { value: 1, label: "仅屏幕",   icon: "monitor",     tip: "只看被控端屏幕" },
                { value: 0, label: "",         icon: "layout-grid", tip: "双画面：屏幕 + 摄像头" },
                { value: 2, label: "仅摄像头", icon: "video",       tip: "只看摄像头实时流" }
            ]
            current: cc.layoutMode
            onActivated: function (v) { cc.layoutMode = v }
        }

        // 画面台（最深底色，像一块"屏幕"）
        Rectangle {
            id: stage
            width: parent.width
            height: parent.height - segRow.height - parent.spacing
            color: cc.tStage
            radius: cc.tRM
            clip: true
            Row {
                id: paneRow
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                // 屏幕画面（真实源 image://frames/<uid>）
                Rectangle {
                    visible: cc.layoutMode !== 2
                    width: cc.layoutMode === 0
                           ? ((paneRow.width - 4) * 3 / 5)
                           : paneRow.width
                    height: paneRow.height
                    color: "transparent"
                    clip: true
                    Image {
                        id: screenImg
                        anchors.fill: parent
                        anchors.margins: 2
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        property int tick: 0
                        source: cc.selUid ? ("image://frames/" + cc.selUid + "?t" + tick) : ""
                        Connections {
                            target: cc.backend
                            function onThumbnailChanged(uid) {
                                screenImg.tick++
                                if (uid === cc.selUid || uid === "") cc.screenHasFrame = true
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        text: cc.selUid === "" ? "暂无画面" : (cc.screenHasFrame ? "" : "连接中...")
                        color: cc.tFg3
                        font.pixelSize: cc.tLabelM
                        ToolTip {
                            id: scrTip
                            delay: 400
                            visible: scrTip.text !== ""
                            text: cc.selUid === ""
                                  ? "请先在左侧列表中选择一台设备"
                                  : (cc.screenHasFrame ? ""
                                     : "已请求画面，还没收到首帧；被控端不在线时会一直连不上")
                        }
                    }
                }
                // 摄像头画面（方案 A：RTC second track "camera"）
                Rectangle {
                    id: camPane
                    visible: cc.layoutMode !== 1 && liveRootVisible
                    width: cc.layoutMode === 0
                           ? ((paneRow.width - 4) * 2 / 5)
                           : paneRow.width
                    height: paneRow.height
                    color: "transparent"
                    clip: true
                    Image {
                        id: camImg
                        anchors.fill: parent
                        anchors.margins: 2
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        property int camTick: 0
                        source: (cc.camStreamUid !== "" && cc.camHasFrame)
                                ? ("image://frames/cam?t" + camTick) : ""
                        Connections {
                            target: cc.backend
                            function onCameraFrameChanged() {
                                cc.camHasFrame = true
                                camImg.camTick++
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        text: cc.camStreamUid === "" ? "暂无画面" : (cc.camHasFrame ? "" : "连接中...")
                        color: cc.tFg3
                        font.pixelSize: cc.tLabelM
                        ToolTip {
                            id: camTip
                            delay: 400
                            visible: camTip.text !== ""
                            text: cc.camStreamUid === ""
                                  ? "切到「双画面 / 仅摄像头」即按需请被控端推流（不看不采）"
                                  : (cc.camHasFrame ? ""
                                     : "已请求推流，还没收到首帧；被控端无摄像头/被占用时会一直无画面")
                        }
                    }

                    // 可见性变化（切标签/切单屏/切双画面）→ 同步推流状态
                    onVisibleChanged: camSync()
                    Component.onCompleted: camSync()
                }
            }
        }
    }

    // ── 摄像头实时画面按需启停单一收口 ──
    // 期望推流设备 = 摄像头格可见且有选中设备时的 selUid，否则为空。camSync() 比较期望与实际，
    // 只在变化时发 start/stop（隐私红线：不看不采）。由 camPane 挂载/可见变化 + selUid 变化触发。
    function camSync() {
        var want = (camPane.visible && cc.selUid !== "") ? cc.selUid : ""
        if (want === cc.camStreamUid) return
        if (cc.camStreamUid !== "") cc.backend.sendAction("camera_stream_stop", {})
        cc.camStreamUid = want
        cc.camHasFrame = false
        if (want !== "") {
            cc.backend.sendAction("camera_stream_start", cc.paramsWithDev())
            cc.status = "正在开摄像头（" + want + "）…"
        }
    }
    Connections {
        target: cc
        function onSelUidChanged() { camSync(); cc.screenHasFrame = false }
    }
}
