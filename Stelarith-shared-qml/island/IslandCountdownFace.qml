import QtQuick 2.15
import "."

// 灵动岛 · 组件面板「倒计时」（2026-10-27 · G6）
//
// ── 目标时刻谁定 ────────────────────────────────────────────────
//   ① 管理端显式给 to（"HH:MM" 或 ISO8601）—— 老师想盯"距 12:30"这种死时刻；
//   ② 没给 to ⇒ C++ 用课表边界：正在上课 → 距下课；否则 → 距下一节上课。
//      （与 ClassIsland 倒计时组件同语义：绑"当前/下一节"，不是拍脑袋定时长。）
//
// ── 剩余量怎么算（唯一正确口径）─────────────────────────────────
//   remaining = targetMs − (nowMs + 本面打开后经过的墙钟)
//   nowMs 是 C++ 的**判定时间**（系统钟 + NTP 差 + 人工偏移）。绝不改读系统钟：
//   学校铃声整体偏移 3 分钟时，倒计时必须跟着偏移后的"上课时刻"走。
//
// ── 到点之后 ────────────────────────────────────────────────────
//   显示「时间到」并停在 00:00 —— **不自动关闭面板**。自动关会让人怀疑
//   "是不是没到点就没了"；该由上层按 durationSec / close 指令收。
Item {
    id: face

    property var th: null
    property var info: ({})

    property int tick: 0
    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: face.tick++
    }

    property double wallAtOpen: Date.now()

    // 剩余毫秒（<=0 = 已到）
    readonly property double leftMs: {
        face.tick                                  // ← 绑定依赖
        const target = Number(face.info.targetMs || 0)
        if (target <= 0) return 0
        const baseNow = Number(face.info.nowMs || 0)
        const now = baseNow > 0 ? (baseNow + (Date.now() - face.wallAtOpen))
                                : (Date.now() + Number(face.info.skewMs || 0))
        return target - now
    }

    readonly property bool arrived: face.leftMs <= 0

    function two(n) { return (n < 10 ? "0" : "") + n }
    function clockText(ms) {
        if (ms < 0) ms = 0
        const total = Math.floor(ms / 1000)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        // ≥1 小时才带小时位：显示 "1:02:33" 而不是 "01:02:33"（小时位不需要补零）
        return h > 0 ? (h + ":" + face.two(m) + ":" + face.two(s))
                     : (face.two(m) + ":" + face.two(s))
    }

    Column {
        anchors.centerIn: parent
        spacing: 4

        // 标签（「距离下课」/「距离上课」/ 自定义 title）
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: face.info.label || ""
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: face.th ? face.th.fsBodyLg : 15
        }

        // 大数字：等宽感靠 Medium + 大字号，不引外部字体（字体库无中文，
        // 且数字走系统默认字体就够等宽观感）
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // 2026-11-03：「未配置」（C++ 侧 ok=false，error 已透传进 info）不能画成
            // 「时间到」—— 00:00 停格会被当成"正好到点"，是最坏的一种错。两者必须可区分。
            text: face.arrived ? (face.info.error || "时间到") : face.clockText(face.leftMs)
            color: face.arrived
                   ? (face.th ? face.th.accent : "#D0BCFF")
                   : (face.th ? face.th.fg : "#FAFAFA")
            font.pixelSize: face.arrived ? 30 : 30   // 2026-11-03（m14970 ②）用户点单默认 30px，到点/倒计时同字号
            font.weight: Font.Medium
        }

        // 目标时刻：note（"08:00 上课"）优先，否则用 targetText 拼
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (face.info.note) return face.info.note
                const t = face.info.targetText || ""
                if (t === "") return ""
                const d = face.info.targetDay || ""
                return d === "" ? ("到 " + t) : (d + " " + t)
            }
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: face.th ? face.th.fsNote : 12
        }
    }
}
