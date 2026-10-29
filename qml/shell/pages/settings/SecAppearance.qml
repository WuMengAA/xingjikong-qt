// 被控端 · 设置页分类「主界面」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 522-574 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
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
// ② 主界面：主题 / 灵动岛形态 / 插件推岛
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "外观" }
            AppSwitchRow {
                text: "浅色主题"
                supporting: "即时生效；灵动岛与面板同时切换"
                checked: agentUi.lightMode
                onToggled: function (v) { agentUi.lightMode = v }
            }
        }
    }

    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "灵动岛" }
            AppSwitchRow {
                text: "空闲时常驻"
                supporting: "关掉后岛只在与老师交互时出现"
                checked: agentUi.islandIdle
                onToggled: function (v) { agentUi.islandIdle = v }
            }
            // P0-0（2026-10-17 · 第三方推岛治理）：
            // 插件经本地 WS 调 island_show / notify 时先问这个开关；
            // 关掉 = 插件推的提示一律静默丢弃（插件侧收到 dropped）。
            // ⚠️ 只管**插件**；管理端通知、本机课表/对讲/文件提示不受影响。
            AppSwitchRow {
                text: "允许第三方推送灵动岛"
                supporting: "插件推的通知带 [插件名] 前缀；限 5 条/秒、标题 120 字"
                checked: agentUi.pluginIslandPush
                onToggled: function (v) { agentUi.pluginIslandPush = v }
            }
        }
    }
}
