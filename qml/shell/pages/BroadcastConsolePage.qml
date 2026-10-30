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
//
// 2026-10-30 用户点单「全部换新」：
//   · `tok`（ctx.theme / Md3）→ 共享库 `Theme`；`import md3 as M` 本件无引用，删除；
//   · 两个入口按钮原来是品牌蓝 `#2F6BFF` / `#1F57E8` 硬编码 + 浅色 Hex 兜底 ——
//     全页按规范改为中性：主入口 `Theme.accentStrong`（被控端已置中性反白），
//     次入口 `Theme.color.surface`，hover 用一层低透明度覆盖（不再写死第二个蓝）；
//   · 次入口的 `border.width/border.color` 删除（去边框规范，靠底色差分界）。
import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/theme"
import "../"   // 2026-10-30：排期中心（BroadcastScheduleSheet）已嵌入本页，引用 qml/shell/ 下的兄弟组件

Item {
    id: page
    anchors.fill: parent

    property var ctx
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
            // 档② 换布局（2026-10-30）：AppCard 默认容器是 ColumnLayout ⇒ 卡内改用 Layout 附加属性。
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "广播端"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: Theme.color.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: station.length > 0
                              ? "归属广播站：" + station
                              : "还没绑定广播站 —— 打开「设置」里的配置，或重新走一遍首次配置"
                        font.pixelSize: 13
                        color: Theme.color.textSecondary
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (typeof agentUi !== "undefined" ? agentUi.connState : "")
                        font.pixelSize: 12
                        color: (typeof agentUi !== "undefined" && agentUi.connState.indexOf("已连接") === 0)
                               ? Theme.color.success
                               : Theme.color.warning
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ── 常用入口：只放广播端真正会用的两个 ─────────────────
            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 12

                // 主入口：中性反白底（被控端禁用品牌色）
                Rectangle {
                    id: primaryEntry
                    width: (parent.width - 12) / 2
                    height: 56
                    radius: Theme.radius.md
                    color: Theme.accentStrong

                    // hover 反馈：独立覆盖层改透明度（不新增第二个色值）
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.accentStrongForeground
                        opacity: ma1.containsMouse ? 0.08 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "打开广播社"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: Theme.accentStrongForeground
                    }
                    MouseArea {
                        id: ma1
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.goPage("broadcastclub")
                    }
                }

                // 次入口：卡片底 + 无边框（靠色差与主入口区分）
                Rectangle {
                    width: (parent.width - 12) / 2
                    height: 56
                    radius: Theme.radius.md
                    color: Theme.color.surface

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.color.textPrimary
                        opacity: ma2.containsMouse ? 0.06 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "播放时段"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: Theme.color.textPrimary
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

            // ── 排期中心（2026-10-30 用户点单「移动到广播里面」：从浮层抽屉改为内联面板）──
            // 高度固定 600、内部按 Tab 各自滚动（与抽屉时代一致）；数据来自全局 `voicehub`。
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 600
                radius: Theme.radius.lg
                color: Theme.color.surface
                clip: true
                BroadcastScheduleSheet {
                    anchors.fill: parent
                }
            }

            // ── 说明：把"为什么没有班级画面"讲清楚，避免现场当成故障 ──
            Text {
                width: parent.width
                text: "广播端不参与班级画面与设备网格：它不抓屏、不推缩略图，也不收班级黑板卡。"
                      + "要改成教室端，由管理员在设备列表里改用途，本机重连后自动切换。"
                font.pixelSize: 12
                lineHeight: 1.5
                color: Theme.color.textSecondary
                wrapMode: Text.Wrap
            }

            Text {
                visible: typeof agentUi !== "undefined" && agentUi.appVersion.length > 0
                text: "版本 " + (typeof agentUi !== "undefined" ? agentUi.appVersion : "")
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }
        }
    }
}
