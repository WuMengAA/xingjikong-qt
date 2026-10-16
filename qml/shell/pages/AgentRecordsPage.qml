import QtQuick
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

            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 6

                    Item {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 24
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "噪音记录"
                            font.pixelSize: 14; font.weight: Font.Medium
                            color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        }
                        M.Md3Btn {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            md3: page.tok
                            text: "打开记录文件夹"
                            variant: "text"
                            height: 30
                            onClicked: agentUi.openNoiseFolder()
                        }
                    }
                    Text {
                        width: parent.width
                        text: agentUi.noiseLogText
                        font.pixelSize: 12
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                    }
                }
            }

            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 6

                    Item {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 24
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "自习记录"
                            font.pixelSize: 14; font.weight: Font.Medium
                            color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        }
                        M.Md3Btn {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            md3: page.tok
                            text: "打开记录文件夹"
                            variant: "text"
                            height: 30
                            onClicked: agentUi.openStudyFolder()
                        }
                    }
                    Text {
                        width: parent.width
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
