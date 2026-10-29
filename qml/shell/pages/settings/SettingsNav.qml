// 设置页左侧分类菜单（2026-10-28 从 AgentSettingsPage.qml 抽出）
//
// 为什么单独成件：主文件骨架抽掉分类菜单后才能压进 QML 200 行红线；
// 且这份分类清单未来岛面/托盘也可能复用（同一份 settingsSections 渲染）。
//
// 行为与抽出前逐字一致：分类模型、选中态三件（底 / 字色 / 字重）全由
// C++ 的 settingsSections 给结果值，本件不做 key 比较、不写三元配色。
// 定位（贴左、上下贴边）由调用方给 anchors —— 组件不自己决定挂在哪儿。
//
// 2026-10-30 用户点单「全部换新」：
//   · 去掉 `property var ash` 注入，底色改读共享库 `Theme.color.surfaceVariant`；
//   · 删掉右缘 1px 发丝线 —— 2026-10-29 规范「去边框，分区只靠背景色差」，
//     侧栏本就比页面底深/浅一档，靠色差即可分界，不需要描边。
import QtQuick

import "../../../components/theme"
import "../../../island"   // SvgIcon（lucide 图标名）

Rectangle {
    id: root

    width: 200
    color: Theme.color.surfaceVariant

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 4

        Text {
            text: "设置"
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.color.textPrimary
            bottomPadding: 8
            leftPadding: 8
        }

        // 分类清单：label/icon/active 全由 C++ 给 —— QML 不做 key 比较
        Repeater {
            model: agentUi.settingsSections
            delegate: Rectangle {
                id: navRow
                required property var modelData
                width: parent.width
                height: 40
                radius: 20
                // 选中态三件（底色/字色/字重）全部取自 modelData —— C++ 算好的结果值，
                // QML 这里没有 active ? A : B 这种三元配色判断。
                color: modelData.rowBg
                Behavior on color { ColorAnimation { duration: 160 } }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    // 图标：C++ 给的 icon 名 → 真 SVG（着色同 fg）。
                    // 2026-10-28 实机截图前这里是 24×24 色块占位（注释写"图标名有限，
                    // 不硬凑"），但七个图标资源其实全在，属于"占了位没接"的欠账。
                    SvgIcon {
                        name: modelData.icon
                        tint: modelData.fg
                        px: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        font.pixelSize: 14
                        font.weight: modelData.weight
                        color: modelData.fg
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // 只报 key，切换合法性由 C++ 判（未知 key 会被忽略并回原值）
                    onClicked: agentUi.settingsTab = navRow.modelData.key
                }
            }
        }
    }
}
