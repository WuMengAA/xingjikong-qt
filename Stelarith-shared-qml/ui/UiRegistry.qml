import QtQuick
import "UiContract.js" as C

// 界面插件注册表（U2）—— 契约与门控口径**全在 UiContract.js**（纯函数，node 可回归）；
// 本文件只是一层薄绑定：把声明清单 + 本端环境喂进去，派生各扩展面的模型。
//
// 用法（各端）：
//   UiRegistry {
//       scope: "viewer"
//       permissions: bridge.permissions          // 空 = 未上报 ⇒ 门控放行（老端容错）
//       capabilities: backend.capActions         // 空 = 未上报 ⇒ 门控放行
//       manifest: UiPlugins.manifest()
//   }
QtObject {
    id: reg

    // ── 环境 ──
    property string scope: "viewer"     // viewer | agent
    property var permissions: []        // 已授予权限（词表见 UiContract.PERMISSIONS）
    property var capabilities: []       // 被控端上报的指令能力（caps.actions）
    // ── 数据 ──
    property var manifest: []           // 原始声明；归一化与校验都走契约

    // 归一后的清单（补全默认值）。改 manifest 会自动重算。
    readonly property var items: C.normalizeAll(manifest)
    // 声明自身的问题（空数组 = 全合法）。**只报不拦**：一条写错不该让整个界面不显示。
    readonly property var problems: C.validate(items)
    readonly property var env: ({ scope: scope, permissions: permissions, caps: capabilities })

    function valid() { return problems.length === 0 }
    function problemsText() { return problems.join("\n") }

    // 按 id 查（不过门控 —— 调用方要能拿到被门控掉的条目去解释原因）
    function byId(id) { return C.byId(items, id) }
    // 过门控
    function visible(p) { return C.visible(p, env) }
    function forSurface(s) { return C.forSurface(items, s, env) }

    // ── 四个扩展面的模型派生 ──
    function navItems() { return forSurface("page") }        // 整屏页
    function panelItems() { return forSurface("panel") }     // 浮层/弹窗
    function actionItems() { return forSurface("action") }   // 设备动作
    function commandItems() { return forSurface("command") } // 命令面板

    // ── U4：动作面的三个投影（右栏分组 / 概览快捷行 / 命令面板）──
    /** 右栏要显示的分组名（按声明里首次出现的 order）。 */
    function actionGroups() { return C.groups(items, env) }
    /** 某个分组的动作（已过门控）。 */
    function actionsForGroup(g) { return C.forGroup(items, g, env) }
    /** 概览页快捷行（phone === false 的由调用方按 isPhone 过滤）。 */
    function quickActions() { return C.quickActions(items, env) }
    /** 这条声明要设备具备哪些指令能力（已过门控后仍要显示原因的场合用）。 */
    function capNeeds(p) { return C.capNeeds(p) }
    /** 单独查一条见过的门控：缺哪些能力（给"为什么置灰"的那句话用）。 */
    function capMissing(p) {
        var need = C.capNeeds(p)
        var miss = []
        for (var i = 0; i < need.length; i++)
            if (!C.visible({ caps: [need[i]], scope: p.scope, perm: "" }, env)) miss.push(need[i])
        return miss
    }

    // 命令面板认的形状（现有 CommandPalette 的项目结构 + 回指声明 id）
    // name 用 palName（命令面板要能自解释）回落 title。
    // ⚠️ 顺序按 palOrder（不是 order）：右栏要「通知」压顶、命令面板要「锁屏」排头，
    //    同一个动作满足不了两边 —— 所以排序键分开。
    function commandModel() {
        var src = commandItems()
        src.sort(function (a, b) { return a.palOrder - b.palOrder })
        var out = []
        for (var i = 0; i < src.length; i++) {
            var p = src[i]
            out.push({ name: p.palName || p.title, keys: p.keys, tip: p.tip, pluginId: p.id,
                       danger: p.danger, admin: p.admin })
        }
        return out
    }

    // 一行人话，供日志/自检显示
    function summary() {
        return "scope=" + scope
             + " 声明=" + items.length
             + " 可见(页/面板/动作/命令)=" + navItems().length + "/" + panelItems().length
             + "/" + actionItems().length + "/" + commandItems().length
             + " 问题=" + problems.length
    }
}
