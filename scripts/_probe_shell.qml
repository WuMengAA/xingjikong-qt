// 星集控管理端 · 互动审批面板「离屏探针场景」（2026-10-15 · 组件6 取证用）
//
// 谁在用：scripts/_interact_probe.cpp（QQuickView 的 root）。
//
// 场景根故意是 **Rectangle（Item）** 而不是 Window：QQuickView 只接受 Item 作 root。
// 真正被取证的是它里面那颗 InteractDialog + 内嵌的共享灵动岛（IslandRoot）。
//
// 参数由 C++ 侧 setProperty 注入（probe* ），Timer 里统一应用 ——
// 因为 setSource() 返回时绑定/锚链还在初始化，立刻写属性会被后续初始化覆盖。
import QtQuick
import QtQuick.Controls
import "."
import "components"

Rectangle {
    id: shell
    width: 1000
    height: 680
    color: "black"

    // ── 探针参数（C++ 注入）─────────────────────────────────────
    property bool   probeLight: false
    property string probeTab: "song"      // song | blessing | feedback
    property int    probeSel: -1          // 预选中行（-1 = 不选）
    property string probeKind: ""         // 触发岛预览：song | blessing | feedback | ""
    property string probeTitle: ""
    property string probeSub: ""

    // 主题令牌真源 = qml/components/Md3.qml（与 Main.qml 同款取法）
    Md3 { id: thDark;  dark: true }
    Md3 { id: thLight; dark: false }

    InteractDialog {
        id: dlg
        theme: shell.probeLight ? thLight : thDark
    }

    function applyProbe() {
        dlg.open()                                  // 打开面板（onOpened 里会 reload；假桥恒回 ok）
        if (dlg.tab !== shell.probeTab) dlg.tab = shell.probeTab
        if (shell.probeSel >= 0) {
            dlg.selIndex = shell.probeSel
            if (shell.probeKind !== "" && typeof bridge !== "undefined" && bridge
                    && bridge.preview)
                bridge.preview(shell.probeKind, shell.probeTitle, shell.probeSub)
        }
    }

    // 一拍之后再应用：等 setSource 的初始化跑完、锚链重建一次
    Timer {
        interval: 400; running: true; repeat: false
        onTriggered: shell.applyProbe()
    }
}
