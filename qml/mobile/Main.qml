// 星集控 · 手机端主界面入口（2026-10-11 · 独立 QML 方案落地）
//
// 文档《手机端：独立 QML 还是响应式》落地：UI 层 mobile/ 独立，
// C++ 层（viewerbackend）100% 共享；原子组件 components/ 共享。
// 结构：qml/mobile/{Main, OverviewPage, DeviceListPage, MonitorPage,
//                SettingsPage, DeviceDetailDrawer, BottomNav}.qml
//
// main.cpp：Android 加载 qrc:/mobile/Main.qml（桌面仍 qml/Stelarith/Main.qml）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "."
// #5：FilePushDialog.qml 在 qml/ 根目录（桌面 Main.qml 同目录），手机端要拿它
// 必须相对导入上一级 —— 手机端此前没有这个 import，所以整个文件传输功能够不着。
import ".."

ApplicationWindow {
    id: root
    width: Math.min(430, Math.max(320, Screen.width))
    height: Math.min(930, Math.max(640, Screen.height))
    visible: true
    title: "星集控 · 手机端"
    color: root.th.win
    // ⚠️ Fluent 风格下 ApplicationWindow 背景色由系统控制，这里显式铺一层
    background: Rectangle { color: root.th.win }

    Theme { id: theme; dark: root.darkMode }
    readonly property var th: theme
    // E4 同步：darkMode 由 backend.themeMode 派生（0 浅色 / 1 深色 / 2 跟随系统取 backend.systemDark）。
    readonly property bool darkMode: (backend.themeMode === 1) ? true
                                      : (backend.themeMode === 2 ? backend.systemDark : false)

    // 关机确认目标（轻指令共用弹窗）
    property string confirmTarget: ""
    property string confirmAction: "shutdown"

    // 监控摘要数据（SettingsPage 用；由 backend 信号重算）
    property bool recBusy: false
    property var recSummary: []   // [{uid, count, last}]
    property var recentEvents: [] // [{id, time, level, msg, uid}]

    // ── 顶部条 ──
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: root.th.panel
        border.color: root.th.stroke
        border.width: 1

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "星集控"
            color: root.th.fg
            font.pixelSize: 17
            font.weight: Font.Medium
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: backend.connected ? root.th.ok : root.th.err
            }
            Text {
                text: backend.loggedIn ? (backend.connected ? "已连接" : "重连中…") : "未登录"
                color: root.th.fg3
                font.pixelSize: 11
            }
        }
    }

    // ── 页容器（StackLayout 扁平导航）──
    StackLayout {
        id: pages
        anchors.top: parent.top
        anchors.topMargin: 52
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomNav.top
        currentIndex: bottomNav.currentIndex

        // 页 0：概览
        OverviewPage { th: root.th }

        // 页 1：设备列表（点击 → 抽屉；关机 → 确认弹窗）
        DeviceListPage {
            th: root.th
            onOpenDetail: function (dev) { deviceDrawer.dev = dev; deviceDrawer.open() }
            onShutdownRequested: function (uid) { root.confirmTarget = uid; root.confirmAction = "shutdown"; confirmDialog.open() }
        }

        // 页 2：监控（WebRTC 实时画面，自持接线）
        MonitorPage { th: root.th }

        // 页 3：我的（账户/身份/外观/告警/录制/版本）
        SettingsPage {
            th: root.th
            recSummary: root.recSummary
            recentEvents: root.recentEvents
            recBusy: root.recBusy
        }
    }

    // ── 底部导航（TabBar 扁平）──
    BottomNav {
        id: bottomNav
        th: root.th
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }

    // ── 设备详情抽屉（底部）──
    DeviceDetailDrawer {
        id: deviceDrawer
        th: root.th
        onShutdownRequested: function (uid) { root.confirmTarget = uid; root.confirmAction = "shutdown"; confirmDialog.open() }
        onToast: function (msg) { root.toast(msg) }
        // #5：点「传文件」→ 先把目标设备设进 backend（FilePushDialog 推给 currentUid），
        // 再开弹窗。currentUid 是 Q_PROPERTY(READ/WRITE)，QML 可直接赋值。
        onFilePushRequested: function (uid) { backend.currentUid = uid; fileDlg.open() }
    }

    // ── 文件传输弹窗（#5：手机端补上这条入口）──
    // 原宽 560 在 390 宽的手机上会溢出，按屏宽收一下。
    FilePushDialog {
        id: fileDlg
        theme: root.th
        width: Math.min(560, root.width * 0.94)
    }

    // ── 关机/锁屏确认弹窗 ──
    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "确认操作"
        standardButtons: Dialog.NoButton
        contentItem: Column {
            spacing: 12
            Text {
                text: "对 " + root.confirmTarget + " 执行「" + (root.confirmAction === "shutdown" ? "关机" : "锁屏") + "」？"
                color: root.th.fg; font.pixelSize: 14; wrapMode: Text.Wrap; width: 240
            }
            Row {
                spacing: 10
                Rectangle {
                    width: 110; height: 34; radius: 17; color: root.th.hover
                    border.color: root.th.stroke; border.width: 1
                    Text { anchors.centerIn: parent; text: "取消"; color: root.th.fg3; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: confirmDialog.close() }
                }
                Rectangle {
                    width: 110; height: 34; radius: 17; color: root.th.err
                    Text { anchors.centerIn: parent; text: "确认"; color: "#fff"; font.pixelSize: 12 }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            backend.currentUid = root.confirmTarget
                            backend.sendAction(root.confirmAction, {})
                            root.toast("已下发「" + root.confirmAction + "」→ " + root.confirmTarget)
                            confirmDialog.close()
                        }
                    }
                }
            }
        }
    }

    // ── 轻提示 toast ──
    function toast(s) {
        tipText.text = s
        tipTimer.restart()
    }
    Rectangle {
        id: tipBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bottomNav.top
        anchors.bottomMargin: 12
        width: tipText.implicitWidth + 24
        height: 30
        radius: 15
        color: root.th.inv
        visible: tipText.text !== ""
        z: 10
        Text {
            id: tipText
            anchors.centerIn: parent
            color: root.th.win
            font.pixelSize: 12
            text: ""
        }
    }
    Timer {
        id: tipTimer
        interval: 2000
        onTriggered: tipText.text = ""
    }

    // ── 设备表刷新（30s 自动）──
    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: backend.requestDevices()
    }

    Component.onCompleted: {
        backend.requestDevices()
        // 监控摘要：录制列表回来 → 汇总
        backend.recordingsChanged.connect(function () { root.buildRecSummary() })
        backend.fetchRecordings()
        // 最近告警（/api/events）
        backend.eventsChanged.connect(function () { root.buildRecentEvents() })
        backend.fetchEvents()
    }

    // ── 录制汇总：{uid: [{name,bytes,url,mtimeMs},...]} → [{uid,count,last}] ──
    function buildRecSummary() {
        root.recBusy = false
        var recs = backend.recordings || {}
        var out = []
        for (var k in recs) {
            var files = recs[k] || []
            if (!files.length) continue
            var lastTxt = "-"
            var newest = 0
            for (var i = 0; i < files.length; ++i)
                if (files[i].mtimeMs > newest) newest = files[i].mtimeMs
            if (newest > 0) {
                var d = new Date(newest)
                lastTxt = (d.getMonth() + 1) + "-" + d.getDate() + " " +
                          (d.getHours() < 10 ? "0" : "") + d.getHours() + ":" +
                          (d.getMinutes() < 10 ? "0" : "") + d.getMinutes()
            }
            out.push({ uid: k, count: files.length, last: lastTxt })
        }
        out.sort(function (a, b) { return (recs[b.uid][0].mtimeMs || 0) - (recs[a.uid][0].mtimeMs || 0) })
        root.recSummary = out.slice(0, 12)
    }

    // ── 最近告警：/api/events → [{id,time,level,msg,uid}] 前 20 条 ──
    function buildRecentEvents() {
        var arr = backend.events || []
        var out = []
        for (var i = 0; i < arr.length && i < 20; ++i) {
            var e = arr[i] || {}
            var t = ""
            if (e.at) {
                var d = new Date(e.at)
                t = (d.getMonth() + 1) + "-" + d.getDate() + " " +
                    (d.getHours() < 10 ? "0" : "") + d.getHours() + ":" +
                    (d.getMinutes() < 10 ? "0" : "") + d.getMinutes()
            }
            out.push({ id: e.id, time: t, level: e.level, msg: e.msg || "", uid: e.uid || "" })
        }
        root.recentEvents = out
    }
}
