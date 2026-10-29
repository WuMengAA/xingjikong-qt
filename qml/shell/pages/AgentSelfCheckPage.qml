import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/md3" as M

// 被控端 · 配置自检页（openSelfCheck/runSelfCheck 的真实迁移 · U5-A3 第二批）
// 四项检查由 C++ buildSelfCheckItems() 判据生成（与 Widgets 版一字不差）；
// 「一键修复」发 selfCheckFixRequested → main.cpp 执行（配置缺→配置向导；否则立即重连）。
Item {
    id: page
    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null

    Component.onCompleted: agentUi.runSelfCheckCheck()

    Flickable {
        anchors.fill: parent
        contentWidth: width        // ⚠️ 不设 contentItem 宽度塌陷 ⇒ 全白（设置页实证）
        contentHeight: col.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.topMargin: 16
            spacing: 14

            // 档② 换布局（2026-10-30）：AppCard 默认容器是 ColumnLayout ⇒ 卡内改用 Layout 附加属性。
            // ⚠️ Repeater 的 delegate 直接成为 ColumnLayout 的子项 ⇒ delegate 上也要写
            //    Layout.fillWidth / Layout.preferredHeight（anchors 会被 Layout 忽略）。
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "配置自检"
                        font.pixelSize: 14; font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "自动检查本机接入状态。红色项表示需要处理后才能被管理端正常管控。"
                        font.pixelSize: 12
                        color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                        bottomPadding: 8
                    }

                    Repeater {
                        model: agentUi.selfCheckItems
                        delegate: Item {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12; height: 12; radius: 6
                                color: modelData.ok
                                       ? (page.tok ? page.tok.cOk : "#1a7f37")
                                       : (page.tok ? page.tok.cError : "#c0392b")
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 22
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name
                                font.pixelSize: 13
                                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                            }
                            AppButton {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.fix
                                variant: "secondary"
                                size: "sm"
                                visible: !modelData.ok
                                onClicked: agentUi.selfCheckFix()
                            }
                        }
                    }
                }
            }

            Row {
                spacing: 10
                AppButton {
                    text: "重新检测"
                    variant: "solid"
                    onClicked: agentUi.runSelfCheckCheck()
                }
            }
        }
    }
}
