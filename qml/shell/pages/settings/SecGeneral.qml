// 被控端 · 设置页分类「通用 · 设备与启动」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 201-258 行）
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
// ① 通用：这台设备是谁、连着谁；启动项；三个目录（托盘移植）
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
            SecCardTitle { text2: "这台设备" }
            SecInfoRow { label: "当前版本"; value: agentUi.versionText }
            SecInfoRow {
                label: "升级状态"
                value: agentUi.latestText
                vColor: agentUi.latestColor
            }
            SecInfoRow { label: "设备代号"; value: agentUi.uidText }
            SecInfoRow { label: "用途";     value: agentUi.deviceRoleText }
            SecInfoRow { label: "归属码";   value: agentUi.classCodeText }
            SecInfoRow { label: "云端地址"; value: agentUi.cloudUrl }
            SecInfoRow {
                label: "连接状态"
                value: agentUi.connState
                vColor: agentUi.connStateColor
            }
            SecInfoRow { visible: agentUi.updateNotes !== ""; label: "版本说明"; value: agentUi.updateNotes }
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
            spacing: 0
            SecCardTitle { text2: "启动" }
            // 提权类（UAC 失败时 C++ 复查回弹，界面不会假勾上）
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "开机自动启动（登录时拉起）"
                supporting: "改动需管理员权限（弹 UAC）"
                checked: agentUi.autostart
                onToggled: function (v) { agentUi.autostart = v }
            }
            M.Md3SwitchRow {
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
