import QtQuick 2.15

// 灵动岛 · 渠道小标签（2026-10-27 · F 收尾第 2 项）
//
// ── 为什么需要它 ────────────────────────────────────────────
//   上下课提醒走独立渠道「课程提醒」（island.h 的 m_channel / setChannelForNext），
//   C++ 侧早就把渠道名交给桥了（IslandBridge::channel），但 QML 从来没画 ——
//   学生看到的只是一句"上课啦"，分不清这是**课表自己响的**还是**老师发的通知**。
//   这两种在教室里的处置完全不同（前者是日程，后者要服从），必须一眼可辨。
//
// ── 形态（为什么是"药丸标签"而不是"标题前缀"）────────────────
//   · 前缀会挤占标题（岛宽由 C++ 按内容算，前缀 ~60px 会把标题压成省略号）；
//   · 药丸是可复用的"徽标"语义，后面「值日」「考试」等渠道直接复用同一件；
//   · 用强调色描边+低透明底：它是**元信息**，不该跟标题抢主次（MD3 的 badge 口径）。
//
// ── 接口（显式传依赖：独立文件里没有隐式作用域）──────────────────
//   th   Is 岛主题令牌（IslandTheme 实例）
//   text 渠道名（空串 ⇒ 本组件整体不占位，调用方也就不用管显隐）

Item {
    id: chip

    property var    th: null
    property string text: ""

    // 药丸高度 18：收起态描述行只有 ~16px，18 会溢出 1px；按 16 收（与 12px 字高配平）
    readonly property int pillH: 16
    readonly property color tone: chip.th ? chip.th.accent : "#D0BCFF"

    implicitWidth: textItem.implicitWidth + 14
    implicitHeight: pillH
    visible: chip.text !== ""

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        // 12% 底 + 40% 描边：深浅两套主题下都够辨识，又不至于变成一块实心色块
        color: Qt.rgba(chip.tone.r, chip.tone.g, chip.tone.b, 0.12)
        border.width: 1
        border.color: Qt.rgba(chip.tone.r, chip.tone.g, chip.tone.b, 0.40)
    }

    Text {
        id: textItem
        anchors.centerIn: parent
        text: chip.text
        color: chip.tone
        // 字号 12：项目规范最小档（11 只用于时间那种"附属中的附属"，
        // 渠道名要读得出来，不能糊成一片）
        font.pixelSize: 12
        font.weight: Font.Medium
        // 不 elide：渠道名由服务端/课表给，长度可控（2–6 字）；真要更长由调用方截断
    }
}
