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
// ⚠️ 这套预算与 island.cpp 的 geometryForContent **必须同步**（几何真源尚未统一到共享目录；
//    当前是"两处同值"，改动需同时改）。展开 640 是 2026-10-13 用户「界面太小了，放大到 640×320」定的。
// ⚠️ 「展开」不是本组件自己的状态 —— 它是桥的 `expanded`（由点击/面板驱动），
//    这样管理端与被控端的开合语义一致。
Item {
    id: host

    // 桥（全局上下文属性，与被控端同一范式；C++ 侧是 InteractBridge）
    readonly property var br: (typeof bridge !== "undefined" && bridge !== null) ? bridge : null

    readonly property bool _idle:     br ? br.idle     : true
    readonly property bool _expanded: br ? br.expanded : false

    // idle（无通知、无状态）⇒ 彻底消失，不占布局（对齐被控端"空闲彻底消失"）。
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
