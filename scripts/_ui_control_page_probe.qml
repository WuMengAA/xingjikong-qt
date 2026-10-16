import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"     // Md3（给页面一个真令牌当 ctx.theme）

// ControlPage（U5 · A-2 抽出的第二页）的加载探针（2026-10-16）
//
// 为什么需要它：`qmllint` **不查「目录导入里的类型缺失」**（D 档实测：Main.qml 输出 2491 行
// 全是 Warning、0 Error，「0 Error」给的是假安全感）⇒ 只有**运行时真加载**才算数。
//
// 验什么：类型解析（`import "../components"` + `import "../island"` 找到
//         Btn/ToggleRow/RtcVideoSurface/SvgIcon）、语法、`ctx` 注入链路、实例化后根对象可用，
//         以及**页内两个内联 component（GroupTitle / ActBtn）随页可编译**。
// **不验**：页面的视觉与交互（那要真跑 viewer）。
//
// ⚠️ 探针下会出现一批 `ReferenceError: backend is not defined` 警告 —— **预期内**：
//    `backend` 是 C++ 上下文属性（`setContextProperty`），qml.exe 里没有。
//    真实运行（viewer 进程）里它恒可用，且**独立 QML 文件同样能访问上下文属性**
//    （这正是那 36 处引用一行都不用改的原因）。判据只看退出码。
//
// ⚠️ 与设置页探针不同：本探针**必须针对产物树**跑（`build/qml/Stelarith/pages/`），
//    因为 ControlPage 依赖 `import "../island"` —— 而 `qml/island/` **源树里没有**，
//    它是构建期 POST_BUILD 从 `Stelarith-shared-qml/island/` 拷进产物目录的
//    （岛的唯一真源在共享目录）。针对源树跑会得到：
//      `ControlPage.qml:5:1: "../island": no such directory`
//    —— 这正是本探针第一次跑就抓到的（"qmllint 0 Error 给的是假安全感"的又一个实例）。
//    跑前先同步：`cp qml/pages/*.qml build/qml/Stelarith/pages/`
//
// 跑法：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_ui_control_page_probe.qml
//   退出码 10 = 全通过 / 20 = 有失败。
Window {
    id: win
    width: 1200
    height: 760
    visible: true
    color: "#101010"

    Md3 { id: md3; dark: true }

    // 假「主窗口」：把控制页依赖的 16 种窗口成员摆上（不做布局断言，见下）。
    // ⚠️ 用**文件内 QtObject** 而不是 `Qt.createQmlObject` 造 —— 后者在 qml.exe 下
    //    对本探针那段声明串报 "failed to create object"（构造字符串的语法坑），
    //    而内联声明更干净（还能被 qmllint 检查）。
    QtObject {
        id: fakeWin
        property var th: md3
        property bool linkBusy: false
        property string currentUid: "u1"
        property bool devOpen: true
        property string deviceFilter: "all"
        property bool compact: false
        property var picked: []
        property bool frameReady: false
        property var results: []
        property var remoteVolume: 50
        property var filteredDevices: []
        property var devices: []
        // ⚠️ 函数与属性的区分**必须照真实 Main.qml 的签名**写（第一版把
        //    bulkList/rightGroups 声明成数组，实例化时页里当函数一调就
        //    "TypeError: Property 'bulkList' ... is not a function" —— 探针自身
        //    的假上下文失真，不是页的 bug；但这类失真会污染判断，必须照真签名写）。
        function bulkList() { return [] }
        function rightGroups() { return [] }
        function pickDevice(uid, mods, idx) {}
        function clearPick() {}
        function runActionById(id, opts) {}
    }

    // 兜底退出：`Component.onCompleted` 里一旦有异常就会中断执行，后面的 Qt.exit 到不了
    // ⇒ qml.exe 会一直挂在事件循环里。这个 Timer 保证进程一定会结束。
    Timer {
        interval: 3000
        running: true
        onTriggered: {
            console.log("UI-CONTROL-PAGE-PROBE 超时兜底退出（onCompleted 未走到 Qt.exit）")
            Qt.exit(30)
        }
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、编译期：语法 + import 解析（只 createComponent，不实例化）──
        var comp = Qt.createComponent("../build/qml/Stelarith/pages/ControlPage.qml", Component.PreferSynchronous)
        ck("ControlPage 编译 status === Ready（import \"../components\" + \"../island\" 均解析）",
           comp.status === Component.Ready)
        if (comp.status !== Component.Ready) {
            console.log("  errorString: " + comp.errorString())
            console.log("UI-CONTROL-PAGE-PROBE bad=" + bad + "（编译失败，后续跳过）")
            Qt.exit(20)
            return
        }

        // ── 二、实例化：真 ctx（形状照 UiPageHost._ctx()；假主窗口＝上面的 fakeWin）──
        var ctx = {
            theme: md3,
            window: fakeWin,
            registry: null,
            host: null,
            scope: "viewer",
            open:        function (id, args) { return id },
            close:       function () { return true },
            closePanels: function () { return true }
        }

        var it = comp.createObject(win, { "ctx": ctx })
        ck("实例化成功（返回非 null）", it !== null)
        if (!it) {
            console.log("UI-CONTROL-PAGE-PROBE bad=" + bad)
            Qt.exit(20)
            return
        }

        // ⚠️ 不要把 JS 对象跨 `createObject` 边界比引用：QML 会把它包装/复制一份
        //    ⇒ `it.ctx === ctx` **必然为假**。要比就比**内部字段**。
        ck("ctx 注入生效（内部字段可读）",
           !!it.ctx && it.ctx.window === fakeWin && it.ctx.scope === "viewer")
        ck("th 经 ctx.window.th 拿到同一令牌实例（QObject 引用保持）", it.th === md3)

        // ⚠️ 到此为止，**刻意不往下做布局断言**（anchors.fill / children / 尺寸）。
        //    那会触发页面绑定求值 ⇒ 页内 36 处 `backend.*` 在 qml.exe 里报
        //    `ReferenceError: backend is not defined`（C++ 上下文属性，qml.exe 没有）
        //    ⇒ 异常会中断 onCompleted。布局与观感属于「真跑 viewer」的验证范围。

        console.log("UI-CONTROL-PAGE-PROBE bad=" + bad
                    + "（本探针只验「加载 + ctx 注入链路」；布局/观感需真跑）")
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
