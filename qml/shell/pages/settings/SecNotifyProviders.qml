// 被控端 · 设置页「通知提供者」卡片（2026-11-01 · 灵动岛顶替 ClassIsland 第 2 步）
//
// ── 这张卡解决什么 ─────────────────────────────────────────────────────
// 老师想"只关掉插件那堆提示"时以前无处可关：岛上只有 kind 字符串（popup/island/…），
// 同一个 kind 既可能是管理端发的、也可能是课表自己响的。这里把**来源**摆出来：
// 一行一个提供方（管理端 / 课程提醒 / 对讲 / 文件 / U盘助手 / 自动化 / 插件 / 考试），
// 每个提供方下面挂它的渠道。关掉的提供方**不弹岛、不响、也不进历史**。
//
// ── 优先级 ────────────────────────────────────────────────────────────
// 上移 = 更优先（更能抢到岛）。默认序由用户在 2026-11-01 裁决：
//   管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件 > 考试
//
// ── QML 零逻辑（铁律）───────────────────────────────────────────────
// name/subtitle/enabled/canUp/canDown/渠道列表全部由 C++ 算好（agentUi.notifyProviders）；
// 本件只画 + 转发点击。**不写**三元配色、不拼字符串、不算序号。
// 改动后由 C++ 发 notifyProvidersChanged 重画 —— 不做乐观更新（避免"勾了但没生效"）。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

AppCard {
    id: card

    // 与本节其它卡片同款定位（父是 SecNotify 的 Column）
    anchors.left: parent.left
    anchors.right: parent.right

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        AppCardTitle { text: "通知提供者" }

        Text {
            Layout.fillWidth: true
            text: "关掉的来源不弹岛、不响、也不进历史。上移 = 更优先，更容易抢到岛。"
            color: Theme.color.textSecondary
            font.pixelSize: 12
            lineHeight: 1.5
            wrapMode: Text.Wrap
        }

        // 一行一提供方：开关 + 上移/下移 + 它的渠道
        Repeater {
            model: agentUi.notifyProviders

            ColumnLayout {
                required property var modelData
                readonly property string providerId: modelData.id
                Layout.fillWidth: true
                spacing: 6

                AppSwitchRow {
                    Layout.fillWidth: true
                    text: modelData.name
                    supporting: modelData.subtitle
                    checked: modelData.enabled
                    onToggled: function (v) { agentUi.setNotifyProviderEnabled(modelData.id, v) }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    // 上移/下移：首/末行只画该有的那枚（visible 绑 C++ 的 canUp/canDown）
                    AppButton {
                        text: "上移"
                        size: "sm"
                        variant: "secondary"
                        visible: modelData.canUp
                        onClicked: agentUi.moveNotifyProvider(modelData.id, -1)
                    }
                    AppButton {
                        text: "下移"
                        size: "sm"
                        variant: "secondary"
                        visible: modelData.canDown
                        onClicked: agentUi.moveNotifyProvider(modelData.id, 1)
                    }
                    Item { Layout.fillWidth: true }
                }

                // 渠道：关掉单个渠道只拦那一类（如只关「下课」、保留「上课」）
                Repeater {
                    model: modelData.channels

                    AppSwitchRow {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.name
                        supporting: modelData.desc
                        checked: modelData.enabled
                        onToggled: function (v) {
                            agentUi.setNotifyChannelEnabled(providerId, modelData.id, v)
                        }
                    }
                }
            }
        }

        AppButton {
            text: "恢复默认顺序与开关"
            size: "sm"
            variant: "secondary"
            onClicked: agentUi.resetNotifyProviders()
        }
    }
}
