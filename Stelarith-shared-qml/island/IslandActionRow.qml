import QtQuick 2.15
import "."

// 灵动岛 · 动作按钮行（独立于展开/收起态，活动态显示）
//
// 从 `IslandRoot.qml` 抽出的**第二块**（2026-10-16 · U 系列 D 档）。
// 规矩同第一块（`IslandIdleFace.qml`）：**只搬不改** + **依赖显式声明**
// （隐式作用域不会跟着搬进独立文件 —— 详见 IslandIdleFace.qml 头部注释）。
//
// ⚠️ 为什么它在**根级**而不是 expandedCol 里（历史 bug，别"顺手"搬回去）：
//   2026-10-11 修「确认按钮点不到」—— 原来 ActionButton 在 expandedCol 里（只有展开态可见），
//   且被根 MouseArea 吞掉点击。现在：① 移到根级（独立于展开/收起态）
//   ② `z: 50`（高于底壳与内容，低于鲸鱼 `z: 100`）
//   ③ ActionButton 自带 MouseArea，自己吃事件。
//
// 布局范式（D 档归档里明确过）：**root 级的行只锚 `parent`**（或用预算公式定位），
//   绝不去锚展开区里的兄弟 —— 跨父级锚会被 QML 静默丢弃（2026-10-14 mediaRow 踩过）。
//   所以本组件**不在内部写锚**，锚定交给实例化处，让"根级行锚 parent"这件事
//   在 IslandRoot 里一眼可见。
Row {
    id: row

    // ── 接口（由 IslandRoot 传入）─────────────────────────────
    property var th              // 主题令牌（IslandTheme 实例，必传）
    property bool showActions: false   // 对应 root._showActionsNow
    property var actions: []     // [{label, role}]
    property var bridgeRef: null // 桥（fireAction 回调）

    spacing: 8
    visible: row.showActions
    height: visible ? 32 : 0
    z: 50    // 高于内容、低于鲸鱼

    Repeater {
        model: row.actions
        delegate: ActionButton {
            theme: row.th
            label: modelData && modelData.label ? modelData.label : String(modelData)
            role: modelData && modelData.role ? modelData.role
                                              : (index === 0 ? "primary" : "secondary")
            onClicked: {
                if (row.bridgeRef && row.bridgeRef.fireAction)
                    row.bridgeRef.fireAction(label)
            }
        }
    }
}
