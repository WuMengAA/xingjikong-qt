// 被控端 · 设置页分类「自动化」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 697-755 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick

import "../../../components/md3" as M

Column {
    id: root

    property var ctx
    property var vh
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ④ 自动化：播放时段（托盘移植）+ 定时规则（岛「自」面）
// ─────────────────────────────────────────────────────
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 2
            SecCardTitle { text2: "播放时段"; bottomPadding: 6 }
            SecInfoRow { label: "状态";   value: agentUi.playStateText }
            SecInfoRow { label: "时段";   value: agentUi.playWindowsText }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "启用播放时段"
                supporting: "只在设定时段内响应广播与媒体推送"
                checked: agentUi.playScheduleEnabled
                onToggled: function (v) { agentUi.playScheduleEnabled = v }
            }
            Row {
                topPadding: 10
                spacing: 10
                M.Md3Btn {
                    text: "打开时段设置"
                    variant: "filled"
                    onClicked: if (root.ctx) root.ctx.goToId("playschedule")
                }
                M.Md3Btn {
                    text: "保存当前时段"
                    variant: "tonal"
                    onClicked: agentUi.savePlaySchedule()
                }
            }
        }
    }

    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 8
            SecCardTitle { text2: "定时规则" }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "「到点自动做什么」的规则在**灵动岛的「自」面**里增删与启停 —— " +
                      "主入口就在老师手边，不用绕到这个设置页。规则可设为通知、调音量、" +
                      "静音、锁屏、截图或启动指定程序，全部复用既有指令白名单。"
                font.pixelSize: 11
                color: root.tok ? root.tok.cOnSurfaceVar : "#CAC4D0"
                wrapMode: Text.WordWrap
            }
        }
    }
}
