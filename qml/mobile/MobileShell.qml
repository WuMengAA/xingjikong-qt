// 星集控 · 手机端布局壳（2026-10-13 · MD3 唯一模板迁移批）
//
// 由 mobile/Main.qml（独立 ApplicationWindow 壳）改造而来：
// 根类型 ApplicationWindow → Item，可嵌入桌面 Main.qml 的响应式分支
// （窄屏时 Loader 挂载，盖住桌面布局）。内部四页 / Drawer / 弹窗不变。
//
// 2026-10-13：UI 转 Material 3（用户批准 MaterialLab 为唯一模板）。
// 令牌单一真源 = ../components/theme/Md3.qml（pragma Singleton）；组件 = ../components/md3/。
// 原 BottomNav.qml 已删除（由 Md3NavBar 取代）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"      // 共享 ui 组件（UiRegistry 等）
import "../components/theme"   // Md3 令牌（pragma Singleton，2026-10-25；dark 由主外壳统一写）
import "../components/md3"  // MD3 组件库
import ".."          // FilePushDialog 在 qml/ 根目录（#5：手机端文件传输入口）

Item {
    id: root
    anchors.fill: parent

    // ⚠️ 必须铺全屏不透明背景——本组件是 Item（非窗口），
    // 透明时桌面 Main.qml 的布局会从空隙透出来 → 「UI 重叠」。z 放最底。
    Rectangle {
        anchors.fill: parent
        z: -1
        color: Md3.cSurface
    }

    // Md3 = pragma Singleton（真源 shared-qml/ui/theme/）；独立壳入口，负责写全局 dark。
    Binding { target: Md3; property: "dark"; value: root.darkMode }
    // 迁移桥：保留 th 旧签名（桌面 Main.qml 的旧组件读 th.*，Md3 提供 legacy 别名）
    readonly property var th: Md3
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

    // ── 顶部条（MD3 top app bar：64 高、surface 底、titleL 标题）──
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
        color: Md3.cSurface

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "星集控"
            font.pixelSize: Md3.titleL
            color: Md3.cOnSurface
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: backend.connected ? Md3.cOk : Md3.cError
            }
            Text {
                text: backend.loggedIn ? (backend.connected ? "已连接" : "重连中…") : "未登录"
                color: Md3.cOnSurfaceVar
                font.pixelSize: Md3.labelM
            }
        }
    }

    // ── 页容器（StackLayout 扁平导航）──
    StackLayout {
        id: pages
        anchors.top: parent.top
        anchors.topMargin: 64
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomNav.top
        currentIndex: bottomNav.currentIndex

        // 页 0：概览
        OverviewPage {}

        // 页 1：设备列表（点击 → 抽屉；关机 → 确认弹窗）
        DeviceListPage {
            onOpenDetail: function (dev) { deviceDrawer.dev = dev; deviceDrawer.open() }
            onShutdownRequested: function (uid) { root.confirmTarget = uid; root.confirmAction = "shutdown"; confirmDialog.opened = true }
        }

        // 页 2：监控（实时屏 + 远控 VNC 双源，#370 重构）
        MonitorPage {
            // 推文件：壳已持有 fileDlg，塞好目标机后开弹窗
            onFilePushRequested: function (uid) { backend.currentUid = uid; fileDlg.open() }
            // 轻提示透传（壳有 Md3Snackbar）
            onToastMsg: function (s) { root.toast(s) }
        }

        // 页 3：我的（账户/身份/外观/告警/录制/版本）
        SettingsPage {
            recSummary: root.recSummary
            recentEvents: root.recentEvents
            recBusy: root.recBusy
        }
    }

    // ── 底部导航（AppNavBar：80 高 + primary 实底 pill；图标走 lucide，不用 emoji）──
    AppNavBar {
        id: bottomNav
        objectName: "bottomNav"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        items: [
            { icon: "layout-grid", label: "概览" },
            { icon: "monitor",     label: "设备" },
            { icon: "video",       label: "监控" },
            { icon: "settings",    label: "我的" }
        ]
    }

    // ── 设备详情抽屉（底部）──
    DeviceDetailDrawer {
        id: deviceDrawer
        onShutdownRequested: function (uid) { root.confirmTarget = uid; root.confirmAction = "shutdown"; confirmDialog.opened = true }
        onToast: function (msg) { root.toast(msg) }
        // #5：点「传文件」→ 先把目标设备设进 backend（FilePushDialog 推给 currentUid），
        // 再开弹窗。currentUid 是 Q_PROPERTY(READ/WRITE)，QML 可直接赋值。
        onFilePushRequested: function (uid) { backend.currentUid = uid; fileDlg.open() }
    }

    // ── 文件传输弹窗（#5：手机端补上这条入口）──
    // 原宽 560 在 390 宽的手机上会溢出，按屏宽收一下。
    FilePushDialog {
        id: fileDlg
        theme: Md3
        width: Math.min(560, root.width * 0.94)
    }

    // ── 关机/锁屏确认弹窗（MD3 对话框：scrim + 28 圆角）──
    Md3Dialog {
        id: confirmDialog
        objectName: "confirmDialog"
        title: "确认操作"
        text: "对 " + root.confirmTarget + " 执行「" + (root.confirmAction === "shutdown" ? "关机" : "锁屏") + "」？"
        actions: [
            { text: "取消", role: "cancel" },
            { text: "确认", role: "danger" }
        ]
        onActionClicked: function (i, role) {
            if (role === "danger") {
                backend.currentUid = root.confirmTarget
                backend.sendAction(root.confirmAction, {})
                root.toast("已下发「" + root.confirmAction + "」→ " + root.confirmTarget)
            }
            confirmDialog.opened = false
        }
    }

    // ── 轻提示（AppSnackbar：反色底 + 4s 自动消失，API 与旧件一致）──
    AppSnackbar {
        id: snack
        objectName: "snack"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bottomNav.top
        anchors.bottomMargin: 12
        z: 10
    }
    function toast(s) { snack.show(s) }

    // ── 设备表刷新（30s 自动）──
    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: backend.requestDevices()
    }

    // ── QML 热更检查（2026-10-11 一源响应式：手机定期拉桌面端）──
    // 每小时查一次 /api/qml-patch；版本不同则下载解压到 qml-live/，下次启动生效。
    Timer {
        interval: 3600000
        repeat: true
        running: true
        onTriggered: backend.checkQmlPatch()
    }

    Component.onCompleted: {
        backend.requestDevices()
        backend.checkQmlPatch()   // 启动查一次（后台更新，下次启动生效）
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
