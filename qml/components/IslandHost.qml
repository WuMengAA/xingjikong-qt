import QtQuick 2.15
import "../island"        // 共享灵动岛真源（Stelarith-shared-qml/island/，两端同一份）

// 管理端灵动岛宿主（2026-10-16 · island 系统性升级）
//
// 用户裁决：「管理端主界面直接用**被控端那一颗**」—— 不是再写一颗像它的。
// 所以这里**不画任何岛的内容**，只做 IslandRoot 自己不做的唯一一件事：**按形态驱动尺寸**。
//
// 为什么需要宿主：
//   · 被控端的尺寸由 C++ `IslandOverlay::geometryForContent/geometryForForm` 定
//     （island.cpp：收起 320×56 / 展开 640×320），QQuickWidget 用 SizeRootObjectToView
//     跟随 —— IslandRoot 自己从不设 width/height（设了会被 view 覆盖、且与 Behavior 打架）。
//   · 管理端的岛是**嵌在 QML 布局里的普通 Item**，没有 C++ 宿主 ⇒ 由本组件复刻同一套预算。
//
// ⚠️ 这套预算与 island.cpp 的 geometryForContent **必须同值**。真源在 C++（island.h::Shape）,
//    本文件是复刻 —— 分叉由 `shared-qml/scripts/test-island-metrics.mjs` 机器盯（跑它即可）。
//    展开 640 是 2026-10-13 用户「界面太小了，放大到 640×320」定的。
// ⚠️ 已知缺口（**有意保留，非遗漏**）：C++ 的非媒体分支按 QFontMetrics 算**内容定宽**
//    （`qBound(kMinContentW, iconW + textW + 48, maxW)`），本宿主恒 320 未复刻 ——
//    管理端的岛嵌在布局里，宽度随文案跳会顶动周围布局，所以固定宽更适合。
//    改它是**行为变更**，要用户点单（门禁第七条会在有人补复刻时提醒更新归档）。
// ⚠️ 「展开」不是本组件自己的状态 —— 它是桥的 `expanded`（由点击/面板驱动），
//    这样管理端与被控端的开合语义一致。
Item {
    id: host

    // 桥（全局上下文属性，与被控端同一范式；C++ 侧是 InteractBridge）
    readonly property var br: (typeof bridge !== "undefined" && bridge !== null) ? bridge : null

    readonly property bool _idle:     br ? br.idle     : true
    readonly property bool _expanded: br ? br.expanded : false

    // idle（无通知、无状态）⇒ 彻底消失，不占布局。
    // ⚠️ 这里与被控端**有意不同**（2026-10-16 纠偏，旧注释「对齐被控端」是错的）：
    //    被控端的 idle 是**常驻不隐藏**的半胶囊（C++ `geometryForIdle()`：132×44、贴顶 y=10，
    //    见 island.h「空闲态不隐藏、不消失」），因为它的岛是独立置顶窗，收回顶部仍有意义；
    //    管理端的岛是**布局里的一块**，空闲还占着 320×56 会白顶一块，所以让它退场。
    visible: !_idle

    // 收起 320×56 / 展开 640×320（高为下限）—— 与 island.cpp 同预算。
    implicitWidth:  _idle ? 0 : (_expanded ? 640 : 320)
    implicitHeight: _idle ? 0 : (_expanded ? 320 : 56)

    // ⚠️ Item 的 width 默认是 0，**不会**自动跟随 implicitWidth —— 必须显式绑，
    //   否则 Main.qml 里 `width: island.width` 拿到 0，岛直接不可见。
    width:  implicitWidth
    height: implicitHeight

    // 几何缓动对齐被控端 QPropertyAnimation(geometry, OutBack, 380ms)。
    Behavior on implicitWidth  { NumberAnimation { duration: 380; easing.type: Easing.OutBack } }
    Behavior on implicitHeight { NumberAnimation { duration: 380; easing.type: Easing.OutBack } }

    // 岛本体：被控端同一份文件，不做任何定制。
    IslandRoot {
        anchors.fill: parent
    }
}
