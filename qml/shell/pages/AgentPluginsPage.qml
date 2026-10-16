import QtQuick
import "../../components"
import "../../components/md3" as M

// 被控端 · 插件市场页（openPluginStore 的真实迁移 · U5-A3 第二批）
// 列表 = 本地快照(g_pluginHost) ∪ 云端商店(STE_PLUGIN_REGISTRY /api/plugins)，
// 判据与状态文案照 Widgets 版；安装 = 下载 zip → 解压 → installAndLaunch（C++ 侧执行）。
Item {
    id: page
    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null

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
            anchors.topMargin: 16
            spacing: 12

            Row {
                spacing: 10
                M.Md3Btn { md3: page.tok; text: "刷新"; variant: "filled"; onClicked: agentUi.pluginsRefresh() }
                M.Md3Btn { md3: page.tok; text: "安装本地示例(echo)"; variant: "tonal"; onClicked: agentUi.pluginInstallSample() }
            }

            // ── 内置模块（2026-10-16「模块化插件」）──
            // 编译进 agent 的官方能力（U盘助手 / VoiceHub 联动 / 灵动岛多界面…）。
            // 与进程外插件的区别：够得到原生窗口消息与岛本体；代价是不可卸载，只能启停。
            Text {
                text: "内置模块"
                font.pixelSize: 16; font.weight: Font.Medium
                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
            }
            Repeater {
                model: agentUi.modules
                delegate: M.Md3Card {
                    required property var modelData
                    anchors.left: parent ? parent.left : undefined
                    anchors.right: parent ? parent.right : undefined
                    md3: page.tok
                    variant: "outlined"

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 4

                        Item {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 24
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + "  v" + modelData.version
                                font.pixelSize: 14; font.weight: Font.Medium
                                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.running ? "运行中" : "已停用"
                                font.pixelSize: 12
                                color: modelData.running
                                       ? (page.tok ? page.tok.cOk : "#1a7f37")
                                       : (page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0")
                            }
                        }
                        Text {
                            width: parent.width
                            visible: modelData.description !== ""
                            text: modelData.description
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                        }
                        Row {
                            topPadding: 6
                            spacing: 10
                            M.Md3Btn {
                                md3: page.tok
                                text: modelData.enabled ? "停用" : "启用"
                                variant: "tonal"
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
                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
            }
            Text {
                visible: agentUi.plugins.length === 0
                text: "商店拉取中，或还没有任何插件。\n点「刷新」重试（离线时仍会显示本机已装的插件）。"
                font.pixelSize: 13
                color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
            }

            Repeater {
                model: agentUi.plugins
                delegate: M.Md3Card {
                    required property var modelData
                    anchors.left: parent ? parent.left : undefined
                    anchors.right: parent ? parent.right : undefined
                    md3: page.tok
                    variant: "filled"

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 4

                        Item {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 24
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + "  v" + modelData.version
                                font.pixelSize: 14; font.weight: Font.Medium
                                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.status
                                font.pixelSize: 12
                                color: modelData.installed
                                       ? (page.tok ? page.tok.cOk : "#1a7f37")
                                       : (page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0")
                            }
                        }
                        Text {
                            width: parent.width
                            visible: modelData.author !== ""
                            text: "作者：" + modelData.author
                            font.pixelSize: 12
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                        }
                        Text {
                            width: parent.width
                            visible: modelData.description !== ""
                            text: modelData.description
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                        }
                        Row {
                            topPadding: 6
                            spacing: 10
                            M.Md3Btn {
                                md3: page.tok
                                text: modelData.installed ? (modelData.enabled ? "停用" : "启用") : "安装"
                                variant: "tonal"
                                height: 30
                                onClicked: modelData.installed
                                           ? agentUi.pluginEnable(modelData.id, !modelData.enabled)
                                           : agentUi.pluginInstall(modelData.id)
                            }
                            M.Md3Btn {
                                md3: page.tok
                                text: "卸载"
                                variant: "text"
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
