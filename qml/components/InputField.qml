// 统一的单行输入框（2026-10-07 抽出）
//
// 为什么要有这一个文件：界面上的输入框以前每个调用点都只写 `color: th.fg`，
// **没有一个设 background**。于是输入框沿用 Qt 自带的系统调色板 —— 在 Windows 上
// 那是白底，而 th.fg 在暗色外观下是 #FAFAFA（近白）。白底白字 ⇒ 打进去的字一个都
// 看不见，用户看到的就是"这框没法输入内容"。亮色外观下反过来：深字配深底，一样看不见。
//
// 控件级的样子不该在每个调用点各写一遍（写一遍漏一遍，漏的那遍就是 bug），
// 统一在这里给：底色透明（面板本身就是底，再叠一层灰就是"两层灰叠一起"）、
// 描边、占位色、聚焦时描边加深。

import QtQuick
import QtQuick.Controls

TextField {
    id: fld

    // 主题对象（Main.qml 的 root.th / CommandPalette 的 pal.theme）。
    // 不传也能用：退回一组暗色安全值，不至于变成"没颜色"。
    property var th: null

    readonly property color colFg:    th ? (th.fg     || "#FAFAFA") : "#FAFAFA"
    readonly property color colPh:    th ? (th.fg4    || "#3A3A3A") : "#3A3A3A"
    readonly property color colLine:  th ? (th.stroke || "#242424") : "#242424"
    readonly property color colLine2: th ? (th.fg3    || "#5A5A5A") : "#5A5A5A"
    readonly property real  rad:      th ? (th.rCtrl  || 8)         : 8

    color: colFg
    font.pixelSize: 12
    leftPadding: 10
    rightPadding: 8
    verticalAlignment: Text.AlignVCenter
    selectByMouse: true
    placeholderTextColor: colPh

    background: Rectangle {
        radius: fld.rad
        color: "transparent"
        border.color: fld.activeFocus ? fld.colLine2 : fld.colLine
        border.width: 1
    }
}
