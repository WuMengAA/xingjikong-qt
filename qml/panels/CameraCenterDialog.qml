import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// CameraCenterDialog（U3 从 Main.qml 抽出 · 监控 / 摄像头（原来只是个内联 wrapper，内含 CameraCenter 组件））
// ───────────────────────────────────────────────────────────────────────────
// 契约（见 Stelarith-shared-qml/ui/UiContract.js）：
//   · Popup 自带 open()/close() ⇒ entry 契约天然满足
//   · `property var ctx` 收宿主注入的上下文：ctx.window（原 `root.`）、ctx.open/ctx.closePanels
//   · `property var theme` 给本文件自己的绘制用（token 对象）
//
// ⚠️ 内容与抽出前逐字相同，只做了四类机械替换（语义不变）：
//      `root.X` → `ctx.window.X`
//      裸 `th.foo`（属性读取）→ `theme.foo`
//      裸 `th`（作为取值，如 `Btn { theme: th }`）→ `ctx.window.th`
//        ⚠️ 不能用本地 `theme`：那会变成 `theme: theme` 自引用绑定
//      跨面板 `controlDlg.close()` → `ctx.closePanels()`
Popup {
    id: camCenterDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: parent
    width: 680
    height: 520
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    // 但这里也用底色区分而非描边（层次靠色阶，不靠线）。
    background: Rectangle {
        color: theme ? theme.cSurface : "#1C1B1F"
        radius: theme ? theme.rXL : 28
    }
    // ⚠️⚠️ 2026-10-19 实测坑：下面三行的右值**必须显式写 camCenterDlg.ctx**，不能写裸 ctx！
    //   CameraCenter 内部若也声明 `ctx` 属性（曾因此中招：自引用 ⇒ theme/backend/ctx 全
    //   undefined，面板能开但全死），裸 ctx 会解析到子组件自己的属性。显式限定宿主最稳。
    //   （本组件现已改名叫 panelCtx，但显式写法保留，防的就是后来人再加 shadow 属性。）
    CameraCenter {
        id: camCenter
        theme: camCenterDlg.ctx.window.th
        backend: camCenterDlg.ctx.window.backendRef
        panelCtx: camCenterDlg.ctx
        anchors.fill: parent
        anchors.margins: 24
    }

    // ── 驱动转发（U3）────────────────────────────────────────────────────
    // 原来 Main.qml 直接拿 `camCenter` 这个 id 调（同一个文件里，id 可见）。
    // 抽出成独立文件后 id 不可见了，故在此暴露同一组入口 —— 调用方从
    // `camCenter.x()` 改成 `panelHost.item("camera").x()`，语义不变。
    // ⚠️ 打开时"选中当前设备 + 拉一次列表"原本写在 Main.qml 的 openOnly 分支里，
    //    现在收进 onOpened（面板自己负责自己的初始化，正是插件化的用意）。
    function refreshCams() { camCenter.refreshCams() }
    function onCams(d) { camCenter.onCams(d) }
    function onShotResult(ok, msg) { camCenter.onShotResult(ok, msg) }
    function onTestResult(ok, msg, d) { camCenter.onTestResult(ok, msg, d) }
    function onBindResult(ok, msg, d) { camCenter.onBindResult(ok, msg, d) }

    onOpened: {
        if (!ctx) return
        camCenter.selUid = ctx.window.backendRef.currentUid
        camCenter.refreshCams()
    }
}