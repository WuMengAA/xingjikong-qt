// 考试模式全屏布局模板（1280×800 · 被控端）
//
// 需求语境：#88 考试模式黑名单 + 全屏。界面三要素：谁在考（标题+倒计时）、
// 考得怎么样（统计+名单）、怎么出去（退出需监考密码 + 求助）。
import QtQuick
import "../components"
import "../components/theme"   // Md3 令牌（pragma Singleton，2026-10-25；dark 由主外壳统一写）
import "../components/md3"

Item {
    id: root
    width: 1280; height: 800
    // MD3 令牌：内部自建实例（深/浅由 dark 驱动）。
    // ⚠️ 不许让宿主晚赋值替换整个 md3 实例——创建期 md3 为空会让 Behavior 时长
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    //    与手机端 mobile/ 页面同一模式：页面自建 Md3，宿主只传 dark 布尔。
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
        if (st === "举手求助") return Md3.cError
        if (st === "已交卷") return Md3.cOk
        if (st === "离线") return Md3.cOutline
        return Md3.cPrimary
    }

    Rectangle { anchors.fill: parent; color: Md3.cSurface }

    // 顶栏：考试名 + 倒计时
    Rectangle {
        anchors.top: parent.top
        width: parent.width; height: 96
        color: Md3.cCont
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Md3.cOutlineVar }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 20
            Column {
                spacing: 2
                Text { text: "期中考试 · 数学"; font.pixelSize: Md3.headlineS; font.weight: Font.Medium; color: Md3.cOnSurface }
                Text { text: "监考：张老师  ·  高一1班  ·  08:00 - 08:45"; font.pixelSize: Md3.bodyS; color: Md3.cOnSurfaceVar }
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
                font.pixelSize: Md3.displayM; font.weight: Font.Medium
                color: root.remainSec < 300 ? Md3.cError : Md3.cOnSurface
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
                radius: Md3.rM
                color: Md3.cContHigh
                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Text { text: modelData.k; font.pixelSize: Md3.labelM; color: Md3.cOnSurfaceVar; anchors.horizontalCenter: parent.horizontalCenter }
                    Text { text: modelData.v; font.pixelSize: Md3.headlineS; font.weight: Font.Medium; color: Md3.cOnSurface; anchors.horizontalCenter: parent.horizontalCenter }
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
            radius: Md3.rM
            color: Md3.cContHigh
            border.width: modelData.state === "举手求助" ? 2 : 0
            border.color: Md3.cError
            Row {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: Md3.cSecondaryCont
                    Text { anchors.centerIn: parent; text: modelData.no; font.pixelSize: Md3.titleM; font.weight: Font.Medium; color: Md3.cOnSecondaryCont }
                }
                Column {
                    spacing: 3
                    Text { text: modelData.name; font.pixelSize: Md3.bodyL; color: Md3.cOnSurface }
                    Row {
                        spacing: 6
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.seatColor(modelData.state)
                        }
                        Text { text: modelData.state; font.pixelSize: Md3.bodyS; color: Md3.cOnSurfaceVar }
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
        color: Md3.cCont
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Md3.cOutlineVar }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            Md3Btn {
                text: "退出考试"
                variant: "outlined"
                onClicked: {}
            }
            Text { text: "退出需监考密码；异常情况点「求助」通知监考老师"; font.pixelSize: Md3.bodyS; color: Md3.cOnSurfaceVar }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Md3Btn {
                text: "🖐 求助"
                variant: "tonal"
                onClicked: {}
            }
        }
    }
}
