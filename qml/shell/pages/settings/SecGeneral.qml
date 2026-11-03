// 被控端 · 设置页分类「通用 · 设备与启动」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 201-258 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 2026-10-30 用户点单「全部换新」：本件从 ash/ 私有体系迁到共享库 App* 体系 ——
//   AshCard → AppCard、AshCardTitle → AppCardTitle、AshInfoRow → AppInfoRow、
//   AshSwitchRow → AppSwitchRow；令牌改读共享库 `Theme` 单例，
//   **不再需要页面注入 `ash`**（AgentSettingsPage 里的 AshTheme 实例已一并删除）。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh

    width: parent ? parent.width : 0
    spacing: 12   // shadcn gap-3

    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            AppCardTitle { text: "这台设备" }
            AppInfoRow { label: "当前版本"; value: agentUi.versionText }
            AppInfoRow {
                label: "升级状态"
                value: agentUi.latestText
                valueColor: agentUi.latestColor
            }
            AppInfoRow { label: "设备代号"; value: agentUi.uidText }
            AppInfoRow { label: "用途";     value: agentUi.deviceRoleText }
            AppInfoRow { label: "归属码";   value: agentUi.classCodeText }
            AppInfoRow { label: "云端地址"; value: agentUi.cloudUrl }
            AppInfoRow {
                label: "连接状态"
                value: agentUi.connState
                valueColor: agentUi.connStateColor
            }
            AppInfoRow {
                visible: agentUi.updateNotes !== ""
                label: "版本说明"
                value: agentUi.updateNotes
            }
        }
    }

    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            AppCardTitle { text: "启动" }
            // 提权类（UAC 失败时 C++ 复查回弹，界面不会假勾上）
            AppSwitchRow {
                text: "开机自动启动（登录时拉起）"
                supporting: "改动需管理员权限（弹 UAC）"
                checked: agentUi.autostart
                onToggled: function (v) { agentUi.autostart = v }
            }
            AppSwitchRow {
                text: "在公共桌面放快捷方式"
                supporting: "所有登录用户可见；改动需管理员权限"
                checked: agentUi.desktopShortcut
                onToggled: function (v) { agentUi.desktopShortcut = v }
            }
        }
    }

    // ── 退出保护（2026-11-03 · 用户笔记序号 2）────────────────────────────
    // 防误退/防学生乱退：设了口令后，从托盘「退出被控端」要输口令。
    // 边界（写在这里避免以后有人当安全机制）：只挡这一条路 —— 关机/注销/
    // 任务管理器结束进程/OTA 自重启都不经过它。防手滑，不防破解。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            AppCardTitle { text: "退出保护" }
            AppInfoRow { label: "当前状态"; value: agentUi.quitPasswordStateText }

            AppInput {
                id: quitPw
                Layout.fillWidth: true
                placeholder: "输入新口令（留空并点「清除」＝取消口令）"
                echoMode: TextInput.Password
            }

            RowLayout {
                spacing: 8
                AppButton {
                    text: "保存口令"
                    variant: "solid"
                    onClicked: agentUi.setQuitPassword(quitPw.text)
                }
                AppButton {
                    text: "清除"
                    variant: "secondary"
                    onClicked: agentUi.setQuitPassword("")
                }
                Item { Layout.fillWidth: true }
            }

            AppInfoRow {
                visible: agentUi.actionResultVisible
                label: ""
                value: agentUi.actionResultText
                wrapValue: true
            }
        }
    }
}
