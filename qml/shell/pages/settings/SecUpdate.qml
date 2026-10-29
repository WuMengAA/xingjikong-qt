// 被控端 · 设置页分类「更新」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 767-869 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
//   · 渠道按钮的选中态仍由 C++ 定（`channelOptionList` 的 variant 字段），
//     但词汇由 ash 的 "filled/tonal" 改为 AppButton 的 "solid/secondary"（同步改 C++）。
//   · 「临时」角标的两个 Hex（#F59E0B / #3B2600）→ `Theme.color.warning` / `Theme.color.background`。
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

// ─────────────────────────────────────────────────────
// ⑤ 更新：自动升级台账 + 渠道（含临时切换）
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "自动升级" }
            AppInfoRow {
                label: "升级方式"
                value: agentUi.otaModeText
                valueColor: agentUi.otaModeColor
            }
            AppInfoRow {
                label: "自动升级"
                value: agentUi.otaAutoText
                valueColor: agentUi.otaAutoColor
            }
            AppInfoRow { label: "上次自动升级"; value: agentUi.otaLastText }
            AppInfoRow { label: "下次自动升级"; value: agentUi.otaNextText }
            RowLayout {
                Layout.topMargin: 10
                spacing: 10
                AppButton {
                    text: "立即升级"
                    variant: "solid"
                    enabled: agentUi.canUpgrade
                    onClicked: agentUi.upgradeNow()
                }
                AppButton {
                    // 文案随状态变，值由 C++ 给（pauseBtnText）
                    text: agentUi.pauseBtnText
                    variant: "secondary"
                    onClicked: agentUi.toggleOtaPause()
                }
                AppButton {
                    text: "重新检查"
                    variant: "ghost"
                    onClicked: agentUi.refresh()
                }
            }
        }
    }

    // 更新渠道（临时测试开关 · 2026-10-28 B4）
    // 用户点单原文：「被控端设置界面添加临时的测试更新渠道切换按钮」+「切换逻辑在 C++」。
    // 生效面：g_channel（register 上报 + OTA 查询）+ channel.txt（重启/升级后仍生效）。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                spacing: 8
                Text {
                    text: "更新渠道"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                // 「临时」角标：让现场一眼看出这不是长期能力（服务端灰度上线后撤）
                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 19
                    radius: 9.5
                    color: Theme.color.warning
                    Text {
                        anchors.centerIn: parent
                        text: "临时"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: Theme.color.background
                    }
                }
                Item { Layout.fillWidth: true }
            }
            AppInfoRow { label: "当前渠道"; value: agentUi.updateChannel }
            RowLayout {
                spacing: 8
                Repeater {
                    model: agentUi.channelOptions
                    AppButton {
                        text: modelData.label
                        variant: modelData.variant
                        onClicked: agentUi.updateChannel = modelData.key
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: agentUi.channelNoteText
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }
}
