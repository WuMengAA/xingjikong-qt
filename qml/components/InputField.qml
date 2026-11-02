//
// 为什么要有这一个文件：界面上的输入框以前每个调用点都只写 `color: th.fg`，
// **没有一个设 background**。于是输入框沿用 Qt 自带的系统调色板 —— 在 Windows 上
// 那是白底，而 th.fg 在暗色外观下是 #FAFAFA（近白）。白底白字 ⇒ 打进去的字一个都
// 看不见（#94 事故同根）。
//
// 2026-10-14：内部规格升到 Material Design 3（outlined 变体）：
//   透明底 + 1px outline 描边、聚焦升 2px primary、圆角 rXS(4)、
//   字 bodyM(14)/cOnSurface、占位 bodyM/cOnSurfaceVar。
// **调用方 API 一字未改**（th / placeholderText / 所有 TextField 原生属性）。
import QtQuick
import QtQuick.Controls

TextField {
    id: fld

    // 主题对象（Main.qml 的 root.th / CommandPalette 的 pal.theme）。
    property var th: null

    readonly property color colFg:    th ? (th.fg     || "#1C1B1F") : "#1C1B1F"
    readonly property color colPh:    th ? (th.ph     || "#49454F") : "#49454F"
    readonly property color colLine:  th ? (th.stroke2 || "#71717A") : "#71717A"
    readonly property color colLine2: th ? (th.acc    || "#18181B") : "#18181B"
    readonly property real  rad:      th ? (th.rXS !== undefined ? th.rXS : 4) : 4

    color: colFg
    font.pixelSize: th && th.bodyM !== undefined ? th.bodyM : 14
    leftPadding: 12
    rightPadding: 10
    verticalAlignment: Text.AlignVCenter
    selectByMouse: true
    placeholderTextColor: colPh

    background: Rectangle {
        radius: fld.rad
        color: "transparent"     // 面板本身就是底（两层灰叠一起更糊）
    }
}
