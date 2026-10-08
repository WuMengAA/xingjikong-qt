// 灵动岛时间/状态显示 · QML 版（2026-10-09 · 用户需求"灵动岛显示时间、状态"）
//
// 管理端 DynamicIsland 目前只有状态列表，没有时钟。本组件补齐 HH:mm 时间
// + 一行状态小字，供岛收起/展开态嵌入。
//
// 设计：
//   · 纯显示件：不自带手势/布局策略，父级（岛）决定摆哪、多大
//   · 500ms 刷新（跨分不延迟；不逐秒重绘）
//   · status 空 = 只显示时间；light 决定深/浅字色
//
// 用法（等 Main.qml/DynamicIsland.qml 工作区解禁后嵌）：
//   IslandClock { width: 60; height: 22; status: "空闲"; light: isl.theme.fg === "#FAFAFA" ? false : true }
import QtQuick

Item {
    id: root

    property string status: ""
    property bool light: false
    property int timePx: 22          // 时间字号（展开态调大）

    function nowText() {
        var d = new Date()
        function p(n) { return (n < 10 ? "0" : "") + n }
        return p(d.getHours()) + ":" + p(d.getMinutes())
    }

    // 时间大字
    Text {
        id: timeLabel
        text: root.nowText()
        color: root.light ? "#1E1E1E" : "#F5F5F5"
        font.pixelSize: root.timePx
        font.weight: Font.Medium
        anchors.left: parent.left
        anchors.top: parent.top
    }

    // 状态小字（可选）
    Text {
        id: statusLabel
        visible: root.status !== ""
        text: root.status
        color: root.light ? "#646464" : "#8C8C8C"
        font.pixelSize: Math.max(8, root.timePx / 2)
        anchors.left: parent.left
        anchors.top: timeLabel.bottom
        anchors.topMargin: 2
    }

    // 半秒刷新：跨分钟不延迟
    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: timeLabel.text = root.nowText()
    }
}