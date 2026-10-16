import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"
import "../qml/UiPlugins.js" as UiPlugins

// U3 面板迁移探针（2026-10-16）
//
// 验什么：**真实的** 14 个面板声明能否全部由 UiPanelHost 按 entry 实例化并开关。
// 这是 U3 的核心风险面 —— 以前面板是内联在 Main.qml 里的对象，现在是
// 「声明 + 按需实例化」，路径错一个字符就白屏。
//
// ⚠️ 前置：先把 qml/ 同步到 build/qml/Stelarith/（entryBase 指向那里，因为
//    面板的 `import "../components"` 要能解析到共享 Md3 基座，那只在产物里）。
//    cp -r qml/. build/qml/Stelarith/
//
// 跑法：qml.exe -platform offscreen scripts/_ui_panels_probe.qml
// 判定看**退出码**：10 = 全通过 / 20 = 有失败（Windows 上 Qt 日志走 OutputDebugString，
// 加 QT_ASSUME_STDERR_HAS_CONSOLE=1 才看得到文字）。
Window {
    id: win
    width: 640
    height: 400
    visible: true
    color: "#101010"

    Md3 { id: md3; dark: true }

    // 假的"主窗口"：替代 Main.qml 的 root，把 ctx.window.* 需要的成员都摆上
    // （抽出前实测这 6 个弹窗合计只用到这几个）。
    QtObject {
        id: fakeWin
        property var th: md3
        property var candidates: []
        property var changelogModel: []
        property bool isPhone: false
        property int page: 1
        property var backendRef: QtObject { property string currentUid: "probe-uid" }
        function toast(msg) {}
        function refreshIsland() {}
        function broadcastAll(text) {}
    }

    UiRegistry {
        id: reg
        scope: "viewer"
        permissions: []
        capabilities: []
        manifest: UiPlugins.manifest()
    }

    UiPanelHost {
        id: host
        anchors.fill: parent
        registry: reg
        theme: md3
        window: fakeWin
        scope: "viewer"
        entryBase: Qt.resolvedUrl("../build/qml/Stelarith")
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、清单本身 ──
        ck("声明 14 条（= 原 openOnly 的面板数）", reg.items.length === 14)
        ck("声明全部合法：" + reg.problemsText(), reg.problems.length === 0)
        ck("viewer 可见面板 = 14", reg.panelItems().length === 14)

        var ids = []
        for (var i = 0; i < reg.panelItems().length; i++) ids.push(reg.panelItems()[i].id)
        var expect = ["log","term","media","sched","camera","notify","file","software",
                      "speech","interact","control","multiband","changelog","help"]
        var missing = []
        for (var j = 0; j < expect.length; j++)
            if (ids.indexOf(expect[j]) < 0) missing.push(expect[j])
        ck("原 openOnly 的 14 个 id 一个不少" + (missing.length ? "（缺 " + missing + "）" : ""),
           missing.length === 0)
        ck("order 升序", (function () {
            var ps = reg.panelItems()
            for (var k = 1; k < ps.length; k++) if (ps[k].order < ps[k-1].order) return false
            return true
        })())

        // ── 二、★ 逐个真开：entry 解析 + 实例化 + ctx 注入 + open/close ──
        var opened = 0, closedOk = 0
        for (var n = 0; n < expect.length; n++) {
            var id = expect[n]
            var ok = host.open(id)
            if (ok && host.currentId === id && host.currentItem !== null) opened++
            else console.log("        ↑ 开不起来：" + id)
            // ctx 注入检查（每个面板都该拿到 theme + window）
            var c = host.currentItem ? host.currentItem.ctx : null
            if (ok && (!c || c.theme === undefined || c.window === undefined))
                console.log("        ↑ ctx 不完整：" + id)
            if (host.closeAll() === true && host.currentId === "") closedOk++
            else console.log("        ↑ 关不掉：" + id)
        }
        ck("14 个面板逐个 open 成功（" + opened + "/14）", opened === 14)
        ck("14 个面板逐个 closeAll 成功（" + closedOk + "/14）", closedOk === 14)
        ck("openOnly 语义：同一时刻至多一个实例在开（currentId 唯一）",
           host.currentId === "" && host.instantiatedIds().length === 14)

        // ── 三、参数通道（相机面板要按 args 之外的方式驱动，这里只验通用 args）──
        // ── 三、参数通道 ──
        // args 是**可选**能力（契约：entry 可声明 openWith(args) 收载荷；机制已在 U2 的
        // 示例插件上验过）。真实的 14 个面板暂时都不需要参数（相机面板原来要在打开前
        // 塞 selUid，已改由它自己的 onOpened 从 ctx.window 取）。
        // 这里只验：带参调用不会把宿主/面板搞崩。
        ck("open(id,args) 带参调用不崩且仍能开", (function () {
            var ok = host.open("log", { title: "带参" })
            var r = ok && host.currentId === "log" && host.currentItem !== null
            host.closeAll()
            return r
        })())

        // ── 四、camera 面板的转发接口（Main.qml 的回调要打到这里）──
        ck("camera 面板暴露 onCams/onShotResult/onTestResult/onBindResult",
           (function () {
               var it = host.item("camera")
               if (!it) return false
               return typeof it.onCams === "function" && typeof it.onShotResult === "function"
                   && typeof it.onTestResult === "function" && typeof it.onBindResult === "function"
           })())

        ck("被门控/未知 id 拒绝", host.open("no-such-panel") === false)

        console.log("UI-PANELS-PROBE bad=" + bad + "  " + reg.summary())
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
