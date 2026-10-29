// 被控端 · 设置页分类「自动化」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 697-755 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
// 顺带修正：原 `Row { topPadding: 10 }` 无效（Row 无 padding 属性）→ RowLayout + Layout.topMargin。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ④ 自动化：播放时段（托盘移植）+ 定时规则（岛「自」面）
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "播放时段" }
            AppInfoRow { label: "状态";   value: agentUi.playStateText }
            AppInfoRow { label: "时段";   value: agentUi.playWindowsText }
            AppSwitchRow {
                text: "启用播放时段"
                supporting: "只在设定时段内响应广播与媒体推送"
                checked: agentUi.playScheduleEnabled
                onToggled: function (v) { agentUi.playScheduleEnabled = v }
            }
            RowLayout {
                Layout.topMargin: 10
                spacing: 10
                AppButton {
                    text: "打开时段设置"
                    variant: "solid"
                    onClicked: if (root.ctx) root.ctx.goToId("playschedule")
                }
                AppButton {
                    text: "保存当前时段"
                    variant: "secondary"
                    onClicked: agentUi.savePlaySchedule()
                }
            }
        }
    }

    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            AppCardTitle { text: "定时规则" }
            Text {
                Layout.fillWidth: true
                text: "「到点自动做什么」的规则在**灵动岛的「自」面**里增删与启停 —— " +
                      "主入口就在老师手边，不用绕到这个设置页。规则可设为通知、调音量、" +
                      "静音、锁屏、截图或启动指定程序，全部复用既有指令白名单。"
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }
}
