// 班级工作台 · 行内输入行（新建班级 / 修改班名 共用）
//
// 为什么抽出来：ClassAdminPane 要守 200 行红线（AGENTS.md §10）；两个输入行
// （新建/改名）结构相同，只差提示语、预填值、确认启用条件与确认动作。
//
// 铁律：
//   · text 唯一真源在本件内。父级打开输入行时调 open(t) **显式预填** —— 不能
//     只绑 property：用户键入会摧毁 Md3TextField 内部 input.text↔root.text 的
//     绑定（Qt 语义：原生赋值即消绑定），之后外部改值不再显示到可见输入框
//     （2026-10-24 实证，Md3TextField.setText 就是为这个场景加的）；
//   · 本件不碰任何班级状态：确认时把 trim 后的文本交给父级，取消只发信号；
//   · 取消后父级收起（visible=false）即可，残留文本不影响下次 —— 下次打开
//     必然经过 open(t) 重置。
import QtQuick
import "../../components/md3"     // Md3TextField / Md3Btn
import "../../components/theme"   // Md3 令牌（pragma Singleton）

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
    function open(t) { field.setText(t || ""); row.text = t || "" }

    Column {
        id: col
        width: parent.width
        spacing: 8

        Text {
            width: parent.width
            visible: row.hint !== ""
            text: row.hint
            font.pixelSize: Md3.bodyS
            color: Md3.cOutline
            wrapMode: Text.WordWrap
        }

        Md3TextField {
            id: field
            width: parent.width
            placeholder: row.placeholder
            text: row.text
            onTextChanged: row.text = text
        }

        Row {
            width: parent.width
            spacing: 8

            Md3Btn {
                text: row.confirmText
                variant: "filled"
                enabled: row.confirmEnabled
                onClicked: row.confirmed(row.text.trim())
            }
            Md3Btn {
                text: "取消"
                variant: "text"
                onClicked: row.cancelled()
            }
        }
    }
}
