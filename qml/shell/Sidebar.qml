// MD3 左侧导航栏（Sidebar）· Expanded 宽屏断点 · V1.0 P1 骨架层
//
// 规格（用户点单）：宽 240px，背景 SurfaceVariant，放「仪表盘 / 班级列表 / 设置」等菜单。
//
// ── 为什么不用 Md3ListItem ────────────────────────────────────────────
// Md3ListItem 的 icon 参数是 **emoji 占位**（组件注释原文），本机渲染成方块（tofu）——
// 项目纪律「符号字形会 tofu ⇒ 用 SvgIcon 或汉字」正是在说这件事。
// 所以菜单项按 MD3 navigation drawer item 的规范自绘（高 56、全圆选中底
// secondaryContainer），图标走 SvgIcon（真源是随 exe 打包的 lucide 图标）。
//
// 组件契约（md3/ 目录下所有件一致）：md3 属性接设计令牌实例，
// 由宿主经 ctx.window.th 注入（见 pages/Md3Shell.qml）。
import QtQuick
import QtQuick.Layouts
import "../components/md3"
import "../island"          // SvgIcon

Item {
    id: root

    property var md3: ({})
    /** 菜单项：[{ key, icon, label }]，icon = lucide 图标名（如 "users"）。 */
    property var items: []
    /** 当前选中的 key。 */
    property string current: ""
    /** 宿主上下文（同 _pageCtx 契约）；用于「返回经典界面」走 ctx.window.goPage。 */
    property var ctx: null

    signal activated(string key)

    Layout.fillHeight: true
    Layout.preferredWidth: 240

    implicitWidth: 240

    Rectangle {
        anchors.fill: parent
        color: md3.cSurfaceVar
    }

    // 右缘发丝分隔线（与主内容区分界，避免侧栏"贴"在主区上）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: md3.cOutlineVar
        opacity: 0.5
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        // 品牌行
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "星集控"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: md3.cOnSurfaceVar
            }
        }

        Repeater {
            model: root.items
            delegate: Rectangle {
                id: navItem
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                radius: md3.rFull
                color: (root.current === modelData.key)
                       ? md3.cSecondaryCont
                       : (navMa.containsMouse ? md3.cContHigh : "transparent")
                Behavior on color { ColorAnimation { duration: md3.durShort } }

                // 终末地工业青 · 选中项左侧细规（"当前终端"冷调点缀）
                Rectangle {
                    visible: root.current === modelData.key
                    width: 3
                    radius: 1.5
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 18
                    anchors.bottomMargin: 18
                    color: "#2FB8C6"
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    SvgIcon {
                        name: modelData.icon
                        tint: (root.current === modelData.key) ? md3.cOnSecondaryCont : md3.cOnSurfaceVar
                        px: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        font.pixelSize: 14
                        font.weight: (root.current === modelData.key) ? Font.Medium : Font.Normal
                        color: (root.current === modelData.key) ? md3.cOnSecondaryCont : md3.cOnSurfaceVar
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: navMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.activated(navItem.modelData.key)
                }
            }
        }

        Item { Layout.fillHeight: true }

        // H7：回旧壳出口（骨架与旧壳并存期必须有退路，否则老师被困在新壳里）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                SvgIcon {
                    name: "arrow-left"
                    tint: md3.cOnSurfaceVar
                    px: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "返回经典界面"
                    font.pixelSize: 14
                    color: md3.cOnSurfaceVar
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: if (root.ctx) root.ctx.window.goPage(1)
            }
        }
    }
}
