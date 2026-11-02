// 班级工作台 · 中栏「学生名单」（2026-10-25 · 云端数据库）
//
// 名单跟班走（换管理端、重装系统都还在）。成员是**登录账号**，学生不是账号，
// 是班级事务的对象（考勤/座位/个人信息都挂在它上面）。
// 数据面全在云端：backend.classAdmin.students(code) → studentsList(code, list)。
// 本件不缓存、不猜：拉不到就显示"还没录入"，绝不摆一张假名单。
import QtQuick
import QtQuick.Layouts

import "../../components"
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）
import "StudentCsv.js" as StudentCsv   // CSV 解析（逗号/制表符/表头自适应）

Item {
    id: pane

    property string classCode: ""
    property bool subscribed: false
    property var toastFn: null

    /** 编辑请求 → 页级弹窗（宿主 ClassDashboard 收口，与发布弹窗同层）。 */
    signal editStudentRequested(var student)

    /** 名单（云端回填，[{id,name,studentNo,seat,note}]）。 */
    property var students: []
    property bool loading: false
    /** 输入行三格 —— 加完立刻清空（避免"加了两次同一个人"）。 */
    property string inName: ""
    property string inNo: ""
    property string inSeat: ""

    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }
    function toast(m, e) { if (pane.toastFn) pane.toastFn(m, e === true) }

    function reload() {
        if (!pane.subscribed || pane.classCode === "") return
        const a = pane.admin()
        if (!a) return
        pane.loading = true
        a.students(pane.classCode)
    }

    function addOne() {
        const a = pane.admin()
        if (!a) return
        const nm = pane.inName.trim()
        if (nm === "") { pane.toast("学生姓名不能为空", true); return }
        a.addStudent(pane.classCode, nm, pane.inNo.trim(), pane.inSeat.trim())
        pane.inName = ""
        pane.inNo = ""
        pane.inSeat = ""
    }

    onSubscribedChanged: reload()
    onClassCodeChanged: reload()

    /** CSV 导入：本地解析 → 云端按学号幂等 upsert（同号视为同一人更新）。
     *  列约定见 StudentCsv.js；空姓名行跳过，不塞"无名氏"。 */
    function importCsv() {
        const a = pane.admin()
        if (!a || pane.classCode === "" || typeof backend === "undefined" || !backend) return
        const path = backend.pickFile()
        if (!path) return                       // 取消选择：不报错
        const text = a.readTextFile(path, 2 * 1024 * 1024)
        if (!text) { pane.toast(a.lastFileError() || "读不到文件内容", true); return }
        const res = StudentCsv.parse(text)
        if (res.rows.length === 0) {
            pane.toast("CSV 里没有可导入的行（第一列应为姓名）", true)
            return
        }
        a.importStudents(pane.classCode, res.rows)
    }

    // 首次就位也要拉一次：进班级看板时 subscribed/classCode 往往**在实例化前就已是
    // 真值**，两个 onChanged 都不会触发 ⇒ 名单会一直空着（看着像"没录入"）。
    Component.onCompleted: reload()

    // 云端回执：名单刷新 / 写操作做完重拉（不本地乐观插入 —— 座位排序由云端定）
    Connections {
        target: pane.admin()
        function onStudentsList(code, list) {
            if (code !== pane.classCode) return
            pane.loading = false
            pane.students = list
        }
        function onOpFinished(op, ok, message) {
            if (!ok) { if (op.indexOf("student") === 0) pane.toast(message, true); return }
            if (op.indexOf("student") === 0) { pane.toast(message, false); pane.reload() }
        }
    }

    Column {
        anchors.fill: parent
        spacing: 10

        // ── 标题行：人数现算（不另存计数）──
        Row {
            width: parent.width
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "学生名单"
                color: Theme.color.textPrimary
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pane.subscribed
                      ? (pane.students.length + " 人" + (pane.loading ? " · 读取中…" : ""))
                      : "未选班"
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
            Item { width: Math.max(8, parent.width - 208); height: 1 }
            Btn {
                width: 88
                height: 26
                theme: ({ })
                text: "导入 CSV"
                tip: "列：姓名,学号,座位,备注（有表头自动认列；学号重复=更新同一人）"
                disabled: !pane.subscribed
                onClicked: pane.importCsv()
            }
        }

        // ── 输入行（未订阅时整块禁用，而不是点了没反应）──
        Column {
            width: parent.width
            spacing: 8
            visible: pane.subscribed

            AppInput {
                width: parent.width
                placeholder: "姓名（必填）"
                text: pane.inName
                onTextChanged: if (text !== pane.inName) pane.inName = text
            }
            RowLayout {
                width: parent.width
                spacing: 8
                AppInput {
                    Layout.fillWidth: true
                    placeholder: "学号"
                    text: pane.inNo
                    onTextChanged: if (text !== pane.inNo) pane.inNo = text
                }
                AppInput {
                    Layout.preferredWidth: 72
                    placeholder: "座位"
                    text: pane.inSeat
                    onTextChanged: if (text !== pane.inSeat) pane.inSeat = text
                }
                Btn {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 44
                    theme: ({ })
                    text: "加入"
                    strong: true
                    disabled: pane.inName.trim() === ""
                    onClicked: pane.addOne()
                }
            }
        }

        // ── 名单 ──
        ListView {
            width: parent.width
            height: parent.height - 190
            clip: true
            visible: pane.subscribed && pane.students.length > 0
            model: pane.students
            spacing: 2
            delegate: ClassStudentRow {
                width: ListView.view.width
                student: modelData
                onEditRequested: function (st) { pane.editStudentRequested(st) }
                onRemoveRequested: function (id, name) {
                    const a = pane.admin()
                    if (a) a.removeStudent(pane.classCode, id)
                }
            }
        }

        // ── 空态 / 未选班 ──
        Text {
            width: parent.width
            visible: !pane.subscribed || (pane.students.length === 0 && !pane.loading)
            wrapMode: Text.Wrap
            text: !pane.subscribed
                  ? "先在左上角选一个班级，再录入这个班的学生。"
                  : "这个班还没录入学生。上面填姓名点「加入」就能开始；连同考勤、座位表都按这份名单走。"
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
    }
}
