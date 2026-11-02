// 监控回放小窗（2026-10-26 · 产品笔记 P4）
//
// 为什么是"小窗"而不是独立播放页：笔记要求复刻 360 / 电脑管家那种
// **贴角小窗 + 弹窗模式** —— 老师看回放的同时还要能操作别的面板，
// 独立页会把整个视图抢走。所以它浮在管理端主界面上：可拖动、可贴角、可放大。
//
// 播放地址来自 backend.playbackUrl(uid, name)：那是**本机回环代理**的地址
// （云端 /recordings/* 要 Bearer 头，而 QML MediaPlayer 的 source 带不了请求头）。
// 代理转发 Range、回传 206 ⇒ 拖动进度条只取需要的字节段，不重下整个录像。
//
// 交互与提示（笔记"要保留视觉与可交互"）：播放/暂停、进度拖动、时间显示、
// 音量与静音切换、缓冲与失败文字提示、双击放大/还原、拖动释放后自动贴角。
//
// 图标优先、克制动效；**只改显示层**：不动 viewerbackend.cpp、不引入 backend
// 新接口、不塞进灵动岛（岛只属被控端，本窗是管理端浮层）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "../components"
import "../island"          // SvgIcon 唯一真源在共享岛目录（构建期拷到 qml/island/）

Item {
    id: win
    property var theme: null
    property string uid: ""
    property string recName: ""
    property string deviceLabel: ""

    // 两档尺寸：小窗（默认）/ 大窗（双击放大）。不做自由缩放 —— 省心且不糊。
    readonly property int smallW: 420
    readonly property int smallH: 268
    readonly property int largeW: 680
    readonly property int largeH: 430
    readonly property int headerH: 36      // 标题栏高
    readonly property int controlH: 44     // 控制条高
    readonly property int playBtn: 32      // 播放/暂停圆钮直径

    visible: false
    width: smallW
    height: smallH
    z: 900                      // 浮在面板之上

    function openFor(uidVal, nameVal, labelVal) {
        win.uid = uidVal
        win.recName = nameVal
        win.deviceLabel = labelVal || uidVal
        const u = backend.playbackUrl(uidVal, nameVal)
        win.visible = true
        if (!u) {
            statusText.text = "播放起不来：本机回放代理未启动"
            return
        }
        statusText.text = "载入中…"
        player.stop()
        player.source = u
        player.play()
    }

    function closeWin() {
        player.stop()
        win.visible = false
    }

    function fmt(ms) {
        if (!ms || ms < 0) ms = 0
        const total = Math.floor(ms / 1000)
        const m = Math.floor(total / 60)
        const s = total % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput { id: audioOut; volume: volSlider.value }
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) statusText.text = ""
        }
        onErrorOccurred: {
            // 401/403/404 由代理原样回传，这里只如实转述，不假成功
            statusText.text = "播放失败：" + (errorString !== "" ? errorString : "未知错误")
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadingStatus) statusText.text = "载入中…"
            else if (mediaStatus === MediaPlayer.InvalidMediaStatus) statusText.text = "这个文件放不出来"
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: win.theme ? win.theme.rM : 12
        color: win.theme ? win.theme.cSurface : "#1E1E1E"
        border.width: 0
        border.color: win.theme ? win.theme.cOutlineVar : "#3A3A3A"
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── 标题栏（拖动区）──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: win.headerH
                color: "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: win.theme ? win.theme.spS : 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: "回放 · " + (win.deviceLabel || win.uid)
                    font.pixelSize: win.theme ? win.theme.body13 : 13
                    color: win.theme ? win.theme.fg : "#FAFAFA"
                    elide: Text.ElideRight
                    width: parent.width - 56
                }
                SvgIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: win.theme ? win.theme.spS : 8
                    name: "x"
                    tint: win.theme ? win.theme.fg : "#FAFAFA"
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        onClicked: win.closeWin()
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    drag.target: win
                    drag.minimumX: 0
                    drag.minimumY: 0
                    onDoubleClicked: {
                        // 双击放大/还原（小窗与大窗两档，不做自由缩放 —— 省心且不糊）
                        if (win.width > win.smallW + 1) { win.width = win.smallW; win.height = win.smallH }
                        else { win.width = win.largeW; win.height = win.largeH }
                    }
                    onReleased: {
                        // 贴角吸附：拖到哪边就贴哪边的角（电脑管家那种植入感）
                        const hostW = win.parent ? win.parent.width : 1200
                        const hostH = win.parent ? win.parent.height : 800
                        const midX = win.x + win.width / 2
                        const midY = win.y + win.height / 2
                        win.x = midX > hostW / 2 ? hostW - win.width - 16 : 16
                        win.y = midY > hostH / 2 ? hostH - win.height - 16 : 16
                    }
                }
            }

            // ── 画面 ──
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                VideoOutput {
                    id: videoOut
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectFit
                }

                Text {
                    id: statusText
                    anchors.centerIn: parent
                    width: parent.width - 32
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: win.theme ? win.theme.bodyS : 12
                    color: win.theme ? win.theme.op : "#C8C8C8"
                    text: ""
                }
            }

            // ── 控制条 ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: win.controlH
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: win.theme ? win.theme.spS : 8
                    anchors.rightMargin: win.theme ? win.theme.spS : 8
                    spacing: win.theme ? win.theme.spS : 8

                    // 播放/暂停（图标只有 play / square 可用，暂停用方形停符）
                    Rectangle {
                        Layout.preferredWidth: win.playBtn; Layout.preferredHeight: win.playBtn
                        radius: win.theme ? win.theme.rFull : 16
                        color: win.theme ? win.theme.cPrimary : "#3B82F6"
                        SvgIcon {
                            anchors.centerIn: parent
                            name: player.playbackState === MediaPlayer.PlayingState ? "square" : "play"
                            // ⚠️ SvgIcon 的着色属性叫 tint（不是 color）—— 写 color 会
                            //    "Cannot assign to non-existent property"，整块 QML 加载不出来
                            tint: win.theme ? win.theme.cOnPrimary : "#FFFFFF"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (player.playbackState === MediaPlayer.PlayingState) player.pause()
                                else player.play()
                            }
                        }
                    }

                    Text {
                        text: win.fmt(player.position)
                        font.pixelSize: win.theme ? win.theme.bodyS : 12
                        color: win.theme ? win.theme.fg : "#FAFAFA"
                    }

                    Slider {
                        id: seek
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, player.duration)
                        enabled: player.seekable
                        value: player.position
                        // 拖动中不要被 position 回写打断（否则"拖了又跳回去"）
                        onMoved: player.setPosition(value)
                    }

                    Text {
                        text: win.fmt(player.duration)
                        font.pixelSize: win.theme ? win.theme.bodyS : 12
                        color: win.theme ? win.theme.op : "#C8C8C8"
                    }

                    // 音量 / 静音切换
                    SvgIcon {
                        name: "volume-2"
                        tint: win.theme ? win.theme.fg : "#FAFAFA"
                        opacity: audioOut.muted ? 0.4 : 1.0
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            onClicked: audioOut.muted = !audioOut.muted
                        }
                    }
                    Slider {
                        id: volSlider
                        Layout.preferredWidth: 64
                        from: 0.0
                        to: 1.0
                        value: 0.8
                    }
                }
            }
        }
    }

    Component.onCompleted: player.videoOutput = videoOut
}
