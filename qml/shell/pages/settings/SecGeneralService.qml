// 被控端 · 设置页分类「通用 · 服务与时间」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 260-371 行）
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

    // ── 蓝图「通用设置」第 2 项：url 配置（2026-10-28）──────────────
    // 原文：「开机自启、url 配置、安全模式（意外崩溃防护）、时间配置、
    //        存储与备份、高级选项。」开机自启在上卡，本卡起依次补齐。
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 10
            SecCardTitle { text2: "云端与服务" }
            SecInfoRow { label: "当前地址"; value: agentUi.serverUrlText }
            M.Md3TextField {
                id: urlField
                anchors.left: parent.left
                anchors.right: parent.right
                placeholder: agentUi.serverUrlPlaceholder
                helper: "只接受 ws:// 或 wss://。保存后立即重连，不用重启。"
            }
            Row {
                spacing: 10
                M.Md3Btn {
                    text: "保存并重连"
                    variant: "filled"
                    onClicked: agentUi.applyServerUrl(urlField.text)
                }
                M.Md3Btn {
                    text: "重新读取状态"
                    variant: "text"
                    onClicked: agentUi.refresh()
                }
            }
        }
    }

    // ── 蓝图「通用设置」第 3 项：安全模式（意外崩溃防护）────────────
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
            SecCardTitle { text2: "安全模式（意外崩溃防护）" }
            SecInfoRow { label: "上次退出"; value: agentUi.lastExitText }
            SecInfoRow { label: "本次状态"; value: agentUi.safeModeStateText }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "异常退出后自动进入安全模式"
                supporting: "安全模式下不加载第三方插件 —— 一台被插件拖崩的设备重启就能直接上课；改完开关重启即恢复"
                checked: agentUi.safeModeAuto
                onToggled: function (v) { agentUi.safeModeAuto = v }
            }
        }
    }

    // ── 蓝图「通用设置」第 4 项：时间配置 ────────────────────────
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
            SecCardTitle { text2: "时间" }
            SecInfoRow { label: "当前判定时间"; value: agentUi.timeNowText }
            SecInfoRow { label: "校时状态";     value: agentUi.timeStateText }
            SecInfoRow { label: "NTP 服务器";   value: agentUi.timeServerText }
            SecInfoRow { label: "本机偏差";     value: agentUi.timeSkewText }
            SecInfoRow { label: "人工偏移";     value: agentUi.timeOffsetText }
            SecInfoRow { label: "上次成功校时"; value: agentUi.timeLastSyncText }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "自动校时"
                supporting: "启动后与每 30 分钟跟 NTP 对一次；关掉只保留人工偏移"
                checked: agentUi.timeAutoSync
                onToggled: function (v) { agentUi.timeAutoSync = v }
            }
            Row {
                topPadding: 10
                spacing: 8
                M.Md3Btn {
                    text: "立即对时"
                    variant: "filled"
                    onClicked: agentUi.syncTimeNow()
                }
                M.Md3Btn {
                    text: "−15 分"
                    variant: "tonal"
                    onClicked: agentUi.adjustTimeOffset(-15)
                }
                M.Md3Btn {
                    text: "+15 分"
                    variant: "tonal"
                    onClicked: agentUi.adjustTimeOffset(15)
                }
                M.Md3Btn {
                    text: "归零"
                    variant: "text"
                    onClicked: agentUi.resetTimeOffset()
                }
            }
        }
    }
}
