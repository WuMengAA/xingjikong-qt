// 监控页（整屏页）· QtShadcn 规范（2026-10-29 迁移）
// ───────────────────────────────────────────────────────────────────────────
// 解决的问题（用户反馈 2026-10-19）：「监控没有独立界面和操作 UI」——
// 本页是**监控中心**一页收拢：
//   ① 设备选择（chip 横条，与手机端 MonitorPage 同一套心智）
//   ② 主画面（实时帧流 image://frames/f + 远控 VNC 帧流 image://vnc/live）
//   ③ 录像回放 / 操作栏
//
// 契约（同 ControlPage/SettingsPage）：ctx.window = 主界面 root；
//   backend = C++ 上下文属性（本文件内可直接访问）
//
// ── 2026-10-29 迁移要点 ────────────────────────────────────────────────
// · `th.*`（旧 Theme 实例，14 处十六进制兜底）→ `Theme.*` 语义令牌，**兜底 hex 全删**
// · **删 4 处 border**（主画面 / 徽标）—— 规范铁律，改由底色差分区
// · **修掉既有 Bug**：原 3 个按钮没有 text（摄像头/返回/停止）⇒ 是空白按钮，
//   用户不知道点哪个。本次补上文案。
// · ⚠️ 保留 `Flow`：规范只列 Column/Row/Grid/StackLayout，但本页明确要
//   「窗口窄时自动换行，不挤成一团」；Flow 是 Qt 的自然换行容器（非绝对坐标），
//   换成 GridLayout 会变等宽格、破坏原意图。此处优先保留行为，已记录为已知偏离。
import QtQuick
import QtQuick.Layouts
import "../../components"        // AppButton / AppChip
import "../../components/theme"  // Theme 令牌

