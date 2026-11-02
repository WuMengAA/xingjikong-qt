// 页级 Toast 浮层（H3）· QtShadcn 规范（2026-10-29 从 MainWindow 抽件）
//
// 职责：骨架页**本地**动作反馈（如「已对 N 台在线设备下发…」）。
// ⚠️ 灵动岛浮层在 page 6 被隐藏（Main.qml:1299），root.toast() 走
//    bridge.showNote → 灵动岛，在这个页面上根本看不见 ⇒ 必须自绘。
//
// ── 2026-10-29 迁移要点（原 MainWindow 内联 31 行）──────────────────
// · `th.*` → `Theme.*` 令牌，零硬编码
// · 竖屏抬到底部导航 + 状态条上方（否则糊在底栏上看不清）
// · 宿主调用：`toastLayer.show(msg, isErr)`；`isErr` 控制配色
// · 自绘遮罩理由同文件头：骨架页不依赖灵动岛/组件弹窗的挂载层级
import QtQuick
import "../components/theme"   // Theme 令牌

Item {
    id: root

    /** 骨架朝向（MainWindow.portrait）：竖屏抬到底栏上方。 */
    property bool portrait: false
    /** 竖屏底部导航高（MainWindow.navBarH）。 */
    property int navBarH: 80
    /** 状态条高（MainWindow.statusBarH）。 */
    property int statusBarH: 32
    /** 横屏距底（MainWindow.toastBottomMargin）。 */
    property int bottomMargin: 28

    anchors.fill: parent
    z: 1000
    visible: false

    /** 显示一条 toast。`isErr`：true = 错误配色，false = 常规。 */
    function show(msg, isErr) {
        if (!msg) return
        toastText.text = msg
        root.__err = (isErr === true)
        // 常规 = surfaceVariant 底 + 次文字（弱容器底，与卡片分区）；
        // 错误 = 危险色底 + 危险色上文字（Toast 语义：提醒即醒目）
        toastBox.color = root.__err ? Theme.color.destructive : Theme.color.surfaceVariant
        root.visible = true
        toastHide.restart()
    }

    // 错误态标记（toastText 配色依赖它，绑定在内部属性上）
    property bool __err: false

    Rectangle {
        id: toastBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.portrait
                                    ? (root.navBarH + root.statusBarH + 12)
                                    : root.bottomMargin
        radius: Theme.radius.md
        // 初始默认常规态（show() 会按 __err 覆写；初始色不能引用不存在的令牌）
        color: Theme.color.surfaceVariant

        Text {
            id: toastText
            anchors.centerIn: parent
            text: ""
            font.pixelSize: 13
            color: root.__err ? Theme.color.destructiveForeground : Theme.color.textSecondary
            padding: Theme.spacing.md + Theme.spacing.xs
        }
    }

    Timer {
        id: toastHide
        interval: 2600
        onTriggered: root.visible = false
    }
}
