// 被控端 · 配置自检页（openSelfCheck/runSelfCheck 的真实迁移 · U5-A3 第二批）
// 四项检查由 C++ buildSelfCheckItems() 判据生成（与 Widgets 版一字不差）；
// 「一键修复」发 selfCheckFixRequested → main.cpp 执行（配置缺→配置向导；否则立即重连）。
//
// 2026-10-30 用户点单「全部换新」：`tok`（= ctx.theme，Md3 令牌）→ 共享库 `Theme` 单例，
// 同时清掉 5 处 `tok ? tok.cX : "#hex"` 三元兜底（Hex 全删）。`import md3 as M` 本件早已无用，一并去掉。
//
// 2026-10-31 「组件显示雏形」批次：
//   · 补汇总行（C++ selfCheckSummaryText 给"共 N 项 · 通过 X · 需处理 Y"），
//     现场不必自己数红点；
//   · 每行补**状态文案**（正常／需处理）—— 旧版只有一颗小圆点，色弱/远看都读不出结论；
//   · 全通过时给一句结论条（selfCheckAllOk），避免"四个绿点，然后呢？"；
//   · 条目为空时走共享空态件 AppEmptyState（"尚未检测"），不再只剩一张空卡；
//   · 行高 40（8 的倍数）、按钮与状态文案进 RowLayout 靠右对齐（不写 x/y 反算）。
import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/theme"

Item {
    id: page
    anchors.fill: parent

    property var ctx

    Component.onCompleted: agentUi.runSelfCheckCheck()

    Flickable {
        anchors.fill: parent
        contentWidth: width        // ⚠️ 不设这个 contentItem 宽度塌陷 ⇒ 全白（设置页实证）
        contentHeight: col.height + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.top: parent.top
            anchors.topMargin: 16
            spacing: 16

            // 档② 换布局（2026-10-30）：AppCard 默认容器是 ColumnLayout ⇒ 卡内改用 Layout 附加属性。
            // ⚠️ Repeater 的 delegate 直接成为 ColumnLayout 的子项 ⇒ delegate 上也要写
            //    Layout.fillWidth / Layout.preferredHeight（anchors 会被 Layout 忽略）。
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppCardTitle {
                        text: "配置自检"
                        supporting: "自动检查本机接入状态。带「需处理」标记的项要先处理，"
                                  + "否则管理端不能正常管控这台设备。"
                    }

                    // 汇总：数量直接由 C++ 给（QML 不数数）
                    Text {
                        Layout.fillWidth: true
                        visible: agentUi.selfCheckItems.length > 0
                        text: agentUi.selfCheckSummaryText
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: agentUi.selfCheckAllOk ? Theme.color.success : Theme.color.textSecondary
                    }

                    // 全通过结论条：只给结论，不重复条目
                    Text {
                        Layout.fillWidth: true
                        visible: agentUi.selfCheckAllOk
                        text: "全部检查通过 —— 这台设备已可被管理端正常管控。"
                        font.pixelSize: 12
                        color: Theme.color.success
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: agentUi.selfCheckItems
                        delegate: Item {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40

                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8; height: 8; radius: 4
                                color: modelData.ok ? Theme.color.success
                                                    : Theme.color.destructive
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 20
                                anchors.right: actions.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name
                                font.pixelSize: 13
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                            }
                            // 状态 + 修复动作靠右成组（RowLayout 自己对齐，不写 x/y 反算）
                            RowLayout {
                                id: actions
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12

                                Text {
                                    text: modelData.ok ? "正常" : "需处理"
                                    font.pixelSize: 12
                                    color: modelData.ok ? Theme.color.success
                                                        : Theme.color.destructive
                                }
                                AppButton {
                                    text: modelData.fix
                                    variant: "secondary"
                                    size: "sm"
                                    visible: !modelData.ok
                                    onClicked: agentUi.selfCheckFix()
                                }
                            }
                        }
                    }

                    // 空态：还没跑过检测（或模型为空）时，说明这里会出现什么
                    AppEmptyState {
                        visible: agentUi.selfCheckItems.length === 0
                        title: "尚未检测"
                        description: "点下面的「重新检测」，会立刻检查配置、云端连接、注册状态与班级绑定四项。"
                    }
                }
            }

            RowLayout {
                spacing: 8
                AppButton {
                    text: "重新检测"
                    variant: "solid"
                    onClicked: agentUi.runSelfCheckCheck()
                }
            }
        }
    }
}
