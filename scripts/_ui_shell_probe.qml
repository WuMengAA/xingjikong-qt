import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"
import "../../Stelarith-shared-qml/ui/samples/UiPlugins.sample.js" as Sample

// UiRegistry / UiPanelHost 的运行时探针（U2 · 2026-10-16）
//
// 契约与门控的**纯函数**部分已由 scripts/test-ui-contract.mjs（node）覆盖；
// 这里验的是 QML 侧那部分：注册表的模型派生、宿主真的按声明解析 entry、
// 实例化、注入 ctx、以及 openOnly（同一时刻只一个）语义。
//
// 跑法（无需构建，直接吃源码树）：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_ui_shell_probe.qml
// ⚠️ Windows 上 Qt 日志走 OutputDebugString ⇒ 加 QT_ASSUME_STDERR_HAS_CONSOLE=1 才看得见文字；
//    但判定不看日志，看**退出码**：10 = 全通过 / 20 = 有失败（"没输出"≠"没跑"）。
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

    UiPanelHost {
        id: host
        anchors.fill: parent
        registry: reg
        theme: md3
        scope: "viewer"
    }

    function anyById(list, id) {
        for (var i = 0; i < list.length; i++) if (list[i].id === id) return true
        return false
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、注册表：模型派生 ──
        // 2026-10-16 U5：示例清单从 4 条扩到 6 条（加了两条 page 面的 sample-page-a/b）。
        ck("清单 6 条", reg.items.length === 6)
        ck("声明全部合法（problems 空）", reg.problems.length === 0)
        ck("归一：notice 缺省 scope 变列表（agent-only-demo scope=[agent]）",
           reg.byId("agent-only-demo").scope.length === 1 && reg.byId("agent-only-demo").scope[0] === "agent")
        ck("viewer 可见面板 = 1（sample）",
           reg.panelItems().length === 1 && reg.panelItems()[0].id === "sample")
        ck("viewer 可见页 = 2（sample-page-a/b）",
           reg.navItems().length === 2 && reg.navItems()[0].id === "sample-page-a")
        ck("order 升序", reg.commandItems()[0].order <= reg.commandItems()[1].order)

        // ── 二、门控三态 ──
        ck("scope：agent-only 被滤掉", !anyById(reg.commandItems(), "agent-only-demo"))
        ck("perm：未上报权限 ⇒ notify-demo 放行", anyById(reg.commandItems(), "notify-demo"))
        ck("caps：未上报能力 ⇒ lock-demo 动作可见", reg.actionItems().length === 1)
        reg.permissions = ["settings"]
        ck("perm：上报权限但缺 notify ⇒ 该条隐藏", !anyById(reg.commandItems(), "notify-demo"))
        reg.permissions = []
        reg.capabilities = ["reboot"]
        ck("caps：上报能力但不含 lock ⇒ 动作隐藏", reg.actionItems().length === 0)
        reg.capabilities = []

        // ── 三、命令面板模型形状 ──
        var cm = reg.commandModel()
        ck("commandModel 带 pluginId 回指", cm.length > 0 && cm[0].pluginId !== undefined)

        // ── 四、★ 宿主：真开一个 ──
        ck("open(sample) 返回 true", host.open("sample") === true)
        ck("currentId === sample", host.currentId === "sample")
        ck("入口真的实例化并 open（visible=true）",
           host.currentItem !== null && host.currentItem.visible === true)
        ck("ctx 已注入（theme / registry / open / close 齐）", (function () {
            var c = host.currentItem ? host.currentItem.ctx : null
            return !!c && c.theme !== undefined && c.registry !== undefined
                && typeof c.open === "function" && typeof c.close === "function"
        })())
        ck("被门控的 id 拒绝打开（不是开了个空壳）", host.open("agent-only-demo") === false)
        ck("未知 id 拒绝打开", host.open("no-such-id") === false)
        ck("openOnly 语义：开了 sample 后 currentId 仍是 sample", host.currentId === "sample")

        ck("closeAll 返回 true 且清空", host.closeAll() === true && host.currentId === "")
        ck("closeAll 后 currentItem 置空", host.currentItem === null)

        // ── 五、实例复用（不该每次开都重建）──
        host.open("sample")
        var n1 = host.instantiatedIds().length
        host.closeAll()
        host.open("sample")
        ck("实例复用：两次开关仍是 " + n1 + " 个实例", host.instantiatedIds().length === n1)
        ck("复用后仍可见", host.currentItem !== null && host.currentItem.visible === true)
        host.closeAll()

        // ── 六、开放参数通道（openWith）──
        host.open("sample", { title: "带参数打开" })
        ck("open(id,args) 载荷送达 entry.args",
           !!host.currentItem && !!host.currentItem.args && host.currentItem.args.title === "带参数打开")
        host.closeAll()

        console.log("UI-SHELL-PROBE bad=" + bad + "  " + reg.summary())
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
