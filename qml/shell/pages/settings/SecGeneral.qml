// 被控端 · 设置页分类「通用 · 设备与启动」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 201-258 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 2026-10-28 第二批（用户裁决「MD3 只用于管理端」）：
//   本件从 Md3Card/Md3SwitchRow 换成 ash/ 的 shadcn 组件（AshCard/AshSwitchRow）。
//   这是**样板件** —— 其余八个分类等这件的视觉确认后再铺。
//   令牌由页面根的 AshTheme 实例经 `ash` 属性注入（QML 侧不 new、不判断主题）。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。
import QtQuick

import "../../../ash"

Column {
    id: root

    property var ctx
    property var vh
    /** shadcn 令牌（AshTheme 实例，页面根注入） */
    property var ash

    width: parent ? parent.width : 0
    spacing: 12   // shadcn gap-3

    AshCard {
        th: root.ash
        anchors.left: parent.left
        anchors.right: parent.right

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0

            AshCardTitle { th: root.ash; text2: "这台设备" }
            AshInfoRow { th: root.ash; label: "当前版本"; value: agentUi.versionText }
            AshInfoRow {
                th: root.ash
                label: "升级状态"
                value: agentUi.latestText
                vColor: agentUi.latestColor
            }
            AshInfoRow { th: root.ash; label: "设备代号"; value: agentUi.uidText }
            AshInfoRow { th: root.ash; label: "用途";     value: agentUi.deviceRoleText }
            AshInfoRow { th: root.ash; label: "归属码";   value: agentUi.classCodeText }
            AshInfoRow { th: root.ash; label: "云端地址"; value: agentUi.cloudUrl }
            AshInfoRow {
                th: root.ash
                label: "连接状态"
                value: agentUi.connState
                vColor: agentUi.connStateColor
            }
            AshInfoRow {
                th: root.ash
                visible: agentUi.updateNotes !== ""
                label: "版本说明"
                value: agentUi.updateNotes
            }
        }
    }

    AshCard {
        th: root.ash
        anchors.left: parent.left
        anchors.right: parent.right

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 8

            AshCardTitle { th: root.ash; text2: "启动" }
            // 提权类（UAC 失败时 C++ 复查回弹，界面不会假勾上）
            AshSwitchRow {
                th: root.ash
                anchors.left: parent.left
                anchors.right: parent.right
                text: "开机自动启动（登录时拉起）"
                supporting: "改动需管理员权限（弹 UAC）"
                checked: agentUi.autostart
                onToggled: function (v) { agentUi.autostart = v }
            }
            AshSwitchRow {
                th: root.ash
                anchors.left: parent.left
                anchors.right: parent.right
                text: "在公共桌面放快捷方式"
                supporting: "所有登录用户可见；改动需管理员权限"
                checked: agentUi.desktopShortcut
                onToggled: function (v) { agentUi.desktopShortcut = v }
            }
        }
    }
}
