// 操作区（Action Bar）· QtShadcn 规范（2026-10-29 迁移）
//
// 规格（用户点单）：距离顶部 24px，包含 [ 一键锁屏 ]、[ 批量重启 ] 两个次要操作按钮。
//
// ── 竖屏第三键「返回经典界面」（2026-10-22 自适应布局）─────────────────
// 竖屏时左侧 Sidebar（含 H7 回旧壳出口）被底部 BottomNav 取代，出口不能丢 ——
// 老师不能被困在新壳里。故竖屏在操作区右端补一个 ghost 变体出口，横屏不显示。
//
// ── 变体映射（Md3Btn → AppButton）与 12px 例外 ─────────────────────────
// · 旧 `outlined`（次要、带描边）→ `secondary`（surfaceVariant 底、无边框）
//   规范禁边框，故用底色差替代描边；语义仍是"高后果但非默认主操作"。
// · 旧 `text`（最弱）→ `ghost`（透明底 + 主色文字，hover 出浅底）
//   AppButton 规范只列 primary/secondary/destructive；`ghost` 是为"弱化不抢主操作"
//   补的，理由写在 AppButton.qml 文件头。
// · `radius: 12` **刻意保留**（用户裁决 C 案）：这是 2026-10-19 对监控中心/ActionBar
//   的明确点单例外（AGENTS.md §9.3 已登记），不是全局约定 —— 其余按钮一律
//   `Theme.radius.md`。详见 AppButton.qml 的「局部例外」段。
//
// ⚠️ 执行不许自己发指令：统一走宿主的 `runActionById(actionId)` ——
//    权限门控（permOk）、能力门控（capMissing）、危险动作二次确认都在那一处，
//    这里再写一遍就会变成第二套门控（历史教训同看板页注释）。
import QtQuick
import QtQuick.Layouts
import "../components"        // AppButton（共享 UI，构建期拷到 components/）

Item {
    id: root

    property var ctx

    // 只表达"用户点了什么"，不在这里选设备、不在这里执行 ——
    // 宿主（MainWindow）负责先把本班在线设备塞进 picked 再走 runActionById。
    signal lockRequested()
    signal rebootRequested()
    /** 竖屏「返回经典界面」（H7 出口的 ActionBar 形态）。 */
    signal classicRequested()
    /** 宿主朝向（MainWindow.portrait）：true = 竖屏，显示 classic 出口。 */
    property bool portrait: false

    Layout.fillWidth: true
    Layout.preferredHeight: 40

    RowLayout {
        anchors.fill: parent
        spacing: 12

        AppButton {
            text: "一键锁屏"
            variant: "secondary"
            radius: 12                      // 局部例外（见文件头）
            onClicked: root.lockRequested()
        }
        AppButton {
            text: "批量重启"
            variant: "secondary"
            radius: 12                      // 局部例外（见文件头）
            onClicked: root.rebootRequested()
        }

        Item { Layout.fillWidth: true }

        // 竖屏出口（H7）：右端对齐的 ghost 变体，弱化处理不抢两个主操作
        AppButton {
            visible: root.portrait
            text: "返回经典界面"
            variant: "ghost"
            onClicked: root.classicRequested()
        }
    }
}
