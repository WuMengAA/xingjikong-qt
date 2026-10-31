// 班级工作台 · 考勤面板（2026-10-31 · 云端 class_attendance）
// 名单打底 + 当日考勤覆盖；点一下即存（upsert 幂等，改判不重复）；统计口径归云端。
import QtQuick
import QtQuick.Layouts

import "../../components"         // Btn
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: pane

    property string classCode: ""
    property bool subscribed: false
    property var toastFn: null

    property var students: []            // 名单 [{id,name,studentNo,seat}]
    property var recByNo: ({})           // 当日考勤：studentNo → status
    property var stats: ({})             // 云端统计 {counts, recorded, rosterSize}
    property bool loading: false
    property string dateText: ""         // 看的日期 YYYY-MM-DD（今天起步，可翻）

    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }
    function toast(m, e) { if (pane.toastFn) pane.toastFn(m, e === true) }

    function todayStr(offsetDays) {
        var d = new Date()
        if (offsetDays) d.setDate(d.getDate() + offsetDays)
        function p(n) { return n < 10 ? "0" + n : "" + n }
        return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate())
    }
    function curDate() { return pane.dateText === "" ? pane.todayStr(0) : pane.dateText }
    function shift(days) { pane.dateText = pane.todayStr(days) }

    function reload() {
        if (!pane.subscribed || pane.classCode === "") return
        const a = pane.admin()
        if (!a) return
        pane.loading = true
        a.students(pane.classCode)
        a.attendance(pane.classCode, pane.curDate())
    }

    /** 点某生某档：立即保存（云端 upsert），成功后重拉统计。 */
    function setStatus(no, status) {
        const a = pane.admin()
        if (!a || !no) return
        if (pane.recByNo[no] === status) return        // 点同一档 = 空操作
        a.saveAttendance(pane.classCode, pane.curDate(), "", [
            { studentNo: no, status: status }
        ])
        var next = {}
        for (var k in pane.recByNo) next[k] = pane.recByNo[k]
        next[no] = status                                  // 乐观更新，行内即时可见
        pane.recByNo = next
    }

    /** 已点人数（本地 recByNo 键数，即时反馈用；对账以云端 stats.recorded 为准）。 */
    function attendedCount() { var n = 0; for (var k in pane.recByNo) n++; return n }
    /** 某档统计（云端 counts[k]，未返回按 0）。 */
    function countOf(k) {
        return (pane.stats && pane.stats.counts && pane.stats.counts[k] !== undefined)
            ? pane.stats.counts[k] : 0
    }

    onSubscribedChanged: reload()
    onClassCodeChanged: reload()
    onDateTextChanged: reload()

    Connections {
        target: pane.admin()
        function onStudentsList(code, list) {
            if (code !== pane.classCode) return
            pane.students = list
            pane.loading = false
        }
        function onAttendanceList(code, date, records, st) {
            if (code !== pane.classCode) return
            pane.loading = false
            var m = {}
            for (var i = 0; i < records.length; ++i) {
                var r = records[i]
                if (r.studentNo) m[r.studentNo] = r.status
            }
            pane.recByNo = m
            pane.stats = st || {}
        }
        function onOpFinished(op, ok, message) {
            if (op !== "attendance") return
            if (!ok) { pane.toast(message, true); pane.reload(); return }
            // 乐观更新已生效；再拉一次让统计与云端对齐（口径不改数）
            const a = pane.admin()
            if (a) a.attendance(pane.classCode, pane.curDate())
        }
    }

    Column {
        width: parent.width
        spacing: 10

        // ── 页头：日期（可前后翻）+ 已点/总人数 ──
        Row {
            width: parent.width
            spacing: 8
            Btn { width: 28; height: 26; text: "‹"; disabled: !pane.subscribed
                  onClicked: pane.shift(-1) }
            Column {
                width: 116
                Text {
                    text: pane.curDate().slice(5)
                    color: Theme.color.textPrimary; font.pixelSize: 14; font.weight: Font.Medium
                }
                Text {
                    text: pane.subscribed
                          ? (pane.attendedCount() + " / " + pane.students.length + " 已点" + (pane.loading ? " · 读取中…" : ""))
                          : "未选班"
                    color: Theme.color.textSecondary; font.pixelSize: 12
                }
            }
            Btn { width: 28; height: 26; text: "›"; disabled: !pane.subscribed
                  onClicked: pane.shift(1) }
            Item { width: Math.max(4, parent.width - 180); height: 1 }
        }

        // ── 四档统计（云端 counts）──
        Row {
            width: parent.width
            spacing: 6
            Repeater {
                model: [{k:"present",label:"出勤",c:Theme.color.success},{k:"late",label:"迟到",c:Theme.color.warn},{k:"leave",label:"请假",c:Theme.color.textSecondary},{k:"absent",label:"缺勤",c:Theme.color.destructive}]
                delegate: Rectangle {
                    width: (parent.width - 18) / 4
                    height: 30
                    radius: Theme.radius.md
                    color: Theme.color.surfaceVariant
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label + " " + pane.countOf(modelData.k)
                        color: modelData.c
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ── 学生行列表 ──
        ListView {
            width: parent.width
            height: parent.height - 132
            clip: true
            visible: pane.subscribed && pane.students.length > 0
            model: pane.students
            spacing: 4
            delegate: ClassAttendanceRow {
                st: modelData
                cur: pane.recByNo[modelData.studentNo] || ""
                clickable: pane.subscribed && modelData.studentNo !== undefined
                onPick: function (no, s) { pane.setStatus(no, s) }
            }
        }

        // ── 空态 / 未选班 ──
        Text {
            width: parent.width
            visible: !pane.subscribed || (pane.students.length === 0 && !pane.loading)
            wrapMode: Text.Wrap
            text: !pane.subscribed
                  ? "先在左上角选一个班级。"
                  : "这个班还没录入学生。去「学生」页加名单，考勤按名单走。"
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
    }
}