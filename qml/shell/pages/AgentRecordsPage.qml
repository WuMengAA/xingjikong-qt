import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/md3" as M

// 被控端 · 记录页（openNoiseLogDialog + openStudyLogDialog 的真实迁移 · U5-A3 第二批）
// 文本由 C++ buildNoiseLogText/buildStudyLogText 生成（口径与 Widgets 版一字不差），
// 每次打开面板时 fill 重读 —— 这里是"看"的界面，生成/落盘仍在采集链路。
Item {
    id: page
    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null

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
            spacing: 14

            // 档② 换布局（2026-10-30）：AppCard 默认容器是 ColumnLayout ⇒ 卡内改用 Layout 附加属性。
            // 标题行那个 Item 保留 anchors：Item 不是 Layout 容器，内部 anchors 合法，
            // 只需给它 Layout.fillWidth + Layout.preferredHeight（Layout 会忽略 height）。
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "噪音记录"
                            font.pixelSize: 14; font.weight: Font.Medium
                            color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        }
                        AppButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: "打开记录文件夹"
                            variant: "ghost"
                            height: 30
                            onClicked: agentUi.openNoiseFolder()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: agentUi.noiseLogText
                        font.pixelSize: 12
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                    }
                }
            }

            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "自习记录"
                            font.pixelSize: 14; font.weight: Font.Medium
                            color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        }
                        AppButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: "打开记录文件夹"
                            variant: "ghost"
                            height: 30
                            onClicked: agentUi.openStudyFolder()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: agentUi.studyLogText
                        font.pixelSize: 12
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                    }
                }
            }
        }
    }
}
