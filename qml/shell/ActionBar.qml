// MD3 操作区（Action Bar）· V1.0 P1 骨架层
//
// 规格（用户点单）：距离顶部 24px，包含 [ 一键锁屏 ]、[ 批量重启 ] 两个 SecondaryButton。
//
// ── SecondaryButton 用哪个变体 ───────────────────────────────────────
// MD3 按钮层级：Filled（主要）> Tonal > Outlined（次要）> Text（最低）。
// 「一键锁屏 / 批量重启」是**高后果**但非默认主操作 ⇒ 选 Outlined（自带描边，克制且不抢主画面），
// 统一 `radius: 12`（方角，与监控页按钮规范一致，药丸形留给 FAB/开关）。
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
    // 宿主（Md3Shell）负责先把本班在线设备塞进 picked 再走 runActionById。
    signal lockRequested()
    signal rebootRequested()

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
    }
}
