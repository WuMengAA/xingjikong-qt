import QtQuick
import QtQuick.Window

// AgentShell（U5-A3 被控端 QML 控制面板）的加载探针（2026-10-16）
//
// 比 viewer 的页面探针**更强**：被控端页引用的全部是 `agentUi.*`（C++ 上下文属性），
// 这里用 QtObject 伪造它 ⇒ 页内**不会**出现 ReferenceError，可以做「完整实例化 +
// UiPageHost 页派生」的验证，而不是只验编译。
//
// ⚠️ 必须针对**产物树**跑（build/qml/shell/AgentShell.qml）——
//    `import "../components"` 指向的是构建期拷入的共享 ui（源树 qml/ 下没有）。
//    跑前先 `bash scripts/build-agent.sh`（POST_BUILD 拷 qml/）或手动 cp。
//
// 跑法：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_agent_shell_probe.qml
//   退出码 10 = 全通过 / 20 = 有失败 / 30 = 兜底超时（onCompleted 中断）。
Window {
    id: win
    width: 880
    height: 640
    visible: true
    color: "#1C1B1F"

    // 假 agentUi：属性/方法与 src/agent_ui.h 的 AgentUi 一一对应
    //（⚠️ 照真实签名写 —— bulkList 教训：假上下文失真会污染判断）
    QtObject {
        id: fakeAgentUi
        property string appVersion: "0.6.36-test"
        property string uid: "probe-uid"
        property string cloudUrl: "wss://example.invalid/ws/agent"
        property string connState: "已连接（云端已确认）"
        property string dataDir: "C:/tmp/xingjikong"
        property string logPath: "C:/tmp/xingjikong/agent-qt.log"
        property string latestText: "已是最新（云端 v0.6.36）"
        property bool hasNewer: false
        property bool canUpgrade: false
        property string updateNotes: ""
        property string otaModeText: "可选：只有点「立即升级」才装"
        property string otaAutoText: "到点自动装"
        property string otaLastText: "（这台机器还没自动升级过）"
        property string otaNextText: "立刻（有网就试）"
        property bool otaPausedNow: false
        property bool notifyEnabled: true
        property bool voiceEnabled: true
        property bool noiseEnabled: true
        property bool islandIdle: true
        property bool lightMode: false
        property bool autostart: true
        property bool desktopShortcut: false
        function refresh() {}
        function openLogDir() {}
        function openDataDir() {}
        function upgradeNow() {}
        function toggleOtaPause() {}
    }

    // ⚠️ 假上下文属性：QML 作用域里 `agentUi` 要能被解析。qml.exe 没有
    //    setContextProperty ⇒ 用一个**根级属性**兜不住页的作用域？
    //    实际上探针的根上下文属性只能经 qml.exe 的环境注入……这里是**真限制**：
    //    页里 `agentUi.xxx` 解析不到根 Window 的 property（作用域链不含 parent）。
    //    ⇒ 本探针退而求其次：验「编译 Ready + UiRegistry 页派生 + UiPageHost 建页」，
    //      页的**实例化**里 agentUi 引用会报 ReferenceError（预期内，见下）。
    //      完整实例化验证属于「真跑 exe」（面板托盘入口）。
    property bool expectAgentUiErrors: true

    Timer {
        interval: 4000
        running: true
        onTriggered: {
            console.log("UI-AGENT-SHELL-PROBE 超时兜底退出（onCompleted 未走到 Qt.exit）")
            Qt.exit(30)
        }
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、编译期 ──
        var comp = Qt.createComponent("../build/qml/shell/AgentShell.qml", Component.PreferSynchronous)
        ck("AgentShell 编译 status === Ready（import ../components + ../components/md3 + AgentPages.js 全解析）",
           comp.status === Component.Ready)
        if (comp.status !== Component.Ready) {
            console.log("  errorString: " + comp.errorString())
            console.log("UI-AGENT-SHELL-PROBE bad=" + bad + "（编译失败，后续跳过）")
            Qt.exit(20)
            return
        }

        // ── 二、页声明（AgentPages.js）静态检查 ──
        // 直接 import js 太绕；UiRegistry 实例化由实例化路径覆盖。这里只确认
        // 声明文件被 QML 引用（编译 Ready 已隐含）。

        // ── 三、实例化：agentUi 在 qml.exe 里解析不到（真限制，见上）——
        //      页会报 ReferenceError，但**壳本身**（顶栏/宿主/导航）应当建成。
        var it = comp.createObject(win, {})
        ck("实例化成功（返回非 null；页内 agentUi ReferenceError 为预期内）", it !== null)
        if (!it) {
            console.log("UI-AGENT-SHELL-PROBE bad=" + bad)
            Qt.exit(20)
            return
        }
        ck("壳的子结构建成（顶栏 / 页宿主 / 底导航 ≥3 个子对象）", it.children.length >= 3)

        console.log("UI-AGENT-SHELL-PROBE bad=" + bad
                    + "（本探针验「壳加载 + 子结构」；⚠️ 不验尺寸 —— 探针里根对象没锚，"
                    + "真机由 QQuickWidget SizeRootObjectToView 给几何。"
                    + "agentUi 数据链路需真跑 exe）")
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
