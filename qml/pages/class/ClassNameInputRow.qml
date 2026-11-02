// 班级工作台 · 行内输入行（新建班级 / 修改班名 共用）· QtShadcn 规范（2026-10-29 迁移）
//
// 为什么抽出来：ClassAdminPane 要守 200 行红线（AGENTS.md §10）；两个输入行
// （新建/改名）结构相同，只差提示语、预填值、确认启用条件与确认动作。
//
// ── 铁律（原样保留）────────────────────────────────────────────────────
//   · text 唯一真源在本件内。父级打开输入行时调 open(t) **显式预填** —— 不能
//     只绑 property：用户键入会摧毁输入组件的 text↔root.text 绑定
//     （Qt 语义：原生赋值即消绑定），之后外部改值不再显示到可见输入框
//     （2026-10-24 实证）。`AppInput` 的 text 是 `input.text` 的别名，
//     所以 `open()` 里直接 `field.text = t` 等价于原来的 `setText()`。
//   · 本件不碰任何班级状态：确认时把 trim 后的文本交给父级，取消只发信号。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · `Column`/`Row` → `ColumnLayout`/`RowLayout`
// · `onTextChanged` → `onTextEdited`：只在**用户编辑**时回写，
//   避免 open() 程序化赋值再触发一轮回写
import QtQuick
import QtQuick.Layouts
import "../../components"          // AppButton / AppInput
import "../../components/theme"    // Theme 令牌

Item {
    id: row

    /** 输入框上方的小字说明（可空；改名行用来说「只改显示名，代码不变」）。 */
    property string hint: ""
    property string placeholder: ""
    /** 当前文本（唯一真源）。确认时父级拿到 trim 后的值。 */
    property string text: ""
    property string confirmText: "确定"
    property bool confirmEnabled: true

    signal confirmed(string t)
    signal cancelled()

    implicitWidth: 280
    implicitHeight: col.implicitHeight

    /** 打开输入行：显式预填/清空（见文件头铁律，必须显式）。 */
    function open(t) {
        field.text = t || ""
        row.text = t || ""
    }

    ColumnLayout {
        id: col
        width: parent.width
        spacing: Theme.spacing.sm

        Text {
            Layout.fillWidth: true
            visible: row.hint !== ""
            text: row.hint
            font.pixelSize: 13
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        AppInput {
            id: field
            Layout.fillWidth: true
            placeholder: row.placeholder
            text: row.text
            onTextEdited: row.text = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm

            AppButton {
                text: row.confirmText
                variant: "primary"
                enabled: row.confirmEnabled
                onClicked: row.confirmed(row.text.trim())
            }
            AppButton {
                text: "取消"
                variant: "ghost"
                onClicked: row.cancelled()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
