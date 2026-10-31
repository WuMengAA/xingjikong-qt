// 星集控 · 手机端监控页 · QtShadcn 规范（2026-10-29 迁移）
// ───────────────────────────────────────────────────────────────────────────
// 修复（用户反馈「对老师不友好、功能缺失、无法退出」）：
//   ① 死摄像头 image://frames/cam（RTC 摄像头轨随 libdatachannel 移除，无数据源）
//      → 改为实时屏 image://frames/f（与控制/桌面监控同一订阅帧流）。
//   ② 进页订阅帧流、离页退订（setScreenActive）—— 解决"卡死/无法退出"。
//   ③ 操作栏补 截图 / 远程控制 / 推文件，停止真正清流。
//   ④ 远控 VNC 画面（image://vnc/live）同屏显示。
//
// ── 2026-10-29 迁移要点 ────────────────────────────────────────────────
// · `Md3Chip` → `AppChip`（Md3Chip 选中态靠描边，规范禁边框 ⇒ 改实底主色）
// · **修掉一个既有 Bug**：原操作栏 4 个 `Md3Btn` **完全没有 text**，
//   渲染出来是 4 个空白按钮，用户不知道点哪个。本次补上 截图/远程控制/推文件/停止。
// · **删主画面区的 border**（规范铁律），改由 surface 与 background 色差分区
// · 链路徽标原来把文字色写死成十六进制白，改为令牌派生（见 badge 段）
// · `Md3.cOk` → `Theme.color.success`
import QtQuick
import QtQuick.Layouts
import "../components"           // AppButton / AppChip
import "../components/theme"    // Theme 令牌

Item {
    id: page

    // 推文件：把目标机塞进 backend.currentUid，由壳（Main/MobileShell）开 FilePushDialog
    signal filePushRequested(var uid)
    // 轻提示：透传给壳的 toast
    signal toastMsg(string s)

    // 是否在看某台：本地标志，独立的"停止/退出"语义，绕开 C++ currentUid 清空怪癖。
    property bool watching: false
    // 屏确实属于当前这台吗（与桌面 frameReady 同判据）
    readonly property bool frameReady: backend.frameCount > 0 && backend.frameUid === backend.currentUid

    // 进/出监控页：订阅或退订主画面帧流（与桌面 onPageChanged 同语义）。
    onVisibleChanged: {
        if (typeof backend === "undefined" || !backend) return
        if (page.visible && page.watching) backend.setScreenActive(true)
        else if (!page.visible) backend.setScreenActive(false)
    }

    // ── ① 设备选择横条 ────────────────────────────────────────────────
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        color: Theme.color.background      // 与主画面区（surface）拉开色差
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing.md - 4
            spacing: Theme.spacing.sm
            Text {
                text: "看哪台："
                color: Theme.color.textSecondary
                font.pixelSize: 14
            }
            ListView {
                id: watchList
                orientation: ListView.Horizontal
                clip: true
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                model: backend.devices
                spacing: Theme.spacing.sm
                delegate: AppChip {
                    text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                    selected: backend.currentUid === modelData.uid
                    onClicked: {
                        backend.currentUid = modelData.uid
                        backend.sendAction("screenshot", {})
                        page.watching = true
                        backend.setScreenActive(true)
                        page.toastMsg("正在接通 " + (modelData.name || modelData.uid))
                    }
                }
            }
        }
    }

    // ── ② 主画面（实时帧 / 远控 VNC 双源）────────────────────────────
    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 66
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: actions.top
        anchors.bottomMargin: 10
        radius: Theme.radius.md
        color: Theme.color.surface
        // 无边框（规范）：靠 surface 与 background 的色差分区
        clip: true

        Image {
            id: screenImg
            anchors.fill: parent
            anchors.margins: Theme.spacing.sm
            fillMode: Image.PreserveAspectFit
            cache: false
            visible: page.watching && backend.frameSource !== "vnc"
            property int tick: 0
            source: (page.watching && page.frameReady && backend.frameSource !== "vnc") ? ("image://frames/f?" + tick) : ""
            Connections { target: backend; function onFrameChanged() { screenImg.tick++ } }
        }

        Image {
            id: vncImg
            anchors.fill: parent
            anchors.margins: Theme.spacing.sm
            fillMode: Image.PreserveAspectFit
            cache: false
            visible: page.watching && backend.frameSource === "vnc"
            property int tick: 0
            source: (page.watching && backend.frameSource === "vnc") ? ("image://vnc/live?" + tick) : ""
            Connections { target: backend; function onVncFrameReady(img) { vncImg.tick++ } }
        }

        // A档（2026-10-31）：H264 远控流小窗（右下角）
        H264StreamView {
            anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
            width: Math.min(parent.width * 0.4, 240); height: width * 9 / 16
        }

        // 空态引导
        Text {
            anchors.centerIn: parent
            text: {
                if (backend.frameSource === "vnc") return ""
                if (!backend.loggedIn) return "还没登录"
                if (!page.watching || backend.currentUid === "") return "先在顶部选一台设备看监控"
                if (!page.frameReady) return "正在接通…"
                return ""
            }
            visible: text !== ""
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }

        // 链路徽标：三态各自用令牌，不再写死白色字
        Rectangle {
            id: badge
            visible: page.watching && backend.currentUid !== ""
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            height: 20
            width: badgeTxt.implicitWidth + Theme.spacing.sm * 2
            radius: Theme.radius.sm - 4
            readonly property bool isVnc: backend.frameSource === "vnc"
            readonly property bool isReady: page.frameReady
            // VNC = 主色实底；已就绪 = 成功色淡底；其余 = 弱底
            color: isVnc ? Theme.color.primary
                 : (isReady ? Qt.rgba(Theme.color.success.r, Theme.color.success.g,
                                      Theme.color.success.b, 0.15)
                            : Theme.color.surfaceVariant)
            Text {
                id: badgeTxt
                anchors.centerIn: parent
                text: backend.frameSource === "vnc" ? "远控中"
                    : (backend.frameSource === "jpeg" ? "轮询" : (page.frameReady ? "实时" : "接通中"))
                color: badge.isVnc ? Theme.color.primaryForeground
                     : (badge.isReady ? Theme.color.success : Theme.color.textSecondary)
                font.pixelSize: 11
            }
        }
    }

    // ── ③ 操作栏（2×2 网格，窄屏友好；已拆出为 MonitorActionsBar）────────
    MonitorActionsBar {
        id: actions
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        onFilePushRequested: (uid) => page.filePushRequested(uid)
        onToastMsg: (s) => page.toastMsg(s)
        onStopRequested: {
            if (backend.frameSource === "vnc") { backend.sendAction("remote_control_stop", {}); page.toastMsg("已停止远控") }
            else { page.watching = false; backend.setScreenActive(false); page.toastMsg("已停止监控") }
        }
    }
}
