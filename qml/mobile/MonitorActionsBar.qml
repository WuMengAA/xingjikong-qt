// 星集控 · 手机端监控页 · 操作栏（2×2 网格，窄屏友好）
// 2026-10-30 从 MonitorPage.qml 拆出。行为零差异：仅把 page.* 信号改为本组件信号，
// 由宿主（MonitorPage）接线；backend 为全局，直接引用。
import QtQuick
import QtQuick.Layouts
import "../components"           // AppButton / AppChip
import "../components/theme"    // Theme 令牌

GridLayout {
    id: bar
    columns: 2
    rowSpacing: 10
    columnSpacing: 10

    // 宿主接线口：推文件 / 轻提示 / 停止（停止的 VNC 分支由宿主决定）
    signal filePushRequested(var uid)
    signal toastMsg(string s)
    signal stopRequested()

    AppButton {
        Layout.fillWidth: true
        text: "截图"
        variant: "secondary"
        onClicked: {
            if (backend.currentUid !== "") { backend.sendAction("screenshot", {}); bar.toastMsg("已请求截图") }
            else bar.toastMsg("先选一台设备")
        }
    }
    AppButton {
        Layout.fillWidth: true
        text: "远程控制"
        variant: "primary"
        onClicked: {
            if (backend.currentUid !== "") { backend.sendAction("remote_control_start", {}); bar.toastMsg("正在接管 " + backend.currentUid) }
            else bar.toastMsg("先选一台设备")
        }
    }
    AppButton {
        Layout.fillWidth: true
        text: "推文件"
        variant: "secondary"
        onClicked: {
            if (backend.currentUid !== "") bar.filePushRequested(backend.currentUid)
            else bar.toastMsg("先选一台设备")
        }
    }
    // 停止/退出：真正清流（本地 watching 标志），在 VNC 模式顺手收掉远控。
    // VNC/清流分支留在宿主（onStopRequested），本组件只上报意图。
    AppButton {
        Layout.fillWidth: true
        text: "停止"
        variant: "destructive"
        onClicked: bar.stopRequested()
    }
}
