// 星集控 · 手机端监控页（#370 重构 2026-10-19）
// ───────────────────────────────────────────────────────────────────────────
// 修复（用户反馈「对老师不友好、功能缺失、无法退出」）：
//   ① 死摄像头 image://frames/cam（RTC 摄像头轨随 libdatachannel 移除，无数据源）
//      → 改为实时屏 image://frames/f（与控制/桌面监控同一订阅帧流）。
//   ② 进页订阅帧流、离页退订（setScreenActive）—— 解决"卡死/无法退出"：
//      退订即停流，云端不再推帧；离开还顺手收掉远控，不再无声泄漏。
//   ③ 操作栏补 截图 / 远程控制 / 推文件，停止真正清流（本地 watching 标志，
//      不依赖 setCurrentUid("") 早返回不清场的 C++ 怪癖）。
//   ④ 远控 VNC 画面（image://vnc/live）同屏显示，点「远程控制」直接出画面。
//
// 移动端本体是 Item（非窗口），令牌走 md3 属性；backend 是全局上下文属性，可直接访问。
import QtQuick
import QtQuick.Layouts
import "../components"
import "../components/md3"

Item {
    id: page
    property var md3: ({})

    // 推文件：把目标机塞进 backend.currentUid，由壳（Main/MobileShell）开 FilePushDialog
    signal filePushRequested(var uid)
    // 轻提示：透传给壳的 toast（壳有 Md3Snackbar，本页没有）
    signal toastMsg(string s)

    // 是否在看某台：本地标志，独立的"停止/退出"语义，绕开 C++ currentUid 清空怪癖。
    property bool watching: false
    // 屏确实属于当前这台吗（与桌面 frameReady 同判据）
    readonly property bool frameReady: backend.frameCount > 0 && backend.frameUid === backend.currentUid

    // 进/出监控页：订阅或退订主画面帧流（与桌面 onPageChanged 同语义）。
    // 退订即停流，云端不再推帧；离开还顺手收掉远控 ⇒ 不卡死、可退出。
    onVisibleChanged: {
        if (typeof backend === "undefined" || !backend) return
        if (page.visible && page.watching) backend.setScreenActive(true)
        else if (!page.visible) backend.setScreenActive(false)
    }

    // ── ① 设备选择横条（MD3 filter chip，与桌面同一套心智）────────────
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
                    onClicked: {
                        backend.currentUid = modelData.uid
                        // 选中的那台要真的开始拉帧（与桌面/集控同语义）
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
        radius: 12
        color: page.md3.cCont
        border.color: page.md3.cOutlineVar
        border.width: 1
        clip: true

        // 实时帧（订阅帧流 image://frames/f）
        Image {
            id: screenImg
            anchors.fill: parent
            anchors.margins: 8
            fillMode: Image.PreserveAspectFit
            cache: false
            visible: page.watching && backend.frameSource !== "vnc"
            property int tick: 0
            source: (page.watching && page.frameReady && backend.frameSource !== "vnc") ? ("image://frames/f?" + tick) : ""
            Connections { target: backend; function onFrameChanged() { screenImg.tick++ } }
        }

        // 远控画面（VNC over WebSocket · image://vnc/live）
        Image {
            id: vncImg
            anchors.fill: parent
            anchors.margins: 8
            fillMode: Image.PreserveAspectFit
            cache: false
            visible: page.watching && backend.frameSource === "vnc"
            property int tick: 0
            source: (page.watching && backend.frameSource === "vnc") ? ("image://vnc/live?" + tick) : ""
            Connections { target: backend; function onVncFrameReady(img) { vncImg.tick++ } }
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
            color: page.md3.cOnSurfaceVar
            font.pixelSize: page.md3.labelM
        }

        // 链路徽标
        Rectangle {
            visible: page.watching && backend.currentUid !== ""
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            height: 20
            radius: 4
            color: backend.frameSource === "vnc" ? page.md3.cPrimary
                 : (page.frameReady ? page.md3.cOk : page.md3.cOutlineVar)
            Text {
                anchors.centerIn: parent
                text: backend.frameSource === "vnc" ? "远控中"
                    : (backend.frameSource === "jpeg" ? "轮询" : (page.frameReady ? "实时" : "接通中"))
                color: "#FFFFFF"
                font.pixelSize: 11
            }
        }
    }

    // ── ③ 操作栏（2×2 网格，窄屏友好）────────────────────────────────
    GridLayout {
        id: actions
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        columns: 2
        rowSpacing: 10
        columnSpacing: 10

        Md3Btn {
            md3: page.md3; text: "截图"; variant: "tonal"; Layout.fillWidth: true
            onClicked: {
                if (backend.currentUid !== "") { backend.sendAction("screenshot", {}); page.toastMsg("已请求截图") }
                else page.toastMsg("先选一台设备")
            }
        }
        Md3Btn {
            md3: page.md3; text: "远程控制"; variant: "tonal"; Layout.fillWidth: true
            onClicked: {
                if (backend.currentUid !== "") { backend.sendAction("remote_control_start", {}); page.toastMsg("正在接管 " + backend.currentUid) }
                else page.toastMsg("先选一台设备")
            }
        }
        Md3Btn {
            md3: page.md3; text: "推文件"; variant: "tonal"; Layout.fillWidth: true
            onClicked: {
                if (backend.currentUid !== "") page.filePushRequested(backend.currentUid)
                else page.toastMsg("先选一台设备")
            }
        }
        // 停止/退出：真正清流（本地 watching 标志），在 VNC 模式顺手收掉远控
        Md3Btn {
            md3: page.md3; text: "⏹ 停止"; variant: "outlined"; Layout.fillWidth: true
            onClicked: {
                if (backend.frameSource === "vnc") { backend.sendAction("remote_control_stop", {}); page.toastMsg("已停止远控") }
                else { page.watching = false; backend.setScreenActive(false); page.toastMsg("已停止监控") }
            }
        }
    }
}
