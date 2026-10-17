import QtQuick 2.15
import "."

// 右下角迷你角标（2026-10-17 · #330 形态多样化 · Mini）
//
// ── 为什么要有这一档 ────────────────────────────────────────────
//   「常驻轻状态」（传输中 / 在播 / 静默计时）不值得占用屏幕**顶部正中**
//   —— 那里是师生最常看的位置，占用它就等于用低价值信息换注意力。
//   但又不能完全看不见。角标放在右下角，贴着任务栏上沿，只留 96~112px。
//
// ── 两种外观档（按有没有描述自动切，真源在 C++）──────────────────
//   无描述 → **正圆** 96×96（radius = 半边）：纯图标状态，收成圆点更像"角标"
//   有描述 → **圆角方牌** 112×112（radius 28）：放得下 2 行标题 + 1 行描述
//   ⚠️ 圆/方判定必须与 C++ 的 `m_desc.isEmpty()` 与 root._miniOrb 三处同口径，
//      否则出现"窗口是圆的、底色是方牌"这种一眼假。
//
// ── 版式 ────────────────────────────────────────────────────────
//   图标居中偏上（30/32）→ 标题 12px Medium（正圆 1 行 / 方牌 2 行，居中）→ 描述 10px
//   全 elide：角标面积小，宁可省略也不能溢出到窗外。
//
// ── 交互 ────────────────────────────────────────────────────────
//   默认**鼠标穿透**（IslandOverlay 的 m_interactive 默认 false）：角标常驻在
//   右下角，若默认吃鼠标，老师点任务栏/桌面右下角会被它吞掉。
//   需要"点角标看详情"的调用方自己 `setInteractive(true)`，此时点击会走
//   setExpanded(true) 的**升格**分支（角标 → 顶部胶囊展开，见 island.cpp）。
Item {
    id: miniFace

    // ⚠️ 显式传依赖（隐式作用域不会跟着搬进独立文件）
    property var    th: null
    property string icon: ""
    property string title: ""
    property string desc: ""
    readonly property bool hasDesc: miniFace.desc !== ""
    readonly property bool orb: !miniFace.hasDesc

    SvgIcon {
        id: mIcon
        name: miniFace.icon
        tint: miniFace.th ? miniFace.th.fg : "#FAFAFA"
        px: miniFace.hasDesc ? 32 : 30
        anchors.horizontalCenter: parent.horizontalCenter
        // 正圆档：图标整体偏上 8px，给下面那行标题留位（视觉重心不下坠）
        y: miniFace.hasDesc ? 18 : Math.round((miniFace.height - height) / 2 - 8)
    }

    Text {
        id: mTitle
        anchors.top: mIcon.visible ? mIcon.bottom : parent.top
        anchors.topMargin: mIcon.visible ? (miniFace.hasDesc ? 6 : 4) : 10
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, miniFace.width - 16)
        horizontalAlignment: Text.AlignHCenter
        text: miniFace.title
        color: miniFace.th ? miniFace.th.fg : "#FAFAFA"
        font.pixelSize: 12
        font.weight: Font.Medium
        wrapMode: Text.WordWrap
        elide: Text.ElideRight
        // 方牌给 2 行（信息量换面积）；正圆只留 1 行（圆里两行会顶到边缘）
        maximumLineCount: miniFace.hasDesc ? 2 : 1
        visible: text !== ""
    }

    Text {
        id: mDesc
        visible: miniFace.hasDesc && text !== ""
        anchors.top: mTitle.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, miniFace.width - 16)
        horizontalAlignment: Text.AlignHCenter
        text: miniFace.desc
        color: miniFace.th ? miniFace.th.sub : "#C8C8C8"
        font.pixelSize: 10
        elide: Text.ElideRight
        maximumLineCount: 1
    }
}
