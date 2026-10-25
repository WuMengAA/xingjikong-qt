// 班级工作台 · 学生编辑弹窗（2026-10-25）
//
// 为什么是弹窗而不是行内编辑：一行只有 40px 高，塞四个输入框会把名单挤成表格，
// 且行内保存/取消两个按钮在窄中栏里必然换行。编辑是一次性动作，弹窗更稳。
//
// 纪律：① 只认 student-update 这一条回执（其余学生写操作归 ClassStudentPane）；
//       ② 失败保留内容可改（绝不静默清空）；③ 姓名空不许保存（云端也拒，但别让人白跑一趟）。
import QtQuick
import "../../components"         // Btn 等基础件
import "../../components/md3"     // Md3TextField（共享 ui/md3 同步进来）
import "../../components/fresh"   // FreshCard / FreshBtn
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: dlg

    property string classCode: ""
    property var toastFn: null
    property var student: ({})
    property bool opened: false
    property bool saving: false

    property string fName: ""
    property string fNo: ""
    property string fSeat: ""
    property string fNote: ""

    signal closed()

    visible: opened
    anchors.fill: parent

    function toast(m, e) { if (dlg.toastFn) dlg.toastFn(m, e === true) }
    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    function open(st) {
        dlg.student = st || ({})
        dlg.fName = st && st.name ? st.name : ""
        dlg.fNo = st && st.studentNo ? st.studentNo : ""
        dlg.fSeat = st && st.seat ? st.seat : ""
        dlg.fNote = st && st.note ? st.note : ""
        dlg.saving = false
        dlg.opened = true
    }

    function close() { dlg.opened = false; dlg.closed() }

    function save() {
        const a = dlg.admin()
        const id = dlg.student && dlg.student.id ? dlg.student.id : ""
        if (!a || id === "" || dlg.saving) return
        const name = dlg.fName.trim()
        if (name === "") { dlg.toast("姓名不能为空", true); return }
        dlg.saving = true
        a.updateStudent(dlg.classCode, id, name, dlg.fNo.trim(), dlg.fSeat.trim(), dlg.fNote.trim())
    }

    Connections {
        target: dlg.admin()
        ignoreUnknownSignals: true
        function onOpFinished(op, ok, message) {
            if (op !== "student-update") return
            dlg.saving = false
            if (ok) { dlg.toast(message, false); dlg.close() }
            else dlg.toast(message, true)      // 内容保留，改完可重试
        }
    }

    // 遮罩（挡背后点击，与发布弹窗同款）
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }
    }

    FreshCard {
        id: card
        anchors.centerIn: parent
        width: Math.min(dlg.width - 48, 420)
        height: 320

        Column {
            anchors.fill: parent
            spacing: 10

            Text {
                width: parent.width
                text: "编辑学生"
                font.pixelSize: Md3.titleM
                font.weight: Font.Medium
                color: Md3.frInk || "#111827"
            }
            Text {
                width: parent.width
                text: "学号重复会被云端当成同一个学生 —— 改名请留空学号或保持原值。"
                wrapMode: Text.WordWrap
                font.pixelSize: Md3.bodyS
                color: Md3.frInk2 || "#6B7280"
            }

            Md3TextField {
                width: parent.width
                placeholder: "姓名（必填）"
                text: dlg.fName
                onTextChanged: if (text !== dlg.fName) dlg.fName = text
            }
            Md3TextField {
                width: parent.width
                placeholder: "学号（可空）"
                text: dlg.fNo
                onTextChanged: if (text !== dlg.fNo) dlg.fNo = text
            }
            Row {
                width: parent.width
                spacing: 8
                Md3TextField {
                    width: 96
                    placeholder: "座位"
                    text: dlg.fSeat
                    onTextChanged: if (text !== dlg.fSeat) dlg.fSeat = text
                }
                Md3TextField {
                    width: parent.width - 104
                    placeholder: "备注（可空）"
                    text: dlg.fNote
                    onTextChanged: if (text !== dlg.fNote) dlg.fNote = text
                }
            }

            Row {
                width: parent.width
                spacing: 8
                FreshBtn {
                    width: (parent.width - 8) / 2
                    text: dlg.saving ? "保存中…" : "保存"
                    variant: "primary"
                    enabled: !dlg.saving && dlg.fName.trim() !== ""
                    onClicked: dlg.save()
                }
                FreshBtn {
                    width: (parent.width - 8) / 2
                    text: "取消"
                    variant: "outlined"
                    onClicked: dlg.close()
                }
            }
        }
    }
}
