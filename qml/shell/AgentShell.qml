import QtQuick
import "../components"            // UiRegistry / UiPageHost 等共享 ui（构建期拷到 qml/components/）
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一真源）
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
    color: Theme.color.background

    // ── 2026-10-30：移除 QtShadcn 依赖（原 2026-10-29 P0 引入的三处，均为坏件）──────
    // ① `import QtShadcn`：**运行时解析不了**。该模块的 qmldir 写
    //    `optional plugin QtShadcnplugin`，但插件目标从未编译出 .dll
    //    （build/_qtshadcn/src/QtShadcn/ 只有 qmldir + qml/ + qmltypes，无插件），
    //    产品 exe 也不依赖 QtShadcn.dll（dumpbin /dependents 实测为空）⇒ 整壳加载失败：
    //    `module "QtShadcn" plugin "QtShadcnplugin" not found`。
    // ② `QtShadcnTheme { id: shadcnTheme }`：id 全文件零引用。App* 组件读的是
    //    本工程自带的 `Theme` 单例（见 ui/theme/Theme.qml 文件头「为什么不 import QtShadcn」），
    //    并不从 QtShadcnTheme 取 token ⇒ 这条实例是纯负债。
    // ③ 两条 `Binding { target: ThemeManager ... }`：**死绑定**。`Theme.qml` 自带调色板、
    //    完全不读 ThemeManager（同见其文件头），所以把 primary 设成 #6750A4 对渲染毫无影响。
    // ⇒ 三处一起删，视觉零变化，但壳从「加载不出来」恢复为可加载。
    //    （若将来真要接入 QtShadcn 的 C++ 单例，前提是先把 QtShadcnplugin 编出 DLL。）

    property bool sheetOpen: false   // 广播站排期中心是否展开（2026-10-22）

    // 2026-10-30「全部换新」收口：`tok`（Md3 令牌别名）与 `Binding { target: Md3 … }` 一并删除。
    // 被控端 QML 至此**不再引用 Md3 令牌，也不再引用 md3 组件库**（末尾 3 处 Md3Card/Md3Dialog 已换）。

    // Theme = 新一代令牌单例（pragma Singleton，共享库 ui/theme/Theme.qml）。
    // ⚠️ 2026-10-30 实测坑：`Theme.dark` 与 `Md3.dark` 是**两个独立布尔**，
    //    Theme 默认 false（浅色）。迁到 Theme 的组件若不跟着绑，会在深色外壳里
    //    渲染成浅色块 —— 静默错、不报错。故两个单例必须同时写，直到 Md3 退役。
    Binding { target: Theme; property: "dark"; value: !agentUi.lightMode }

    // 被控端「不要品牌紫」（2026-10-30 用户点单）：开关轨道 / 选中段 / 当前导航 / primary 按钮
    // 一律走中性反白，品牌紫只留给管理端。开关本身在共享库 Theme 里（`neutralAccent`），
    // 这里只负责把它打开 —— 关掉管理端不受影响。
    Binding { target: Theme; property: "neutralAccent"; value: true }

    UiRegistry {
        id: reg
        scope: "agent"
        // 被控端没有"权限授权"链路 ⇒ 传空数组（契约是容错口径：环境未上报权限一律放行）。
        permissions: []
        // A-3 第一批的页都没声明 caps。
        capabilities: []
        // 2026-10-25：用途是第一维过滤 —— 广播机看到「广播台」首屏、看不到班级采集页；
        // 教室机反之。manifestFor 内部按 order 重排，底导航顺序随之重算。
        manifest: AgentPages.manifestFor(agentUi.deviceRole)
    }

    // ── 顶栏 ─────────────────────────────────────────────────
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: Theme.color.surface

        // 2026-10-30 去边框：顶栏下方 1px 发丝线删除（靠 surface / background 的色差分界）
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            // 标题即身份：现场一眼能看出这台是教室机还是广播机（用途来自 OOBE 第一步）
            text: agentUi.deviceRole === "broadcast" ? "星集控 · 广播端" : "星集控 · 被控端"
            font.pixelSize: 16
            font.weight: Font.Medium
            color: Theme.color.textPrimary
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            // 广播站排期中心入口（蓝色按钮，预览其内部蓝白主题）
            Item {
                height: 28; width: 72
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius.full
                    color: Theme.accentStrong
                }
                // hover / press 反馈：一层覆盖色改透明度（2026-10-30 去品牌蓝，不再写死三个 Hex）
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius.full
                    color: Theme.accentStrongForeground
                    opacity: bcMa.pressed ? 0.16 : (bcMa.containsMouse ? 0.08 : 0)
                    Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                }
                Text {
                    anchors.centerIn: parent; text: "广播站"
                    font.pixelSize: 12; font.weight: Font.Medium
                    color: Theme.accentStrongForeground
                }
                MouseArea {
                    id: bcMa; anchors.fill: parent; hoverEnabled: true
                    onClicked: shellRoot.sheetOpen = true
                }
            }
            Text {
                // 打开面板那一刻的状态（agentUi.refresh() 在 openPanel 里先行调用）
                text: agentUi.connState
                font.pixelSize: 12
                color: agentUi.connState.indexOf("已连接") === 0 ? Theme.color.success : Theme.color.warning
            }
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
        theme: Theme
        scope: "agent"
        window: shellRoot
        entryBase: Qt.resolvedUrl(".")
    }

    // ── 底导航（模型从页宿主派生；⚠️ 函数调用无依赖追踪 ⇒ 先触达 count/currentIndex）──
    // 2026-10-28 模块三：换掉共享 Md3NavBar（图标是 `Text{text: icon}` ⇒ 渲染成
    // 方块/英文串）。本件同目录兄弟组件（SvgIcon 版），接口不变。
    AgentBottomNav {
        id: nav
        height: implicitHeight       // ⚠️ Item 的 height 不自动跟随 implicitHeight（80 会落成 0）
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
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

    // ── 广播站排期中心（右侧滑出 SideSheet，2026-10-22）──────────────────
    // 同目录兄弟组件，QML 直接按类型名解析，无需 import。
    BroadcastScheduleSheet {
        id: sheet
        anchors.fill: parent
        opened: shellRoot.sheetOpen
        onClosed: shellRoot.sheetOpen = false
    }
}
