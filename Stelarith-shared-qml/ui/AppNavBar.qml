// 星集控 · 底部导航（AppNavBar）—— 2026-10-29 QtShadcn 规范
//
// ── 定位 ────────────────────────────────────────────────────────────────
// 取代共享库 `md3/Md3NavBar`（共享库 App 系补齐第 3 件）。手机端壳（mobile/Main.qml、
// mobile/MobileShell.qml）底部导航用。**API 不变**：items / currentIndex / activated(index)。
//
// ── ⚠️ 与旧件最大的行为差异：icon 从 emoji 改成 lucide 图标名 ─────────────
// 旧件 items[].icon 直接放 emoji 字符（"📊"），而项目早有结论：**本机 emoji 渲染成方块
// （tofu）**（Sidebar.qml 对 Md3ListItem 的注释、SvgIcon.qml 文件头都写着）。
// ⇒ 新件改用 SvgIcon（lucide 实体，见 resources/icons/），调用方把 emoji 换成图标名：
//     📊→"layout-grid"、💻→"monitor"、📹→"video"、⚙️→"settings"
// 这不是"顺手改别处"，是新建件必须自带的能力：否则等于把已知缺陷复制一份。
//
// ── 其它差异 ────────────────────────────────────────────────────────────
// · 激活 pill 底：旧件 secondaryContainer（浅紫）→ 新件 **primary 实底** + primaryForeground
//   图标（去边框后浅底分不开，与 AppSegmentedButton 同一口径）。
// · badge：旧件 cError/cOnError → destructive / destructiveForeground。
// · 导航条底：surface（与内容区 background 拉开一档，去边框后仍分得开）。
//
// 用法：
//   AppNavBar {
//       anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
//       items: [ { icon: "layout-grid", label: "概览" }, { icon: "monitor", label: "设备" } ]
//   }

import QtQuick
import "../island"               // SvgIcon
import "theme"

Item {
    id: root

    /** [{ icon, label, badge }]；icon = lucide 图标名（不是 emoji）。 */
    property var items: []
    property int currentIndex: 0

    signal activated(int index)

    implicitWidth: 360
    implicitHeight: 80

    Rectangle {
        anchors.fill: parent
        color: Theme.color.surface
    }

    Row {
        anchors.fill: parent
        Repeater {
            model: root.items
            delegate: Item {
                required property var modelData
                required property int index
                readonly property bool on: root.currentIndex === index

                width: root.width / root.items.length
                height: 80

                // 激活指示器 pill（64×32）
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 12
                    width: 64
                    height: 32
                    radius: height / 2
                    color: parent.on ? Theme.color.primary : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                    SvgIcon {
                        anchors.centerIn: parent
                        name: modelData.icon || ""
                        px: 18
                        tint: parent.on ? Theme.color.primaryForeground
                                        : Theme.color.textSecondary
                    }

                    // 角标（在线/待办计数）
                    Rectangle {
                        visible: modelData.badge !== undefined && modelData.badge !== ""
                        anchors.right: parent.right
                        anchors.top: parent.top
                        width: 16
                        height: 16
                        radius: 8
                        color: Theme.color.destructive
                        Text {
                            anchors.centerIn: parent
                            text: modelData.badge !== undefined ? modelData.badge : ""
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            color: Theme.color.destructiveForeground
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 48
                    text: modelData.label || ""
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: parent.on ? Theme.color.textPrimary : Theme.color.textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.currentIndex = index
                        root.activated(index)
                    }
                }
            }
        }
    }
}
