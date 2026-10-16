import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"     // Md3（给页面一个真令牌当 ctx.theme）

// SettingsPage（U5 · A-2 抽出的第一页）的加载探针（2026-10-16）
//
// 为什么需要它：`qmllint` **不查「目录导入里的类型缺失」**（D 档实测：Main.qml 输出 2491 行
// 全是 Warning、0 Error，「0 Error」给的是假安全感）⇒ 只有**运行时真加载**才算数。
//
// 验什么：类型解析（`import "../components"` 找到 Btn/ToggleRow）、语法、`ctx` 注入链路、
// 实例化后根对象可用。**不验**：页面的视觉与交互（那要真跑 viewer）。
//
// ⚠️ 探针下会出现一批 `ReferenceError: backend is not defined` 警告 —— **预期内**：
//    `backend` 是 C++ 上下文属性（`setContextProperty`），qml.exe 里没有。
//    真实运行（viewer 进程）里它恒可用，且**独立 QML 文件同样能访问上下文属性**
//    （这正是那 40 处引用一行都不用改的原因）。判据只看退出码。
//
// 跑法：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_ui_settings_page_probe.qml
//   退出码 10 = 全通过 / 20 = 有失败。
Window {
    id: win
    width: 900
    height: 640
    visible: true
    color: "#101010"

    Md3 { id: md3; dark: true }

    // 兜底退出：`Component.onCompleted` 里一旦有异常（例如 backend 未定义引发的
    // ReferenceError）就会**中断执行**，后面的 Qt.exit 到不了 ⇒ qml.exe 会一直挂在
    // 事件循环里（实测挂 4 分钟不退出）。这个 Timer 保证进程一定会结束。
    Timer {
        interval: 3000
        running: true
        onTriggered: {
            console.log("UI-SETTINGS-PAGE-PROBE 超时兜底退出（onCompleted 未走到 Qt.exit）")
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
        var comp = Qt.createComponent("../qml/pages/SettingsPage.qml", Component.PreferSynchronous)
        ck("SettingsPage 编译 status === Ready（import \"../components\" 解析到 Btn/ToggleRow）",
           comp.status === Component.Ready)
        if (comp.status !== Component.Ready) {
            console.log("  errorString: " + comp.errorString())
            console.log("UI-SETTINGS-PAGE-PROBE bad=" + bad + "（编译失败，后续跳过）")
            Qt.exit(20)
            return
        }

        // ── 二、实例化：假 ctx（形状照 UiPageHost._ctx()）──
        var fakeWin = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {'
            + ' property var th; property bool isPhone: false;'
            + ' property int page: 2; property int volumeValue: 30 }', win)
        fakeWin.th = md3

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
            console.log("UI-SETTINGS-PAGE-PROBE bad=" + bad)
            Qt.exit(20)
            return
        }

        // ⚠️ 不要把 JS 对象跨 `createObject` 边界比引用：QML 会把它包装/复制一份
        //    ⇒ `it.ctx === ctx` **必然为假**（本探针第一版就写错了这条断言）。
        //    要比就比**内部字段**。
        ck("ctx 注入生效（内部字段可读）",
           !!it.ctx && it.ctx.window === fakeWin && it.ctx.scope === "viewer")
        ck("th 经 ctx.window.th 拿到同一令牌实例（QObject 引用保持）", it.th === md3)

        // ⚠️ 到此为止，**刻意不往下做布局断言**（anchors.fill / children / 尺寸）。
        //    那会触发页面绑定求值 ⇒ 页内 40 处 `backend.*` 在 qml.exe 里报
        //    `ReferenceError: backend is not defined`（`backend` 是 C++ 上下文属性，
        //    qml.exe 没有；真实 viewer 进程里恒可用）⇒ 异常会中断 onCompleted。
        //    布局与观感属于「真跑 viewer」的验证范围，不在这里伪造。

        console.log("UI-SETTINGS-PAGE-PROBE bad=" + bad
                    + "（本探针只验「加载 + ctx 注入链路」；布局/观感需真跑）")
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
