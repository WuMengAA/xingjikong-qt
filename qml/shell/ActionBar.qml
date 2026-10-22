// MD3 操作区（Action Bar）· V1.0 P1 骨架层
//
// 规格（用户点单）：距离顶部 24px，包含 [ 一键锁屏 ]、[ 批量重启 ] 两个 SecondaryButton。
//
// ── 竖屏第三键「返回经典界面」（2026-10-22 自适应布局）─────────────────
// 竖屏时左侧 Sidebar（含 H7 回旧壳出口）被底部 BottomNav 取代，出口不能丢 ——
// 老师不能被困在新壳里。故竖屏在操作区右端补一个 text 变体出口，
// 横屏不显示（那条路在 Sidebar 底部）。两条路都通向 page 1，同一目的地。
//
// ── SecondaryButton 用哪个变体 ───────────────────────────────────────
// MD3 按钮层级：Filled（主要）> Tonal > Outlined（次要）> Text（最低）。
// 「一键锁屏 / 批量重启」是**高后果**但非默认主操作 ⇒ 选 Outlined（自带描边，克制且不抢主画面），
// 统一 `radius: 12`（方角，与监控页按钮规范一致，药丸形留给 FAB/开关）。
// ⚠️ 2026-10-22 注：规范默认按钮为药丸（shape.full）；此处方角是用户 2026-10-19
//    对监控中心/ActionBar 的明确点单例外（AGENTS.md §9.3 已登记），新代码默认药丸。
//
// ⚠️ 执行不许自己发指令：统一走宿主的 `runActionById(actionId)` ——
//    权限门控（permOk）、能力门控（capMissing）、危险动作二次确认都在那一处，
//    这里再写一遍就会变成第二套门控（历史教训同看板页注释）。
import QtQuick
import QtQuick.Layouts
import "../components/md3"

Item {
    id: root

    property var ctx
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

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

        Md3Btn {
            md3: root.th
            text: "一键锁屏"
            variant: "outlined"
            radius: 12
            tip: "对当前班全部在线设备下发锁定桌面"
            onClicked: root.lockRequested()
        }
        Md3Btn {
            md3: root.th
            text: "批量重启"
            variant: "outlined"
            radius: 12
            tip: "对当前班全部在线设备下发远程重启"
            onClicked: root.rebootRequested()
        }

        Item { Layout.fillWidth: true }

        // 竖屏出口（H7）：右端对齐的 text 变体，弱化处理不抢两个主操作
        Md3Btn {
            md3: root.th
            visible: root.portrait
            text: "返回经典界面"
            variant: "text"
            tip: "回到经典三栏界面"
            onClicked: root.classicRequested()
        }
    }
}
