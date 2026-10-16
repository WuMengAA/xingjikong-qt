import QtQuick
import "../components"            // Md3 令牌 + UiRegistry / UiPageHost（共享 ui，构建期拷到 qml/components/）
import "../components/md3" as M   // MD3 组件（Md3NavBar 等）
import "AgentPages.js" as AgentPages

// 星集控被控端 · QML 控制面板（U5「两端同 shell」A-3 · 2026-10-16）
//
// 由 C++ `AgentShell`（src/agent_ui.h）加载：QQuickWidget 顶格承载本文件，
// 上下文属性 `agentUi` 提供数据（版本/连接/升级状态 + 各功能开关）。
//
// 结构：顶栏（标题 + 连接状态）→ 页宿主（UiPageHost）→ 底导航（Md3NavBar）。
// 页清单从 `AgentPages.js` 声明派生；底导航从页宿主的 navModel() 派生 ——
// 加页只改 AgentPages.js，本文件零改动（与 viewer 的 Main.qml 同一思路）。
//
// ⚠️ 命名纪律：Md3 令牌实例的 id 用 `tok`，**不用 `md3`** —— 子组件的属性也叫 md3，
//    写 `md3: md3` 会得到自引用绑定环（`theme: theme` 同款坑，U3 实证）。
Rectangle {
    id: shellRoot
    color: tok.cSurface

    Md3 { id: tok; dark: !agentUi.lightMode }

    UiRegistry {
        id: reg
        scope: "agent"
        // 被控端没有"权限授权"链路 ⇒ 传空数组（契约是容错口径：环境未上报权限一律放行）。
        permissions: []
        // A-3 第一批的页都没声明 caps。
        capabilities: []
        manifest: AgentPages.manifest()
    }

    // ── 顶栏 ─────────────────────────────────────────────────
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: tok.cSurface

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: tok.cOutlineVar
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "星集控 · 被控端"
            font.pixelSize: 16
            font.weight: Font.Medium
            color: tok.cOnSurface
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            // 打开面板那一刻的状态（agentUi.refresh() 在 openPanel 里先行调用）
            text: agentUi.connState
            font.pixelSize: 12
            color: agentUi.connState.indexOf("已连接") === 0 ? tok.cOk : tok.cWarn
        }
    }

    // ── 页宿主（整屏页：只显示当前页，其余保留实例、切回来状态还在）──
    UiPageHost {
        id: pageHost
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: nav.top
        registry: reg
        theme: tok
        scope: "agent"
        window: shellRoot
        entryBase: Qt.resolvedUrl(".")
    }

    // ── 底导航（模型从页宿主派生；⚠️ 函数调用无依赖追踪 ⇒ 先触达 count/currentIndex）──
    M.Md3NavBar {
        id: nav
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        md3: tok
        items: {
            var _dep = pageHost.count
            _dep = pageHost.currentIndex
            return pageHost.navModel().map(function (m) {
                return { icon: m.icon, label: m.title }
            })
        }
        currentIndex: pageHost.currentIndex
        onActivated: pageHost.goTo(index)
    }
}
