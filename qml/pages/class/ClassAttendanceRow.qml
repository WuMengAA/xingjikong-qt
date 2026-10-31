// 考勤行（2026-10-31 · 与 ClassAttendancePane 配套拆件）：
// 一个学生的名字/学号/座位 + 四档状态按钮（点一下即存由宿主收口）。
import QtQuick
import QtQuick.Layouts

import "../../components"         // Btn
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Rectangle {
    id: row

    /** 学生 {name, studentNo, seat}（云端名单）。 */
    property var st: ({})
    /** 当前状态：present / late / leave / absent / 空（未点）。 */
    property string cur: ""
    /** 未选班 / 无学号时整行禁点（不是点了没反应）。 */
    property bool clickable: true

    /** 宿主收口保存：pick(studentNo, status)。 */
    signal pick(string studentNo, string status)

    width: ListView.view ? ListView.view.width : 200
    height: 40
    radius: Theme.radius.md
    color: Theme.color.surface

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        Column {
            width: Math.max(56, parent.width - 182)
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: row.st.name !== "" ? row.st.name : ("#" + row.st.studentNo)
                color: Theme.color.textPrimary; font.pixelSize: 13; elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: (row.st.studentNo || "无学号")
                      + (row.st.seat ? " · " + row.st.seat : "")
                color: Theme.color.textSecondary; font.pixelSize: 11; elide: Text.ElideRight
            }
        }

        Repeater {
            model: ["present", "late", "leave", "absent"]
            delegate: Btn {
                width: 38
                height: 26
                anchors.verticalCenter: parent.verticalCenter
                text: ["出", "迟", "假", "缺"][index]
                tip: ["出勤", "迟到", "请假", "缺勤"][index]
                strong: row.cur === modelData
                disabled: !row.clickable
                onClicked: row.pick(row.st.studentNo, modelData)
            }
        }
    }
}