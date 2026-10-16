import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"
import "../../Stelarith-shared-qml/ui/samples/UiPlugins.sample.js" as Sample

// UiPageHost 的运行时探针（U5 · 2026-10-16）
//
// 验的是 QML 侧那部分：宿主真的按 `surfaces:["page"]` 的声明解析 entry、实例化、
// 注入 ctx、以及「同一时刻只显示一页、其余页保留实例」的语义。
// 契约的纯函数部分已由 scripts/test-ui-contract.mjs（node）覆盖，这里不重复。
//
// 跑法（无需构建，直接吃源码树）：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_ui_page_probe.qml
// ⚠️ Windows 上 Qt 日志走 OutputDebugString ⇒ 加 QT_ASSUME_STDERR_HAS_CONSOLE=1 才看得见文字；
//    判定不看日志，看**退出码**：10 = 全通过 / 20 = 有失败（"没输出"≠"没跑"）。
Window {
    id: win
    width: 480
    height: 320
    visible: true
    color: "#101010"

    Md3 { id: md3; dark: true }

    UiRegistry {
        id: reg
        scope: "viewer"
        permissions: []
        capabilities: []
        manifest: Sample.manifest()
    }

    // 页宿主 + 面板宿主平级：页是底、面板是浮层（z 更高）。
    UiPanelHost {
        id: panelHost
        anchors.fill: parent
        registry: reg
        theme: md3
        scope: "viewer"
        z: 10
    }

    UiPageHost {
        id: pageHost
        anchors.fill: parent
        registry: reg
        theme: md3
        scope: "viewer"
        window: win
        panelHost: panelHost
    }

    function visOf(id) {
        var it = pageHost.item(id)
        return it ? it.visible : null
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、页清单派生 ──
        ck("页数 = 2（只收 page 面）", pageHost.count === 2)
        ck("顺序按 order：A 在 B 前",
           pageHost.pages[0].id === "sample-page-a" && pageHost.pages[1].id === "sample-page-b")
        ck("默认停在第 0 页", pageHost.currentIndex === 0 && pageHost.currentId === "sample-page-a")
        ck("titleOf / iconOf 取到声明值",
           pageHost.titleOf(0) === "示例页 A" && pageHost.iconOf(1) === "monitor")

        // ── 二、预建 + 可见性（同一时刻只有一页可见）──
        ck("默认全部预建 = 2 个实例", pageHost.instantiatedIds().length === 2)
        ck("当前页可见", visOf("sample-page-a") === true)
        ck("非当前页隐藏但**实例仍在**",
           visOf("sample-page-b") === false && pageHost.item("sample-page-b") !== null)

        // ── 三、切页 ──
        ck("goTo(1) 返回 true 且 currentId 跟着换",
           pageHost.goTo(1) === true && pageHost.currentId === "sample-page-b")
        ck("切页后可见性跟着换",
           visOf("sample-page-a") === false && visOf("sample-page-b") === true)
        ck("goTo 越界拒绝（不改状态）",
           pageHost.goTo(9) === false && pageHost.currentIndex === 1)
        ck("goTo(-1) 拒绝", pageHost.goTo(-1) === false)
        ck("goToId 命中", pageHost.goToId("sample-page-a") === true && pageHost.currentIndex === 0)
        ck("goToId 未命中返回 false", pageHost.goToId("no-such-page") === false)
        ck("indexOfId 命中/未命中", pageHost.indexOfId("sample-page-b") === 1 && pageHost.indexOfId("nope") === -1)
        ck("next 前进", pageHost.next() === true && pageHost.currentIndex === 1)
        ck("next 到末尾拒绝", pageHost.next() === false && pageHost.currentIndex === 1)
        ck("prev 后退", pageHost.prev() === true && pageHost.currentIndex === 0)
        ck("prev 到开头拒绝", pageHost.prev() === false && pageHost.currentIndex === 0)

        // ── 四、navModel（导航栏直接用）──
        var nm = pageHost.navModel()
        ck("navModel 条数 = 页数", nm.length === 2)
        ck("navModel 带 index/id/title/icon/current",
           nm[0].index === 0 && nm[0].id === "sample-page-a" && nm[0].title === "示例页 A"
           && nm[0].icon === "circle" && nm[0].current === true && nm[1].current === false)

        // ── 五、同一 entry 被两条声明引用 ⇒ 两个**独立**实例 ──
        var ia = pageHost.item("sample-page-a")
        var ib = pageHost.item("sample-page-b")
        ck("两个实例不是同一个对象", ia !== null && ib !== null && ia !== ib)

        // ── 六、ctx 注入 ──
        ck("ctx 字段齐（window/theme/registry/host/scope/open/close/closePanels）", (function () {
            var c = ia ? ia.ctx : null
            return !!c && c.window === win && c.theme === md3 && c.registry === reg
                && c.host === pageHost && c.scope === "viewer"
                && typeof c.open === "function" && typeof c.close === "function"
                && typeof c.closePanels === "function"
        })())
        ck("ctx.goTo / goToId 由宿主提供（页能切到别的页）",
           typeof ia.ctx.goTo === "function" && typeof ia.ctx.goToId === "function")
        ck("页里 goToId 真的切页",
           ia.ctx.goToId("sample-page-b") === true && pageHost.currentIndex === 1)
        pageHost.goTo(0)

        // ── 七、ctx.open 转发到面板宿主 ──
        ck("页里 open() 转发到 panelHost",
           ia.ctx.open("sample") === true && panelHost.currentId === "sample")
        ck("页里 closePanels() 转发到 panelHost",
           ia.ctx.closePanels() === true && panelHost.currentId === "")

        // ── 八、lazy 切换不影响已建实例 ──
        pageHost.lazy = true
        ck("切 lazy 后已建实例不丢（仍是 2 个）", pageHost.instantiatedIds().length === 2)
        pageHost.lazy = false

        // ── 九、summary 口径 ──
        // 可见(页/面板/动作/命令)：页 2（a/b）· 面板 1（sample）· 动作 1（lock-demo）·
        // 命令 2（sample + notify-demo；agent-only-demo 被 scope 滤掉，未上报权限故放行）
        ck("注册表 summary 含页数", reg.summary().indexOf("可见(页/面板/动作/命令)=2/1/1/") >= 0)

        console.log("UI-PAGE-PROBE bad=" + bad + "  " + reg.summary())
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
