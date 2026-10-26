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
    // 行高 30 + 行距 1：8 节 = 8*31-1 = 247px，正好落在可用区（320−20上−24表头−8间距−20下=248）。
    // ⚠️ 别再加回 34：那是两行时间列的旧值，8 节会被切掉最后一节（2026-10-27 探针实测）。
    readonly property int rowH: 30

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

    // ── 课表列表 ───────────────────────────────────────────────
    // Flickable 而不是纯 Column：一天 8~10 节 + 小屏（或 125% 缩放下高度被压）
    // 会超出面板；纵向面板必须能滚（布局红线的老教训）。
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
        contentWidth: width
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: flick.width
            spacing: 1

            Repeater {
                model: face.lessons
                delegate: Item {
                    width: col.width
                    height: face.rowH

                    // ⚠️ 不能叫 state —— Item 自带 state（QML 状态机），重名会直接加载失败
                    readonly property string rowState: modelData.state || ""

                    // 状态竖条：只在 now 上出现（3×18 圆角条，靠左缘）
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 18
                        radius: 1.5
                        visible: parent.rowState === "now"
                        color: face.th ? face.th.accent : "#D0BCFF"
                    }

                    // 时间列：**只放开始时间**（单行）。
                    // ⚠️ 2026-10-27 探针实测：原来上下两行（13px 起 / 12px 止）导致
                    //    行高必须 ≥34，8 节 = 288px > 可用 248px ⇒ 最后一节被切在
                    //    Flickable 边界外，看着像"课表少了半节"。结束时间改到右侧
                    //    「第一节 · 08:40」同排显示 —— 信息一个不少，行高压到 30。
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.start || ""
                        color: parent.rowState === "now"
                               ? (face.th ? face.th.accent : "#D0BCFF")
                               : (face.th ? face.th.fg : "#FAFAFA")
                        font.pixelSize: face.th ? face.th.fsBody : 13
                        font.weight: Font.Medium
                    }

                    // 科目（主信息）：给足宽度，长名字省略号
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 62
                        anchors.right: periodText.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.subject || ""
                        color: parent.rowState === "done"
                               ? (face.th ? face.th.sub : "#C8C8C8")
                               : (face.th ? face.th.fg : "#FAFAFA")
                        opacity: parent.rowState === "done" ? 0.55 : 1.0
                        font.pixelSize: face.th ? face.th.fsBodyLg : 15
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    // 节次名 + 结束时间（一行：「第一节 · 08:40」）
                    Text {
                        id: periodText
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: (modelData.period || "")
                              + ((modelData.end || "") !== "" ? (" · " + modelData.end) : "")
                        color: face.th ? face.th.sub : "#C8C8C8"
                        opacity: parent.rowState === "done" ? 0.55 : 1.0
                        font.pixelSize: face.th ? face.th.fsNote : 12
                    }
                }
            }
        }
    }
}
