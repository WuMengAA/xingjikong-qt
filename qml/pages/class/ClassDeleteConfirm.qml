// 班级工作台 · 删除班级两段确认（行内红字条）· QtShadcn 规范（2026-10-29 迁移）
//
// 为什么行内、不借弹窗：scrim 的 anchors.fill 会把对话框锁在父卡片内，
// 且面板自己会塌高度（2026-10-24 踩过，别再试）。两段确认 = 先点「删除班级」
// 出现本红字条，再点「确认删除」才真删 —— 不可逆操作的缓冲。
// 云端默认策略也是 refuse（班内有设备即拒），这里是双保险。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · `Md3.rS` → `Theme.radius.sm`；`Md3.bodyS` → 13（规范字号 12/13/14/16）
// · **规范无 errorContainer 语义**：`Md3.cErrorCont`（浅红底）改用
//   `Theme.color.destructive` 叠 10% 透明度派生 —— 仍是令牌派生，不硬编码 Hex
// · `Md3Btn error/text` → `AppButton destructive/ghost`
// · `Column`/`Row` → `ColumnLayout`/`RowLayout`（规范只许 Layout 系列）
// · `anchors.margins: 10` → `Theme.spacing.sm`(8)，严格 8 的倍数
import QtQuick
import QtQuick.Layouts
import "../../components"          // AppButton
import "../../components/theme"    // Theme 令牌

Item {
    id: confirm

    /** 班名（显示名；空则回落班码，由父级算好传入）。 */
    property string targetName: ""

    signal confirmed()
    signal cancelled()

    /** 警告底：危险色叠 10% 透明（规范无 errorContainer 语义，故派生）。 */
    readonly property color _warnBg: Qt.rgba(Theme.color.destructive.r,
                                             Theme.color.destructive.g,
                                             Theme.color.destructive.b, 0.10)
    readonly property color _warnFg: Theme.color.destructive

    implicitWidth: 280
    implicitHeight: body.implicitHeight + Theme.spacing.md

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.sm
        color: confirm._warnBg
        // 无边框（规范铁律）

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: Theme.spacing.sm
            spacing: Theme.spacing.sm

            Text {
                Layout.fillWidth: true
                text: "删除「" + confirm.targetName + "」？班内所有设备将被移出班级（设备本身不受影响），此操作不可撤销。"
                font.pixelSize: 13
                color: confirm._warnFg
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.sm

                AppButton {
                    text: "确认删除"
                    variant: "destructive"
                    size: "sm"
                    onClicked: confirm.confirmed()
                }
                AppButton {
                    text: "再想想"
                    variant: "ghost"
                    size: "sm"
                    onClicked: confirm.cancelled()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
