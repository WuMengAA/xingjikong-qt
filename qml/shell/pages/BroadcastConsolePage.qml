// 被控端 · 广播台（2026-10-25）
//
// 这是「广播端」的独立首屏 —— 用途在 OOBE 第一步确定，之后这台机器启动就落在这里，
// 而不是落进教室端那套 6 页外壳。
//
// 为什么是**独立页**而不是给广播社页加个入口：
//   · 广播端的身份不是"能打开广播功能"，而是"它就是广播设备"；
//   · 教室端的页面（记录/自习/缩略图）对它无意义，混在一起会让现场分不清这台是谁。
//
// 数据全部来自 C++ 快照（agentUi），本页不自己发请求：
//   deviceRole / classCode（广播端里它是**站码**）/ connState / appVersion
import QtQuick
import "../../components"
import "../../components/md3" as M

Item {
    id: page
    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null
    readonly property var win: ctx ? ctx.window : null
    readonly property string station: (typeof agentUi !== "undefined" && agentUi.classCode) ? agentUi.classCode : ""

    // 跳到别的页：走 UiPageHost 给页面的 ctx 契约（goToId），不自己猜索引。
    function goPage(id) {
        if (ctx && ctx.goToId) ctx.goToId(id)
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 20
            spacing: 16

            // ── 身份卡：这台机器是什么 ─────────────────────────────
            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 10

                    Text {
                        text: "广播端"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: page.tok ? page.tok.cOnSurface : "#111827"
                    }
                    Text {
                        width: parent.width
                        text: station.length > 0
                              ? "归属广播站：" + station
                              : "还没绑定广播站 —— 打开「设置」里的配置，或重新走一遍首次配置"
                        font.pixelSize: 13
                        color: page.tok ? page.tok.cOnSurfaceVar : "#6B7280"
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        text: (typeof agentUi !== "undefined" ? agentUi.connState : "")
                        font.pixelSize: 12
                        color: (typeof agentUi !== "undefined" && agentUi.connState.indexOf("已连接") === 0)
                               ? (page.tok ? page.tok.cOk : "#15803D")
                               : (page.tok ? page.tok.cWarn : "#B45309")
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ── 常用入口：只放广播端真正会用的两个 ─────────────────
            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 12

                Rectangle {
                    width: (parent.width - 12) / 2
                    height: 56
                    radius: page.tok ? page.tok.rM : 12
                    color: ma1.containsMouse ? "#1F57E8" : "#2F6BFF"
                    Text {
                        anchors.centerIn: parent
                        text: "打开广播社"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#FFFFFF"
                    }
                    MouseArea {
                        id: ma1
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.goPage("broadcastclub")
                    }
                }

                Rectangle {
                    width: (parent.width - 12) / 2
                    height: 56
                    radius: page.tok ? page.tok.rM : 12
                    color: ma2.containsMouse
                           ? (page.tok ? page.tok.cContLow : "#F1F5F9")
                           : (page.tok ? page.tok.cCont : "#F8FAFC")
                    border.width: 1
                    border.color: page.tok ? page.tok.cOutlineVar : "#E5E7EB"
                    Text {
                        anchors.centerIn: parent
                        text: "播放时段"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#111827"
                    }
                    MouseArea {
                        id: ma2
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.goPage("playschedule")
                    }
                }
            }

            // ── 说明：把"为什么没有班级画面"讲清楚，避免现场当成故障 ──
            Text {
                width: parent.width
                text: "广播端不参与班级画面与设备网格：它不抓屏、不推缩略图，也不收班级黑板卡。"
                      + "要改成教室端，由管理员在设备列表里改用途，本机重连后自动切换。"
                font.pixelSize: 12
                lineHeight: 1.5
                color: page.tok ? page.tok.cOnSurfaceVar : "#6B7280"
                wrapMode: Text.Wrap
            }

            Text {
                visible: typeof agentUi !== "undefined" && agentUi.appVersion.length > 0
                text: "版本 " + (typeof agentUi !== "undefined" ? agentUi.appVersion : "")
                font.pixelSize: 12
                color: page.tok ? page.tok.cOnSurfaceVar : "#6B7280"
            }
        }
    }
}
