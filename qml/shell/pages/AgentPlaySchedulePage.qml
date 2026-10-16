import QtQuick
import QtQuick.Controls
import "../../components"
import "../../components/md3" as M

// 被控端 · 播放时段页（openPlayScheduleDialog 的真实迁移 · U5-A3 第二批）
// 开关走 switchChanged("playScheduleEnabled") 落 QSettings；
// 「保存」把多行文本解析成窗口列表交给 C++（解析口径与 Widgets 版一致：trim + 跳空行）。
// ⚠️ 多行编辑用 QtQuick.Controls TextArea —— 共享 md3/ 组件库暂无多行输入件，
//    只做 token 配色（妥协点，已记归档；后续若补 Md3TextArea 再换）。
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
                    spacing: 4

                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "启用播放时段限制"
                        supporting: "只在这些时段内允许广播/播放"
                        checked: agentUi.playScheduleEnabled
                        onToggled: function (v) { agentUi.playScheduleEnabled = v }
                    }
                    Text {
                        width: parent.width
                        text: "允许的时段（每行一个，格式 HH:MM-HH:MM；可跨午夜如 22:00-07:00）："
                        font.pixelSize: 12
                        topPadding: 6
                        color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 120
                        radius: page.tok ? page.tok.rM : 12
                        color: page.tok ? page.tok.cSurface : "#1C1B1F"
                        border.width: 1
                        border.color: page.tok ? page.tok.cOutline : "#938F99"

                        TextArea {
                            id: winEdit
                            anchors.fill: parent
                            anchors.margins: 8
                            text: agentUi.playWindowsText
                            font.pixelSize: 13
                            font.family: "Consolas"
                            color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                            placeholderText: "08:00-11:30\n14:00-17:00"
                            placeholderTextColor: page.tok ? page.tok.cOutline : "#938F99"
                            background: null
                            wrapMode: TextArea.Wrap
                            // 编辑期间不回写 ctx（避免 fill 把正在输入的文本顶掉）；
                            // 焦点离开或保存时由 C++ fill 统一刷新。
                            onActiveFocusChanged: if (!activeFocus) text = agentUi.playWindowsText
                        }
                    }
                    Text {
                        width: parent.width
                        text: agentUi.playStateText
                        font.pixelSize: 12
                        topPadding: 4
                        color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                    }
                    Row {
                        topPadding: 6
                        spacing: 10
                        M.Md3Btn {
                            md3: page.tok
                            text: "保存"
                            variant: "filled"
                            onClicked: {
                                agentUi.playWindowsText = winEdit.text
                                agentUi.savePlaySchedule()
                            }
                        }
                        M.Md3Btn {
                            md3: page.tok
                            text: "打开数据目录"
                            variant: "text"
                            onClicked: agentUi.openDataDir()
                        }
                    }
                }
            }
        }
    }
}
