// 被控端 · 设置页分类「通用 · 服务与时间」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 260-371 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
//   · AshInput 的 `helper`（输入框下方提示）在 AppInput 里没有对应属性 ⇒
//     这一段提示改成紧跟其后的独立 Text（12 号次文字色），文案原样保留。
//   · 原 `Row { topPadding: 10 }` 无效（Row 无 padding）→ RowLayout + Layout.topMargin。
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

    // ── 蓝图「通用设置」第 2 项：url 配置（2026-10-28）──────────────
    // 原文：「开机自启、url 配置、安全模式（意外崩溃防护）、时间配置、
    //        存储与备份、高级选项。」开机自启在上卡，本卡起依次补齐。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            AppCardTitle { text: "云端与服务" }
            AppInfoRow { label: "当前地址"; value: agentUi.serverUrlText }
            AppInput {
                id: urlField
                Layout.fillWidth: true
                placeholder: agentUi.serverUrlPlaceholder
            }
            Text {
                Layout.fillWidth: true
                text: "只接受 ws:// 或 wss://。保存后立即重连，不用重启。"
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            RowLayout {
                spacing: 10
                AppButton {
                    text: "保存并重连"
                    variant: "solid"
                    onClicked: agentUi.applyServerUrl(urlField.text)
                }
                AppButton {
                    text: "重新读取状态"
                    variant: "ghost"
                    onClicked: agentUi.refresh()
                }
            }
        }
    }

    // ── 蓝图「通用设置」第 3 项：安全模式（意外崩溃防护）────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "安全模式（意外崩溃防护）" }
            AppInfoRow { label: "上次退出"; value: agentUi.lastExitText }
            AppInfoRow { label: "本次状态"; value: agentUi.safeModeStateText }
            AppSwitchRow {
                text: "异常退出后自动进入安全模式"
                supporting: "安全模式下不加载第三方插件 —— 一台被插件拖崩的设备重启就能直接上课；改完开关重启即恢复"
                checked: agentUi.safeModeAuto
                onToggled: function (v) { agentUi.safeModeAuto = v }
            }
        }
    }

    // ── 蓝图「通用设置」第 4 项：时间配置 ────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "时间" }
            AppInfoRow { label: "当前判定时间"; value: agentUi.timeNowText }
            AppInfoRow { label: "校时状态";     value: agentUi.timeStateText }
            AppInfoRow { label: "NTP 服务器";   value: agentUi.timeServerText }
            AppInfoRow { label: "本机偏差";     value: agentUi.timeSkewText }
            AppInfoRow { label: "人工偏移";     value: agentUi.timeOffsetText }
            AppInfoRow { label: "上次成功校时"; value: agentUi.timeLastSyncText }
            AppSwitchRow {
                text: "自动校时"
                supporting: "启动后与每 30 分钟跟 NTP 对一次；关掉只保留人工偏移"
                checked: agentUi.timeAutoSync
                onToggled: function (v) { agentUi.timeAutoSync = v }
            }
            RowLayout {
                Layout.topMargin: 10
                spacing: 8
                AppButton {
                    text: "立即对时"
                    variant: "solid"
                    onClicked: agentUi.syncTimeNow()
                }
                AppButton {
                    text: "−15 分"
                    variant: "secondary"
                    onClicked: agentUi.adjustTimeOffset(-15)
                }
                AppButton {
                    text: "+15 分"
                    variant: "secondary"
                    onClicked: agentUi.adjustTimeOffset(15)
                }
                AppButton {
                    text: "归零"
                    variant: "ghost"
                    onClicked: agentUi.resetTimeOffset()
                }
            }
        }
    }
}
