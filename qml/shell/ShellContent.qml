// 星集控管理端 · 外壳内容区（2026-11-01 从 MainWindow.qml 抽出）
//
// 抽出原因：经典外壳（Sidebar/TopBar/ActionBar）与**新版外壳预览**
// （qml/shell/PreviewConsole.qml）需要**同一份业务页**，不能各拷一份
// —— 两份副本必然漂移（§9「单一真源」）。
//
// 本件只装**业务页**（六个栏目），**不画任何外壳**：
//   ① 仪表盘  ② 班级中心  ③ 设备集控  ④ 运行监控  ⑤ 系统设置  ⑥ 个人中心
// 外壳（导航/顶栏/状态条）由各自的外壳件负责。
//
// ⚠️ 搬动时保持语义**逐字不变**（2026-11-01 从 MainWindow 原样搬出）：
//   · ControlPage / MonitorPage 必须用 setSource(url, {ctx}) 注入 ctx
//     （不是 source + onLoaded）—— 它们在**实例化瞬间**就求值 ctx.window.currentUid，
//     onLoaded 时补已经晚了（历史踩坑：TypeError: Cannot read property 'window' of undefined）。
//   · 分类菜单/权限裁剪仍由 MainWindow 的 sectionIndex / canControlPage 决定。
import QtQuick
import QtQuick.Layouts
import "../pages/dashboard"     // DashPage（仪表盘）
import "../components/theme"   // Theme 令牌

StackLayout {
    id: stack

    /** 当前栏目下标（由 MainWindow 的 sectionIndex 给；顺序与 navItems 一致）。 */
    property int sectionIndex: 0
    property var ctx: null
    /** 主机壳（MainWindow）—— 业务页要读它的数据与状态。 */
    property var host: null
    property var backendRef: null
    property var devices: []
    property var stats: ({})
    property bool portrait: false
    property string filterMode: "all"
    property string openUid: ""
    property var toastFn: null

    currentIndex: stack.sectionIndex

    // ① 仪表盘（S1/S2/S3/S4/S5/S7 全在 pages/dashboard/）
    DashPage {
        host: stack.ctx ? stack.ctx.window : null
        devices: stack.devices
        stats: stack.stats
        portrait: stack.portrait
        filterMode: stack.filterMode
        openUid: stack.openUid
        toastFn: function (msg, isErr) { if (stack.toastFn) stack.toastFn(msg, isErr) }
        onFilterChanged: function (m) { stack.filterMode = m }
        onDeviceActivated: function (uid) {
            stack.openUid = uid
            // H1：同时写宿主「看哪台」真源（SideSheet 与危险动作都取它）
            if (uid !== "" && stack.backendRef) stack.backendRef.currentUid = uid
        }
    }

    // ② 班级中心（2026-10-28 模块一 · 用户裁决「取代」）
    //    纯声明式：数据、几何、配色全在 C++ ClassCenterController（context property: classCenter），
    //    点击只调 classCenter.requestOp("名字")。
    Loader {
        source: "../pages/class/ClassCenterPage.qml"
    }

    // ③ 设备集控：ControlPage 原样内嵌，零重写。它自带左栏设备列表与多选。
    Loader {
        id: controlLoader
        Component.onCompleted: {
            if (stack.ctx)
                setSource("../pages/control/ControlPage.qml", { "ctx": stack.ctx })
        }
    }

    // ④ 运行监控：MonitorPage 直接内嵌；注入了 backFn 时「返回」回仪表盘。
    Loader {
        id: monitorLoader
        Component.onCompleted: {
            if (!stack.ctx) return
            setSource("../pages/control/MonitorPage.qml", {
                "ctx": stack.ctx,
                "backFn": function () { if (stack.host) stack.host.section = "dash" }
            })
        }
    }

    // ⑤ 系统设置：只给管理员（navActivated 里的 system:settings 门禁）
    Loader {
        source: "../pages/settings/SettingsPage.qml"
        onLoaded: if (item) item.ctx = stack.ctx
    }

    // ⑥ 个人中心（2026-10-25）：头像的落点。**不受** system:settings 门禁 ——
    //    老师必须能看自己的账号、在册班级、提醒/外观偏好与退出登录。
    Loader {
        source: "../pages/account/AccountCenterPage.qml"
        onLoaded: if (item) item.ctx = stack.ctx
    }
}
