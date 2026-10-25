// 班级工作台 · 学生名单的一行（2026-10-25，拆件：宿主撞 200 行红线）
//
// 只负责"长什么样 + 把移出意图抛给宿主"，不碰网络 —— 谁调 classAdmin 由
// ClassStudentPane 决定（一处收口，避免两处各写一遍调用参数）。
import QtQuick
import QtQuick.Layouts

import "../../components"

Item {
    id: row

    property var md3: ({})
    property var student: ({})     // {id,name,studentNo,seat,note}
    property bool hovered: false

    signal removeRequested(string studentId, string name)

    implicitHeight: 40

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: row.hovered ? (row.md3 ? row.md3.cContHigh : "#ECE6F0") : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            spacing: 8

            Text {
                Layout.preferredWidth: 30
                text: (row.student.seat === undefined || row.student.seat === "") ? "—" : row.student.seat
                color: row.md3 ? row.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                text: row.student.name === undefined ? "" : row.student.name
                elide: Text.ElideRight
                color: row.md3 ? row.md3.cOnSurface : "#1C1B1F"
                font.pixelSize: 13
            }
            Text {
                Layout.preferredWidth: 84
                text: (row.student.studentNo === undefined || row.student.studentNo === "")
                      ? "无学号" : row.student.studentNo
                elide: Text.ElideRight
                color: row.md3 ? row.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 12
            }
            Btn {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 26
                theme: row.md3
                text: "移出"
                tip: "从本班名单移除（学生本来就没有账号，不影响任何登录）"
                onClicked: row.removeRequested(row.student.id, row.student.name)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton       // 只吃悬停，不抢「移出」按钮的点击
        onEntered: row.hovered = true
        onExited: row.hovered = false
    }
}
