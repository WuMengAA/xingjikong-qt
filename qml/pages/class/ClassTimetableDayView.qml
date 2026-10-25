// 班级课表 · 单日视图（课程列表 + 当日作息）· 2026-10-25 从 ClassTimetablePane 抽出
//
// 拆件原因：Pane 已经要管「导入 / 选群 / 星期条」，再把两个列表塞进去必撞 200 行红线。
// 本件只认两个入参（当天课表 + 当天作息），不持有状态、不碰 admin —— 好测也好改。
import QtQuick
import QtQuick.Controls          // ScrollBar
import "../../components/theme"  // Md3 令牌（pragma Singleton）

Item {
    id: view

    property var day: null        // { dow, name, lessons:[{index,subject,teacher,start,end}] }
    property var layout: null     // { id, name, items:[{kind,label,start,end,name}] }
    /** 宽度够才显示作息列（窄栏先保课程本身）。 */
    readonly property bool showLayout: view.width >= 520

    Row {
        anchors.fill: parent
        spacing: 12

        ListView {
            id: lessonList
            width: parent.width - (view.showLayout ? 212 : 0)
            height: parent.height
            clip: true
            spacing: 4
            model: view.day ? view.day.lessons : []
            ScrollBar.vertical: ScrollBar { width: 8 }

            delegate: Row {
                width: lessonList.width
                height: 30
                spacing: 10

                Text {
                    text: modelData.index
                    width: 22
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Md3.bodyS
                    font.family: "monospace"
                    color: Md3.frOffDot || "#D1D5DB"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.subject || "（未命名）"
                    width: Math.max(60, view.width * 0.24)
                    elide: Text.ElideRight
                    font.pixelSize: Md3.body13
                    font.weight: Font.Medium
                    color: Md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.start + " - " + modelData.end
                    font.pixelSize: Md3.body13
                    font.family: "monospace"
                    color: Md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.teacher || ""
                    font.pixelSize: Md3.bodyS
                    color: Md3.frInk2 || "#6B7280"
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 这一天空（周日晚自习这类）不是故障，说明白
            Text {
                anchors.centerIn: parent
                visible: lessonList.count === 0
                text: "这一天没有课"
                font.pixelSize: Md3.bodyS
                color: Md3.frOffDot || "#D1D5DB"
            }
        }

        ListView {
            id: layoutList
            visible: view.showLayout
            width: visible ? 200 : 0
            height: parent.height
            clip: true
            spacing: 2
            model: view.layout ? view.layout.items : []
            ScrollBar.vertical: ScrollBar { width: 8 }

            delegate: Row {
                width: layoutList.width
                height: 22
                spacing: 6

                Text {
                    text: modelData.start || ""
                    font.pixelSize: Md3.bodyS
                    font.family: "monospace"
                    color: modelData.kind === "lesson" ? (Md3.frInk || "#111827")
                                                       : (Md3.frOffDot || "#D1D5DB")
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.kind === "lesson" ? (modelData.label || "上课")
                          : modelData.kind === "break" ? (modelData.name || "课间")
                          : modelData.kind === "action" ? "行动" : ""
                    width: 140
                    elide: Text.ElideRight
                    font.pixelSize: Md3.bodyS
                    color: modelData.kind === "lesson" ? (Md3.frInk2 || "#6B7280")
                                                       : (Md3.frOffDot || "#D1D5DB")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
