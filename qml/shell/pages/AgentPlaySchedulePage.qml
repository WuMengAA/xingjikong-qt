import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/theme"

// 被控端 · 播放时段页（openPlayScheduleDialog 的真实迁移 · U5-A3 第二批）
// 开关走 switchChanged("playScheduleEnabled") 落 QSettings；
// 「保存」把多行文本解析成窗口列表交给 C++（解析口径与 Widgets 版一致：trim + 跳空行）。
// ⚠️ 多行编辑用 **QtQuick 原生 TextEdit**（不是 QtQuick.Controls 的 TextArea）：
//    被控端部署不含 QtQuick Controls 运行库，真机实证 `Cannot load library
//    qtquickcontrols2plugin.dll` ⇒ 整页 entry 解析失败 ⇒ 面板空白（2026-10-16）。
//    占位提示与滚动自己来（TextEdit 没有 placeholderText / 自带滚动）。
Item {
    id: page
    anchors.fill: parent

    property var ctx

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
            spacing: 14

            // 档② 换布局（2026-10-30）：Md3Card 的默认容器是普通 Item（子项用 anchors 定位），
            // 而 AppCard 的默认容器是 ColumnLayout ⇒ 卡内一律改用 Layout 附加属性、
            // 且 height 必须写成 Layout.preferredHeight（Layout 会忽略 height）。
            // variant: filled → muted（AppCard.muted = surfaceVariant 底、不叠阴影）。
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    AppSwitchRow {
                        Layout.fillWidth: true
                        text: "启用播放时段限制"
                        supporting: "只在这些时段内允许广播/播放"
                        checked: agentUi.playScheduleEnabled
                        onToggled: function (v) { agentUi.playScheduleEnabled = v }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "允许的时段（每行一个，格式 HH:MM-HH:MM；可跨午夜如 22:00-07:00）："
                        font.pixelSize: 12
                        topPadding: 6
                        color: Theme.color.textSecondary
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        radius: Theme.radius.md
                        color: Theme.color.surface
                        // 2026-10-30 去边框：靠 surface 底与 muted 卡底的色差分界（原 border.width/border.color 删除）

                        Flickable {
                            id: winScroll
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            contentWidth: width
                            contentHeight: winEdit.contentHeight + 8
                            boundsBehavior: Flickable.StopAtBounds

                            TextEdit {
                                id: winEdit
                                width: winScroll.width
                                text: agentUi.playWindowsText
                                font.pixelSize: 13
                                font.family: "Consolas"
                                color: Theme.color.textPrimary
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                // 编辑期间不回写 ctx（避免 fill 把正在输入的文本顶掉）；
                                // 焦点离开或保存时由 C++ fill 统一刷新。
                                onActiveFocusChanged: if (!activeFocus) text = agentUi.playWindowsText
                            }
                            // 占位提示（TextEdit 无 placeholderText，自己叠一层）
                            Text {
                                anchors.top: winEdit.top
                                anchors.left: winEdit.left
                                visible: winEdit.text.length === 0
                                text: "08:00-11:30\n14:00-17:00"
                                font.pixelSize: 13
                                font.family: "Consolas"
                                color: Theme.color.textSecondary
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: agentUi.playStateText
                        font.pixelSize: 12
                        topPadding: 4
                        color: Theme.color.textSecondary
                    }
                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 10
                        AppButton {
                            text: "保存"
                            variant: "solid"
                            onClicked: {
                                agentUi.playWindowsText = winEdit.text
                                agentUi.savePlaySchedule()
                            }
                        }
                        AppButton {
                            text: "打开数据目录"
                            variant: "ghost"
                            onClicked: agentUi.openDataDir()
                        }
                    }
                }
            }
        }
    }
}
