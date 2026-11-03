import QtQuick 2.15
import "."

// 灵动岛 · 组件面板「今日课表」（2026-10-27 · G5）
//
// ── 数据从哪来 ──────────────────────────────────────────────────
//   PanelDomain::buildTimetable() → ScheduleClock::todayLessons()。
//   与「上课/下课提醒」**同一份判定数据**（同一个 ScheduleClock 实例）——
//   两份数据会分叉：档案换了只刷新一边，学生看到"岛上课表有语文、
//   语文课铃却不响"。
//
// ── 三态（state）────────────────────────────────────────────────
//   now  正在上：accent 竖条 + accent 文字（一眼定位"现在在哪节"）
//   done 已上完：整行降到 sub 色、透明度 0.55（可读但不抢注意力）
//   next 下一节：细描边高亮（不填色 —— 填色会和 now 混淆）
//   ""   还没到：正常色
//
// ── 空课表不是错误 ──────────────────────────────────────────────
//   周末/假期/档案没导入都会是空的。这里写一句"今天没有课"，
//   而不是留一块空板子（老师会以为功能坏了）。
Item {
    id: face

    property var th: null
    property var info: ({})

    readonly property int pad: 20
    readonly property var lessons: face.info.lessons || []
    // 2026-11-03（m14970 ③）：横向胶囊条形态下不再需要 rowH 逐节行高（胶囊定高 30）。
    // 全称提示行内容：点任一科目胶囊后短暂显示「节次 · 全称（到 xx:xx）」。
    property string fullHint: ""

    // ── 表头：标题 + 周次 + 节数 ─────────────────────────────────
    Item {
        id: head
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: face.pad
        anchors.rightMargin: face.pad
        anchors.topMargin: face.pad
        height: 24

        Text {
            id: headTitle
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: face.info.title || "今日课表"
            color: face.th ? face.th.fg : "#FAFAFA"
            font.pixelSize: face.th ? face.th.fsTitle : 15
            font.weight: Font.Medium
        }

        // 右侧：周次 + 共几节（同一行右侧对齐；拆两个 Text 是为了各自配色）
        Text {
            id: headCount
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: (face.info.count || 0) > 0 ? ("共 " + face.info.count + " 节") : ""
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: face.th ? face.th.fsNote : 12
        }

        Text {
            id: headWeek
            anchors.right: headCount.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: face.info.week || ""
            color: face.th ? face.th.accent : "#D0BCFF"
            font.pixelSize: face.th ? face.th.fsNote : 12
            font.weight: Font.Medium
        }
    }

    // ── 空态 ──────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: face.lessons.length === 0
        text: "今天没有课"
        color: face.th ? face.th.sub : "#C8C8C8"
        font.pixelSize: face.th ? face.th.fsBodyLg : 15
    }

    // ── 课表条（2026-11-03 · m14970 ③ · 用户裁决「横向排列的每科简称」）──
    // 纵向逐节列表改为**横向科目条**：每科一个胶囊，显示 `subjectAbbr`（简称，
    // ClassIsland 档案 simplifiedName，缺了回退全称——数据链 panel_domain.cpp 已给）。
    // 参照用户截图：`语 英 体 数 数 政 化 化 自 语 地` 一条排开。当前节 accent 描边，
    // 已结课降透明度；悬停/长按无（被控端触屏点按才弹 tooltip 式全称行）。
    // 横向放不下 ⇒ Flickable 横滚（纵向面板的老教训，横条同理）。
    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: face.pad
        anchors.rightMargin: face.pad
        anchors.topMargin: 8
        anchors.bottomMargin: face.pad
        visible: face.lessons.length > 0
        clip: true
        contentHeight: height
        contentWidth: strip.implicitWidth
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: strip
            spacing: 6

            Repeater {
                model: face.lessons
                delegate: Rectangle {
                    id: chip
                    // ⚠️ 不能叫 state —— Item 自带 state（QML 状态机），重名会直接加载失败
                    readonly property string rowState: modelData.state || ""
                    width: chipLabel.implicitWidth + 20
                    height: 30
                    radius: 8
                    color: chip.rowState === "now"
                           ? (face.th ? face.th.accent : "#D0BCFF")
                           : (face.th ? face.th.surfaceVariant : "#2A2A2A")
                    opacity: chip.rowState === "done" ? 0.45 : 1.0

                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData.subjectAbbr || modelData.subject || ""
                        color: chip.rowState === "now"
                               ? (face.th ? face.th.onAccent || "#1B1B1B" : "#1B1B1B")
                               : (face.th ? face.th.fg : "#FAFAFA")
                        font.pixelSize: face.th ? face.th.fsBodyLg : 15
                        font.weight: chip.rowState === "now" ? Font.Medium : Font.Normal
                    }

                    // 全称提示：简称被截/语义不清时老师点一下看全称（节次+全称+时间）
                    TapHandler { onTapped: face.fullHint = (modelData.period || "") + " · "
                        + (modelData.subject || "") + (modelData.end ? ("（到 " + modelData.end + "）") : "") }
                }
            }
        }
    }

    // 全称提示行（点胶囊后短暂显示；下一次点击/翻面被覆盖）
    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: face.pad
        visible: face.fullHint !== ""
        text: face.fullHint
        color: face.th ? face.th.sub : "#C8C8C8"
        font.pixelSize: face.th ? face.th.fsNote : 12
        elide: Text.ElideRight
    }
}
