import QtQuick 2.15
import "."

// 灵动岛 · 组件面板「日期」（2026-10-27 · G4）
//
// ── 内容口径（都在 C++ 算，本面只排版）───────────────────────────────
//   PanelDomain::buildDate() 给：time（打开那一刻 HH:mm）、date、weekday、
//   week（"第 9 周"）、nowMs（判定时间戳）、skewMs（总偏移）。
//   本面**只做滴答**：拿 nowMs 起算、用 Date.now() 续走 —— 不自己读系统钟，
//   否则 NTP 校正/人工偏移一开，面板时间就和上下课提醒分叉。
//
// ── 为什么要有这一面 ─────────────────────────────────────────────
//   ClassIsland 的"日期组件"看似只是显示今天几号，实际承担两件事：
//   ① 学生抬头能确认"今天周几、第几周"（走班/单双周靠它）；
//   ② 它是**常驻看板**，不打断任何人（所以走展开区而不是通知胶囊）。
Item {
    id: face

    property var th: null
    property var info: ({})

    readonly property int pad: 24

    // 每秒推一次的重算触发器。
    // ⚠️ 绑定里必须**读一下** tick，否则 Qt 不知道这个绑定依赖它（纯 function 不算依赖）。
    property int tick: 0
    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: face.tick++
    }

    // 面板打开那一刻的墙钟（只用于算"过了多久"，不参与显示时刻本身）
    property double wallAtOpen: Date.now()

    readonly property double nowMs: {
        face.tick                                  // ← 依赖：每秒重算
        const base = Number(face.info.nowMs || 0)
        if (base <= 0) return Date.now()           // 兜底：C++ 没给就退回系统钟
        return base + (Date.now() - face.wallAtOpen)
    }

    function two(n) { return (n < 10 ? "0" : "") + n }
    function hm(ms) {
        const d = new Date(ms)
        return two(d.getHours()) + ":" + two(d.getMinutes())
    }

    Column {
        anchors.centerIn: parent
        spacing: 6

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: face.hm(face.nowMs)
            color: face.th ? face.th.fg : "#FAFAFA"
            // 2026-11-03（m14970 ②）：用户点单「时间组件默认字体大小为 30px」。
            // 刻意不走令牌阶梯（原 fsTitleXxl=64 太大，把课表横向条挤没了）——本条即引用出处。
            font.pixelSize: 30
            font.weight: Font.Medium
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // 日期 + 星期同一行：中文里"2026年10月27日 星期二"是最自然的读法
            text: (face.info.date || "") + "　" + (face.info.weekday || "")
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: face.th ? face.th.fsBodyLg : 15
        }

        // 周次徽标：accent 0.12 底 + 0.40 描边（与 IslandChannelChip 同一套观感，
        // 但这里字更大 —— 640×320 的面里 12px 药丸会小得像脚注）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: (face.info.week || "") !== ""
            implicitWidth: weekText.implicitWidth + 22
            implicitHeight: 26
            radius: 13
            color: face.th ? Qt.rgba(face.th.accent.r, face.th.accent.g,
                                     face.th.accent.b, 0.12) : Qt.rgba(0.4, 0.3, 0.6, 0.12)
            border.width: 1
            border.color: face.th ? Qt.rgba(face.th.accent.r, face.th.accent.g,
                                            face.th.accent.b, 0.40) : Qt.rgba(0.4, 0.3, 0.6, 0.40)

            Text {
                id: weekText
                anchors.centerIn: parent
                text: face.info.week || ""
                color: face.th ? face.th.accent : "#D0BCFF"
                font.pixelSize: face.th ? face.th.fsBadge : 14
                font.weight: Font.Medium
            }
        }
    }
}
