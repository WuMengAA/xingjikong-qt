import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/theme"

// 被控端 · 插件市场页（openPluginStore 的真实迁移 · U5-A3 第二批）
// 列表 = 本地快照(g_pluginHost) ∪ 云端商店(STE_PLUGIN_REGISTRY /api/plugins)，
// 判据与状态文案照 Widgets 版；安装 = 下载 zip → 解压 → installAndLaunch（C++ 侧执行）。
//
// 2026-10-30 用户点单「全部换新」：
//     卡内一律改 Layout 附加属性，`height` 必须写 `Layout.preferredHeight`（Layout 忽略 height）；
//   · `Row { topPadding }` → `RowLayout { Layout.topMargin }`（Row 没有 padding 属性）。
Item {
    id: page
    anchors.fill: parent

    property var ctx

    Component.onCompleted: agentUi.pluginsRefresh()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.top: parent.top
            anchors.topMargin: 16
            spacing: 12

            Row {
                spacing: 10
                AppButton { text: "刷新"; variant: "solid"; onClicked: agentUi.pluginsRefresh() }
                AppButton { text: "安装本地示例(echo)"; variant: "secondary"; onClicked: agentUi.pluginInstallSample() }
            }

            // ── 内置模块（2026-10-16「模块化插件」）──
            // 编译进 agent 的官方能力（U盘助手 / VoiceHub 联动 / 灵动岛多界面…）。
            // 与进程外插件的区别：够得到原生窗口消息与岛本体；代价是不可卸载，只能启停。
            Text {
                text: "内置模块"
                font.pixelSize: 16; font.weight: Font.Medium
                color: Theme.color.textPrimary
            }
            Repeater {
                model: agentUi.modules
                delegate: AppCard {
                    required property var modelData
                    anchors.left: parent ? parent.left : undefined
                    anchors.right: parent ? parent.right : undefined

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + "  v" + modelData.version
                                font.pixelSize: 14; font.weight: Font.Medium
                                color: Theme.color.textPrimary
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.running ? "运行中" : "已停用"
                                font.pixelSize: 12
                                color: modelData.running ? Theme.color.success
                                                         : Theme.color.textSecondary
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.description !== ""
                            text: modelData.description
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            color: Theme.color.textSecondary
                        }
                        RowLayout {
                            Layout.topMargin: 6
                            spacing: 10
                            AppButton {
                                text: modelData.enabled ? "停用" : "启用"
                                variant: "secondary"
                                height: 30
                                onClicked: agentUi.moduleEnable(modelData.id, !modelData.enabled)
                            }
                        }
                    }
                }
            }

            Text {
                text: "插件市场"
                font.pixelSize: 16; font.weight: Font.Medium
                color: Theme.color.textPrimary
            }
            Text {
                visible: agentUi.plugins.length === 0
                text: "商店拉取中，或还没有任何插件。\n点「刷新」重试（离线时仍会显示本机已装的插件）。"
                font.pixelSize: 13
                color: Theme.color.textSecondary
            }

            Repeater {
                model: agentUi.plugins
                delegate: AppCard {
                    required property var modelData
                    anchors.left: parent ? parent.left : undefined
                    anchors.right: parent ? parent.right : undefined
                    variant: "muted"

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + "  v" + modelData.version
                                font.pixelSize: 14; font.weight: Font.Medium
                                color: Theme.color.textPrimary
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.status
                                font.pixelSize: 12
                                color: modelData.installed ? Theme.color.success
                                                           : Theme.color.textSecondary
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.author !== ""
                            text: "作者：" + modelData.author
                            font.pixelSize: 12
                            color: Theme.color.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.description !== ""
                            text: modelData.description
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            color: Theme.color.textSecondary
                        }
                        RowLayout {
                            Layout.topMargin: 6
                            spacing: 10
                            AppButton {
                                text: modelData.installed ? (modelData.enabled ? "停用" : "启用") : "安装"
                                variant: "secondary"
                                height: 30
                                onClicked: modelData.installed
                                           ? agentUi.pluginEnable(modelData.id, !modelData.enabled)
                                           : agentUi.pluginInstall(modelData.id)
                            }
                            AppButton {
                                text: "卸载"
                                variant: "ghost"
                                height: 30
                                visible: modelData.installed
                                onClicked: agentUi.pluginUninstall(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
