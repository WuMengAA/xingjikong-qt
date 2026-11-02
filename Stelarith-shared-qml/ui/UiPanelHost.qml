import QtQuick

// 界面插件宿主（U2）—— 取代 viewer Main.qml 里那段 openOnly()：
//
//   function openOnly(which) {
//       softwareDlg.close(); logDlg.close(); mediaDlg.close(); ...
//       if      (which === "software") softwareDlg.open()
//       else if (which === "log")      logDlg.open()
//       ... 14 行 closeAll + 14 行 if-else
//   }
//
// 现在换成：宿主按声明清单**按需实例化** entry，并保证「同一时刻屏幕上只可能有一个」
// （原注释对 openOnly 的语义要求）。加一个面板 = 加一条声明，不动宿主一个字。
//
// 契约（ui/UiContract.js）：
//   · entry 必须实现 open() / close()；可选 openWith(args)（要参数的场景，如「监控」带 uid）
//   · entry 里声明 `property var ctx`  收上下文（theme/registry/host/scope/open/close）
//   · entry 里声明 `property var args` 收 open(id, args) 的载荷
Item {
    id: host

    property var registry                    // UiRegistry 实例
    property var theme
    property string scope: "viewer"
    // 主窗口引用，作为 ctx.window 透传给 entry。
    // ⚠️ 存在的理由：面板从 Main.qml 抽出成独立文件后，就没法再靠 id 直接够到主窗口了
    //   （抽出前它们用 `root.xxx`）。实测这些引用面很小且可枚举（th / isPhone / toast /
    //   refreshIsland / page / backendRef / broadcastAll / changelogModel），
    //   统一走 ctx.window 比逐个改写成 ctx.xxx 更省事、也不会漏。
    property var window: null
    // entry 相对此目录解析。默认＝本文件所在目录（共享 ui 目录），
    // copy_directory 保留目录树 ⇒ 源码树与构建产物里的解析结果一致。
    property url entryBase: Qt.resolvedUrl(".")

    readonly property string currentId: currentItem ? _currentId : ""
    property string _currentId: ""
    property var currentItem: null
    // id → 已建实例。建过就复用（与既有「14 个 Popup 声明一次、反复开关」同语义）。
    property var _cache: ({})

    signal opened(string id)
    signal closed(string id)

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
            host: host,
            scope: scope,
            open: function (id, args) { return host.open(id, args) },
            close: function () { return host.closeAll() },
            closePanels: function () { return host.closeAll() }
        }
    }

    function _item(id) {
        if (_cache[id]) return _cache[id]
        var p = registry ? registry.byId(id) : null
        if (!p) { console.log("[UiPanelHost] 未知 id:", id); return null }
        if (!p.entry) { console.log("[UiPanelHost] 声明无 entry：", id); return null }
        var url = _entryUrl(p)
        var comp = Qt.createComponent(url, Component.PreferSynchronous)
        if (comp.status !== Component.Ready) {
            console.log("[UiPanelHost] entry 解析失败：", url, comp.errorString())
            return null
        }
        var it = comp.createObject(host, { "ctx": _ctx() })
        if (!it) { console.log("[UiPanelHost] 实例化失败：", url); return null }
        _cache[id] = it
        return it
    }

    function _has(o, f) { return o && typeof o[f] === "function" }

    /** 打开一个界面插件。被门控掉的一律拒绝（而不是开了个空壳）。 */
    function open(id, args) {
        var p = registry ? registry.byId(id) : null
        if (p && registry && !registry.visible(p)) {
            console.log("[UiPanelHost] 被门控，拒绝打开：", id); return false
        }
        var it = _item(id)
        if (!it) return false
        if (_currentId === id && currentItem === it) {
            if (_has(it, "open")) it.open()
            return true
        }
        closeAll()                       // openOnly 语义：先全关
        currentItem = it
        _currentId = id
        if (_has(it, "openWith")) it.openWith(args)
        else if (_has(it, "open")) it.open()
        opened(id)
        return true
    }

    function toggle(id, args) { return _currentId === id ? closeAll() : open(id, args) }

    /**
     * 取某面板的实例（没有就按需建）。给"先把数据灌进面板、再打开"的既有路径用 ——
     * 例如收到 process_list 回执时先 item("software").items = ... 然后 open("software")。
     * 与 open() 共用同一份缓存 ⇒ 不会出现两个实例。
     */
    function item(id) { return _item(id) }

    /** 某面板当前是否开着（不创建实例）。 */
    function isOpen(id) { return _currentId === id }

    function closeAll() {
        var had = _currentId
        if (currentItem && _has(currentItem, "close")) currentItem.close()
        _currentId = ""
        currentItem = null
        if (had !== "") closed(had)
        return true
    }

    /** 已经建过实例的 id 列表（自检/日志用）。 */
    function instantiatedIds() {
        var out = []
        for (var k in _cache) out.push(k)
        return out
    }
}