Item {
    id: page

    property var ctx

    /** 返回出口。内嵌进新外壳（MainWindow 的 monitor section）时由外壳注入，
     *  点一下切回仪表盘；旧壳 page 3 不注入，回落原来的"跳回集控页"。 */
    property var backFn: null

    // 2026-10-26 P4：当前设备的录像列表。
    readonly property var recs: {
        const u = backend.currentUid
        if (!u) return []
        const o = backend.recordings
        if (!o || !o[u]) return []
        return o[u] || []
    }

    Rectangle { anchors.fill: parent; color: Theme.color.background }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.md - 2
        spacing: Theme.spacing.sm + 2

        // ── ① 设备选择横条 ────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            radius: Theme.radius.md
            color: Theme.color.surfaceVariant
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.sm + 4
                anchors.rightMargin: Theme.spacing.sm + 4
                spacing: Theme.spacing.sm

                Text {
                    text: "看哪台："
                    color: Theme.color.textSecondary
                    font.pixelSize: 13
                }

                ListView {
                    id: watchList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: Theme.spacing.sm
                    model: backend.devices
                    delegate: AppChip {
                        text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                        selected: backend.currentUid === modelData.uid
                        onClicked: {
                            backend.currentUid = modelData.uid
                            ctx.window.currentUid = backend.currentUid
                            // 选中的那台要真的开始拉帧（与集控页同语义）
                            backend.sendAction("screenshot", {})
                        }
                    }
                }
            }
        }

        // ── ② 主画面（实时帧 / 远控 VNC 双源）──────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius.md
            color: Theme.color.surface
            // 无边框（规范）：靠 surface 与 background 的色差分区
            clip: true

            Image {
                id: frameImg
                anchors.fill: parent
                anchors.margins: Theme.spacing.sm
                fillMode: Image.PreserveAspectFit
                cache: false
                visible: backend.frameSource !== "vnc"
                property int tick: 0
                source: (ctx.window.frameReady && backend.frameSource !== "vnc") ? ("image://frames/f?" + tick) : ""
                Connections {
                    target: backend
                    function onFrameChanged() { frameImg.tick++ }
                }
            }

            Image {
                id: vncImg
                anchors.fill: parent
                anchors.margins: Theme.spacing.sm
                fillMode: Image.PreserveAspectFit
                cache: false
                visible: backend.frameSource === "vnc"
                property int tick: 0
                source: backend.frameSource === "vnc" ? ("image://vnc/live?" + tick) : ""
                Connections {
                    target: backend
                    function onVncFrameReady(img) { vncImg.tick++ }
                }
            }

            // A档（2026-10-31）：H264 远控流小窗（右下角）
            H264StreamView {
                anchors { right: parent.right; bottom: parent.bottom; margins: 12 }
                width: Math.min(parent.width * 0.28, 320); height: width * 9 / 16
            }

            // 未选设备 / 无帧时的引导（不弹空白）
            Text {
                anchors.centerIn: parent
                text: {
                    if (backend.frameSource === "vnc") return ""
                    if (!backend.loggedIn) return "还没登录 —— 点右上角账户先登录"
                    if (backend.currentUid === "") return "先在顶部选一台设备看监控"
                    if (!ctx.window.frameReady) return "正在等该设备的画面…"
                    return ""
                }
                visible: text !== ""
                color: Theme.color.textSecondary
                font.pixelSize: 14
            }

            // 链路徽标（四态各自用令牌；无边框）
            Rectangle {
                id: badge
                visible: backend.currentUid !== ""
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                height: 20
                width: badgeTxt.implicitWidth + Theme.spacing.sm * 2
                radius: Theme.radius.sm - 4

                readonly property bool isVnc: backend.frameSource === "vnc"
                readonly property bool isBusy: ctx.window.linkBusy
                readonly property bool isJpeg: backend.frameSource === "jpeg"

                color: isVnc  ? Theme.color.primary
                     : isBusy ? Qt.rgba(Theme.color.warning.r, Theme.color.warning.g,
                                        Theme.color.warning.b, 0.15)
                     : isJpeg ? Theme.color.surfaceVariant
                              : Qt.rgba(Theme.color.success.r, Theme.color.success.g,
                                        Theme.color.success.b, 0.15)

                Text {
                    id: badgeTxt
                    anchors.centerIn: parent
                    text: badge.isVnc ? "远控中"
                        : badge.isBusy ? "正在接通…"
                        : badge.isJpeg ? "轮询"
                        : "实时"
                    color: badge.isVnc  ? Theme.color.primaryForeground
                         : badge.isBusy ? Theme.color.warning
                         : badge.isJpeg ? Theme.color.textSecondary
                                        : Theme.color.success
                    font.pixelSize: 11
                }
            }
        }

        // ── ③ 录像回放（2026-10-26 · P4）──────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.xs + 2
            visible: page.recs.length > 0

            Text {
                text: "录像回放"
                font.pixelSize: 13
                color: Theme.color.textPrimary
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing.sm
                Repeater {
                    model: page.recs
                    AppButton {
                        required property var modelData
                        text: (modelData && modelData.name)
                              ? modelData.name.replace("rec-", "").replace(".mp4", "")
                              : "录像"
                        variant: "secondary"
                        radius: 12            // 局部例外（12px 点单范围）
                        onClicked: {
                            if (!modelData) return
                            if (ctx.window && ctx.window.openPlayback)
                                ctx.window.openPlayback(backend.currentUid, modelData.name,
                                                        backend.currentUid)
                        }
                    }
                }
            }
        }

        // ── ④ 操作栏 ──────────────────────────────────────────────────
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm + 2

            AppButton { text: "截图"; variant: "secondary"; radius: 12
                        onClicked: ctx.window.runActionById("screenshot") }
            // 原为空白按钮（无 text），补「摄像头」
            AppButton {
                text: "摄像头"
                variant: "secondary"
                radius: 12
                visible: backend.capActions.indexOf("camera_list") >= 0
                onClicked: ctx.window.openOnly("camera")
            }
            AppButton { text: "多班墙"; variant: "secondary"; radius: 12
                        onClicked: ctx.window.openOnly("multiband") }
            AppButton { text: "远程控制"; variant: "secondary"; radius: 12
                        onClicked: ctx.window.runActionById("remote-start") }
            AppButton { text: "推文件"; variant: "secondary"; radius: 12
                        onClicked: ctx.window.runActionById("file_push") }
            // 原为空白按钮（无 text），补「返回」
            AppButton {
                text: "返回"
                variant: "ghost"
                radius: 12
                onClicked: {
                    if (page.backFn) page.backFn()
                    else ctx.window.goPage(1)
                }
            }
            // 原为空白按钮（无 text），补「停止」（弱化：停止看当前设备）
            AppButton {
                text: "停止"
                variant: "ghost"
                radius: 12
                onClicked: { backend.currentUid = ""; ctx.window.currentUid = "" }
            }
        }
    }
}
