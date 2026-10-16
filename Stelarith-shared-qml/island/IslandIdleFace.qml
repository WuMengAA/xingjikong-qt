import QtQuick 2.15
import "."

// 灵动岛 · 空闲面部（对齐 island.cpp 644-689）
//
// 从 `IslandRoot.qml` 抽出的**第一块**（2026-10-16 · U 系列 D 档「IslandRoot 2207 行拆分」）。
// 后续各块照此办理，规矩两条：
//
// 1. **只搬，不改**。几何 / 锚 / 字号 / 动效 / z 一律逐字保留 —— 拆分是纯结构搬迁，
//    行为必须零变化。证明手段：拆前冻结探针基线（`_island_qml_probe.py` 的
//    `=== tree ===` 全子树几何 + `root.*` 属性），拆后用
//    `shared-qml/scripts/diff-island-tree.mjs` 比对，**差异必须为空**。
//
// 2. **隐式作用域搬不走**，这是拆分唯一的真代价。原来靠「子对象 → 组件根」隐式解析的
//    裸标识符（`th`、`_idle`）和 `root.xxx` 前缀，一旦离开 IslandRoot 就**无声断链**
//    （不报错，只是取值恒 undefined）。所以每个抽出的组件必须把自己的依赖
//    **逐个声明成 property**，实例化处显式传入。
//    ⚠️ 2026-10-16 U3 的教训：当时只 grep 带 `root.` 前缀的引用，漏掉 87 处裸 `th`，
//       搬完全套面板的配色变默认值。这次逐块核对了裸标识符。
Item {
    id: face

    // ── 接口（由 IslandRoot 传入）─────────────────────────────
    // 名字刻意用**不带下划线**的公开名：`_xxx` 是 IslandRoot 的内部约定，
    // 子组件用公开名，避免"子组件里写 `_idle` 以为是自己的还是父的"这种二义。
    property var th           // 主题令牌（IslandTheme 实例，必传）
    property bool idle: true  // 空闲态（false ⇒ 整面淡出并让位给活动态）

    visible: face.idle
    opacity: face.idle ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    IslandClock {
        id: idleClock
        theme: face.th
        timePx: face.th.fsBody
        // 岛宽 132（C++ kIdleW），鲸鱼占右 28px + 6 margin = 34px，时钟右留 38px
        anchors.right: parent.right
        anchors.rightMargin: 38
        anchors.verticalCenter: parent.verticalCenter
    }

    // 左半区：横杠 + 下三角
    Item {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width / 2
        height: parent.height

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -3
            width: 24; height: 4; radius: 2
            color: face.th.idleFg
        }

        Canvas {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 5
            width: 10; height: 6
            onPaint: {
                var c = getContext("2d")
                c.reset(); c.beginPath()
                c.moveTo(0, 0); c.lineTo(width, 0); c.lineTo(width / 2, height)
                c.closePath()
                c.fillStyle = face.th.idleFg
                c.fill()
            }
        }
    }
}
