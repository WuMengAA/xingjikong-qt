// 班级展板 · 已发布内容区（2026-10-25）
//
// 展板的**正经功能**：把老师发布出去的通知/作业显示出来。此前 ClassBoardPane 只有
// 设备画面 —— 发出去的东西在展板上完全看不见，等于展板没有内容。
//
// 数据来源：ClassEventModel.recentOfKind("publish", N)（云端 adminPublish 落的事件，
// 载荷 {type,title,body,from}）。宿主负责筛选并透传，本件只负责"轮播 + 排版"。
//
// 轮播语义：多条时每 8s 自动换下一条（大屏常亮场景），圆点可点直接跳；
// 新内容到达（列表变化）回到第一条 —— 刚发布的东西必须第一眼看见。
import QtQuick
import "../../components/fresh"   // FreshCard
import "../../island"             // SvgIcon
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane

    /** publish 事件摊平列表（最新在前）。 */
    property var notices: []
    property int rotateMs: 8000
    property int index: 0

    readonly property int total: pane.notices ? pane.notices.length : 0
    readonly property var cur: (pane.index >= 0 && pane.index < pane.total) ? pane.notices[pane.index] : null

    /** publish 类型 → 人话（英文 type 不许裸奔上界面）。 */
    function typeLabel(t) {
        if (t === "student-notice")    return "学生通知"
        if (t === "class-notice")      return "班级通知"
        if (t === "fullscreen-notice") return "全屏通知"
        if (t === "homework")          return "课后作业"
        return "通知"
    }

    /** 类型 → 强调色（通知/作业分色，扫一眼能分辨）。 */
    function typeTint(t) {
        if (t === "homework")          return Md3.frGreen || "#22C55E"
        if (t === "fullscreen-notice") return Md3.frDanger || "#EF4444"
        return Md3.cInfo || "#2A6394"
    }

    /** ISO 8601 → "MM-DD HH:mm"；脏数据给 "——"，不显示 Invalid Date。 */
    function stamp(at) {
        if (!at) return "——"
        const d = new Date(at)
        if (isNaN(d.getTime())) return "——"
        const p = function (n) { return (n < 10 ? "0" : "") + n }
        return p(d.getMonth() + 1) + "-" + p(d.getDate()) + " " + p(d.getHours()) + ":" + p(d.getMinutes())
    }

    function step(delta) {
        if (pane.total > 0) pane.index = (pane.index + delta + pane.total) % pane.total
    }

    onNoticesChanged: pane.index = 0

    // 多条才转（单条常亮不动，避免"整屏在抖"的错觉）
    Timer {
        interval: pane.rotateMs
        running: pane.total > 1
        repeat: true
        onTriggered: pane.step(1)
    }

    FreshCard {
        anchors.fill: parent
        visible: pane.total > 0

        Column {
            anchors.fill: parent
            spacing: 10

            // ── 头：类型章 + 时间 ──
            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    height: 22
                    width: chip.implicitWidth + 16
                    radius: 11
                    color: Md3.frLine || "#EEF0F3"

                    Text {
                        id: chip
                        anchors.centerIn: parent
                        text: pane.cur ? pane.typeLabel(pane.cur.type) : ""
                        font.pixelSize: Md3.bodyS || 12
                        color: pane.cur ? pane.typeTint(pane.cur.type) : (Md3.frInk2 || "#6B7280")
                    }
                }
                Item { width: Math.max(8, parent.width - 220); height: 1 }
                Text {
                    text: pane.cur ? pane.stamp(pane.cur.at) : ""
                    font.pixelSize: Md3.bodyS || 12
                    font.family: "monospace"
                    color: Md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 标题 ──
            Text {
                width: parent.width
                text: pane.cur ? (pane.cur.title || "") : ""
                visible: text !== ""
                font.pixelSize: Md3.titleL || 22
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: Md3.frInk || "#111827"
            }

            // ── 正文 ──
            Text {
                width: parent.width
                text: pane.cur ? (pane.cur.body || "") : ""
                visible: pane.cur && pane.cur.body !== ""
                font.pixelSize: Md3.body13 || 13
                lineHeight: 1.35
                wrapMode: Text.WordWrap
                maximumLineCount: Math.max(2, Math.floor((parent.height - 96) / 20))
                elide: Text.ElideRight
                color: Md3.frInk || "#111827"
            }

            // ── 底：轮播指示 + 计数（可点，真会跳）──
            Row {
                width: parent.width
                spacing: 6
                visible: pane.total > 1

                Repeater {
                    model: Math.min(pane.total, 12)

                    Rectangle {
                        width: index === pane.index ? 18 : 6
                        height: 6
                        radius: 3
                        color: index === pane.index
                               ? (Md3.frGreen || "#22C55E")
                               : (Md3.frOffDot || "#D1D5DB")

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.index = index
                        }
                    }
                }
                Item { width: 8; height: 1 }
                Text {
                    text: (pane.index + 1) + " / " + pane.total
                    font.pixelSize: Md3.bodyS || 12
                    color: Md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ── 空态：说清楚"这里会显示什么"和"怎么让它有内容" ──
    FreshCard {
        anchors.fill: parent
        visible: pane.total === 0

        Column {
            anchors.centerIn: parent
            spacing: 8

            SvgIcon {
                name: "tv"
                px: 32
                tint: Md3.frOffDot || "#D1D5DB"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "展板还没有内容"
                font.pixelSize: Md3.body13 || 13
                color: Md3.frInk2 || "#6B7280"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "点右上「发布到展板」，通知与作业会同时推到本展板和全班设备"
                width: Math.min(320, pane.width - 64)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: Md3.bodyS || 12
                color: Md3.frOffDot || "#D1D5DB"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
