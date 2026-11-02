// 考试模式全屏布局模板（1280×800 · 被控端）
//
// 需求语境：#88 考试模式黑名单 + 全屏。界面三要素：谁在考（标题+倒计时）、
// 考得怎么样（统计+名单）、怎么出去（退出需监考密码 + 求助）。
import QtQuick
import "../components"
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton，2026-10-25；dark 由主外壳统一写）

Item {
    id: root
    width: 1280; height: 800
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    property bool dark: false
    property int remainSec: 2700        // 45:00
    property var seats: [
        { no: "01", name: "王同学", state: "答题中" },
        { no: "02", name: "李同学", state: "答题中" },
        { no: "03", name: "赵同学", state: "已交卷" },
        { no: "04", name: "陈同学", state: "举手求助" },
        { no: "05", name: "周同学", state: "答题中" },
        { no: "06", name: "吴同学", state: "答题中" },
        { no: "07", name: "郑同学", state: "离线" },
        { no: "08", name: "孙同学", state: "答题中" }
    ]

    function fmt(s) {
        var m = Math.floor(s / 60), sec = s % 60
        return (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
    }
    function seatColor(st) {
        if (st === "举手求助") return Theme.color.destructive
        if (st === "已交卷") return Theme.color.success
        if (st === "离线") return Theme.color.textSecondary
        return Theme.color.primary
    }

    Rectangle { anchors.fill: parent; color: Theme.color.surface }

    // 顶栏：考试名 + 倒计时
    Rectangle {
        anchors.top: parent.top
        width: parent.width; height: 96
        color: Theme.color.surfaceVariant
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.color.textSecondary }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 20
            Column {
                spacing: 2
                Text { text: "期中考试 · 数学"; font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.textPrimary }
                Text { text: "监考：张老师  ·  高一1班  ·  08:00 - 08:45"; font.pixelSize: 12; color: Theme.color.textSecondary }
            }
        }
        // 倒计时大数字
        Row {
            anchors.right: parent.right; anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Text { text: "⏱"; font.pixelSize: 24; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: root.fmt(root.remainSec)
                font.pixelSize: 45; font.weight: Font.Medium
                color: root.remainSec < 300 ? Theme.color.destructive : Theme.color.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // 统计行
    Row {
        anchors.top: parent.top; anchors.topMargin: 120
        anchors.left: parent.left; anchors.leftMargin: 32
        spacing: 16
        Repeater {
            model: [
                { k: "应到", v: "42" }, { k: "实到", v: "40" },
                { k: "已交卷", v: "12" }, { k: "求助", v: "1" }
            ]
            delegate: Rectangle {
                required property var modelData
                width: 200; height: 76
                radius: Theme.radius.md
                color: Theme.color.surface
                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Text { text: modelData.k; font.pixelSize: 14; color: Theme.color.textSecondary; anchors.horizontalCenter: parent.horizontalCenter }
                    Text { text: modelData.v; font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.textPrimary; anchors.horizontalCenter: parent.horizontalCenter }
                }
            }
        }
    }

    // 座位名单网格
    GridView {
        anchors.top: parent.top; anchors.topMargin: 216
        anchors.left: parent.left; anchors.leftMargin: 32
        anchors.right: parent.right; anchors.rightMargin: 32
        anchors.bottom: bottomBar.top; anchors.bottomMargin: 20
        clip: true
        model: root.seats
        cellWidth: 296; cellHeight: 120
        delegate: Rectangle {
            required property var modelData
            width: 280; height: 104
            radius: Theme.radius.md
            color: Theme.color.surface
            border.width: 0   // 无边框：求助态由底色/图标表达
            border.color: Theme.color.destructive
            Row {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: Theme.color.primary
                    Text { anchors.centerIn: parent; text: modelData.no; font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.primaryForeground }
                }
                Column {
                    spacing: 3
                    Text { text: modelData.name; font.pixelSize: 14; color: Theme.color.textPrimary }
                    Row {
                        spacing: 6
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.seatColor(modelData.state)
                        }
                        Text { text: modelData.state; font.pixelSize: 12; color: Theme.color.textSecondary }
                    }
                }
            }
        }
    }

    // 底栏：退出（需密码）+ 求助
    Rectangle {
        id: bottomBar
        anchors.bottom: parent.bottom
        width: parent.width; height: 88
        color: Theme.color.surfaceVariant
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.color.textSecondary }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            AppButton {
                text: "退出考试"
                variant: "secondary"
                // 模板占位：真实实现见被控端 exam_mode.cpp（监考密码校验 + 退出指令）
                onClicked: {}
            }
            Text { text: "退出需监考密码；异常情况点「求助」通知监考老师"; font.pixelSize: 12; color: Theme.color.textSecondary }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            AppButton {
                text: "🖐 求助"
                variant: "secondary"
                // 模板占位：真实实现见被控端求助上报指令（管理端收到后转告监考老师）
                onClicked: {}
            }
        }
    }
}
