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

    // 命令面板认的形状（现有 CommandPalette 的项目结构 + 回指声明 id）
    function commandModel() {
        var src = commandItems()
        var out = []
        for (var i = 0; i < src.length; i++) {
            var p = src[i]
            out.push({ name: p.title, keys: p.keys, tip: p.tip, pluginId: p.id,
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
