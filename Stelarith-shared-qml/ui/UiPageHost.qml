import QtQuick

// 整屏页宿主（U5 · 「两端同 shell」的**页**扩展面）
//
// 与 `UiPanelHost` 同构，区别只在于装的东西：
//   · `UiPanelHost` 装**浮层/弹窗** —— 同一时刻只可能有一个，按需创建、开关即显隐
//   · `UiPageHost`  装**整屏页**   —— 同一时刻只显示一个，**其余页保留实例**（切回来状态还在）
//
// 为什么现在才做它：契约里的「页」面（`UiContract.js`：`{ name:"page", type:"int", dflt:-1 }`）
// **U2 就定义了，但一直零使用** —— viewer 的三页还是 `Main.qml` 里的内联 `Item`，
// 靠 `StackLayout { currentIndex: root.page }` 切。要把 shell 抽成可复用件（让被控端也能装），
// 页就必须和面板一样**声明化**。本文件把那一步落地。
//
// 契约（ui/UiContract.js）：
//   · 声明 `surfaces: ["page"]` + `entry`（缺 entry 会被 `validate` 报错）
//   · entry 根对象自己 `anchors.fill: parent`，并声明 `property var ctx` 收上下文
//   · ctx 字段与面板一致（`CTX_FIELDS`）：window / theme / registry / host / scope /
//     open / close / closePanels —— 其中 `host` 是**本页宿主**；`open/close/closePanels`
//     转发给 `panelHost`（页里要开面板时才有意义）
//   · ⚠️ entry 别在 `Component.onCompleted` 里读 `ctx` —— 宿主是 `createObject` 时注入的，
//     那一刻已经就绪；但若改用 Loader 装载则 `onLoaded` 晚于 `onCompleted`。照本宿主的
//     写法（createObject 带属性）是安全的，直接用属性绑定即可。
//
// 用法（各端）：
//   UiPageHost {
//       registry: reg                 // UiRegistry 实例
//       theme: th                     // Md3 令牌
//       scope: "viewer"               // viewer | agent
//       window: root                  // 主窗口（ctx.window，页面够不到 id 时用）
//       panelHost: panelHostRef       // 选填：页里的 open() 往这儿转发
//   }
Item {
    id: pageHost

    // ── 输入 ──────────────────────────────────────────────────
    property var registry
    property var theme
    property string scope: "viewer"
    property var window: null
    property var panelHost: null
    // entry 相对此目录解析（默认＝共享 ui 目录，与 UiPanelHost 同款）
    property url entryBase: Qt.resolvedUrl(".")

    /** 只建当前页（省启动开销）。代价：首次切页有延迟、页的 `Component.onCompleted`
     *  时点被推迟 —— 页里若在那儿做订阅/初始化要留意。默认 false＝全部预建，
     *  与现状 `StackLayout` 三页全建**同语义**（零行为差异）。 */
    property bool lazy: false

    property int currentIndex: 0

    // ── 派生（按 order 升序；顺序即导航顺序）────────────────────
    // ⚠️ 绑定里显式触达 `registry.items` / `registry.env`：`navItems()` 是函数调用，
    //    QML 的依赖追踪看不进函数内部，不这样写的话声明清单变了本属性不会重算。
    readonly property var pages: {
        if (!registry) return []
        var _dep = registry.items !== undefined ? registry.items.length : 0
        _dep = registry.env !== undefined ? registry.env.scope : ""
        return registry.navItems()
    }
    readonly property int count: pages.length
    readonly property var currentPage:
        (currentIndex >= 0 && currentIndex < count) ? pages[currentIndex] : null
    readonly property string currentId: currentPage ? currentPage.id : ""

    signal pageChanged(string id, int index)

    // ── 页容器 ────────────────────────────────────────────────
    // 每页一个实例，全部铺满；只显示 currentIndex 那个。
    // ⚠️ 面板宿主应与本宿主**平级且 z 更高** —— 页是底、弹窗是浮层。
    Item {
        id: pageArea
        anchors.fill: parent
    }

    property var _cache: ({})
    property var _ids: []          // 已建实例的 id（按 pages 顺序）

    function _entryUrl(p) {
        var s = entryBase.toString()
        if (s.length > 0 && s.charAt(s.length - 1) !== "/") s += "/"
        return s + p.entry
    }

    function _ctx() {
        return {
            theme: theme,
            window: window,
            registry: registry,
            host: pageHost,
            scope: scope,
            open:        function (id, args) { return panelHost ? panelHost.open(id, args) : false },
            close:       function () { return panelHost ? panelHost.closeAll() : false },
            closePanels: function () { return panelHost ? panelHost.closeAll() : false },
            // 页之间的跳转：页里也能自己切页（例如"去设置"按钮）
            goTo:     function (i) { return pageHost.goTo(i) },
            goToId:   function (id) { return pageHost.goToId(id) }
        }
    }

    function _build(i) {
        var p = pages[i]
        if (!p) return null
        if (!p.entry) { console.log("[UiPageHost] 声明无 entry：", p.id); return null }
        if (_cache[p.id]) return _cache[p.id]
        var url = _entryUrl(p)
        var comp = Qt.createComponent(url, Component.PreferSynchronous)
        if (comp.status !== Component.Ready) {
            console.log("[UiPageHost] entry 解析失败：", url, comp.errorString())
            return null
        }
        // 用 createObject 注入 ctx（而不是 Loader + onLoaded），
        // 免得 entry 的 Component.onCompleted 早于 onLoaded 拿不到 ctx。
        var it = comp.createObject(pageArea, { "ctx": _ctx() })
        if (!it) { console.log("[UiPageHost] 实例化失败：", url); return null }
        it.visible = false
        _cache[p.id] = it
        _ids.push(p.id)
        return it
    }

    function _buildAll() {
        if (!registry) return
        for (var i = 0; i < count; i++) _build(i)
        _sync()
    }

    /** 只让 currentIndex 那页可见（其余保留实例、只是隐藏）。 */
    function _sync() {
        for (var i = 0; i < count; i++) {
            var it = _cache[pages[i].id]
            if (it && it.hasOwnProperty("visible")) it.visible = (i === currentIndex)
        }
    }

    onPagesChanged: _buildAll()
    Component.onCompleted: _buildAll()

    onCurrentIndexChanged: {
        if (lazy) _build(currentIndex)
        _sync()
        pageChanged(currentId, currentIndex)
    }

    // ── 导航 API（给导航栏/快捷键用）───────────────────────────
    function goTo(i) {
        if (i >= 0 && i < count) { currentIndex = i; return true }
        return false
    }
    function goToId(id) {
        var i = indexOfId(id)
        if (i >= 0) { currentIndex = i; return true }
        return false
    }
    function indexOfId(id) {
        for (var i = 0; i < count; i++) if (pages[i].id === id) return i
        return -1
    }
    function titleOf(i) { return (i >= 0 && i < count) ? (pages[i].title || "") : "" }
    function iconOf(i)  { return (i >= 0 && i < count) ? (pages[i].icon || "") : "" }
    function next() { return goTo(currentIndex + 1) }
    function prev() { return goTo(currentIndex - 1) }

    /** 导航栏模型：把"有哪些页、现在在第几页"一次性给出，避免消费方自己算。 */
    function navModel() {
        var out = []
        for (var i = 0; i < count; i++) {
            out.push({
                index: i,
                id: pages[i].id,
                title: pages[i].title,
                icon: pages[i].icon,
                current: i === currentIndex
            })
        }
        return out
    }

    /** 已建实例的 id（自检/日志用）。 */
    function instantiatedIds() { return _ids.slice() }

    /** 取某页实例（没有就按需建）。给"先把数据灌进页、再切过去"的路径用。 */
    function item(id) { return _cache[id] ? _cache[id] : _build(indexOfId(id)) }
}