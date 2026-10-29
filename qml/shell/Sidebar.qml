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
// 由宿主经 ctx.window.th 注入（见 shell/MainWindow.qml）。
import QtQuick
import QtQuick.Layouts
import "../island"          // SvgIcon
import "../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: root

    /** 菜单项：[{ key, icon, label }]，icon = lucide 图标名（如 "users"）。 */
    property var items: []
    /** 当前选中的 key。 */
    property string current: ""
    /** 宿主上下文（同 _pageCtx 契约）；用于「返回经典界面」走 ctx.window.goPage。 */
    property var ctx: null

    // ── 账号卡（2026-10-28 蓝图还原）────────────────────────────────
    // 蓝图左侧导航：品牌行下面是一张账号卡（头像 + 账号名 + 角色）。
    // 三个文案全部由 C++ 给（backend.accountAvatarText / accountName / roleText）——
    // 取首字、角色→中文这类映射是逻辑，不下放给 QML。
    property string avatarText: ""
    property string accountTitle: ""
    property string accountSub: ""

    // ── 骨架结构尺寸（AGENTS.md §9.2：命名常量集中声明，不散落魔法数）──
    readonly property int navWidth: 240        // 规范：NavigationDrawer 宽 240
    readonly property int navItemH: 56         // MD3 导航项高（规范 48 的 MD3 口径取 56）
    readonly property int brandRowH: 48        // 品牌行高
    readonly property int backRowH: 44         // 「返回经典界面」行高
    readonly property int iconPx: 20           // 菜单图标边长

    signal activated(string key)

    Layout.fillHeight: true
    Layout.preferredWidth: root.navWidth

    implicitWidth: root.navWidth

    Rectangle {
        anchors.fill: parent
        color: Md3.cSurfaceVar
    }

    // 右缘发丝分隔线（与主内容区分界，避免侧栏"贴"在主区上）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Md3.cOutlineVar
        opacity: 0.5
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        // ── 品牌行（蓝图：w-8 h-8 绿色圆角方块 + 品牌名，font-extrabold）──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.brandRowH
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Md3.spXS
                anchors.verticalCenter: parent.verticalCenter
                spacing: Md3.spS

                Rectangle {
                    width: 28; height: 28
                    radius: Md3.rS
                    color: Md3.cOk            // 蓝图 bg-green-500 ⇒ 本项目成功绿令牌
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: "星"
                        color: Md3.cOnPrimary
                        font.pixelSize: 13
                        font.bold: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "星集控"
                    font.pixelSize: Md3.titleM
                    font.weight: Font.ExtraBold
                    color: Md3.cOnSurface
                }
            }
        }

        // ── 账号卡（蓝图：头像 + 账号名 + 角色）──
        // 文案全来自 C++；这里只摆位置。accountTitle 为空（未拿到身份）时整卡不出现，
        // 不摆一个空框占位 —— 那看起来像加载失败。
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: Md3.spXS
            Layout.rightMargin: Md3.spXS
            radius: Md3.rM
            color: Md3.cCont
            visible: root.accountTitle.length > 0

            Row {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10
                Rectangle {
                    width: 36; height: 36
                    radius: 18
                    color: Md3.cContHighest
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: root.avatarText
                        color: Md3.cOnSurface
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: root.accountTitle
                        color: Md3.cOnSurface
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                        width: parent.parent.width - 54
                    }
                    Text {
                        text: root.accountSub
                        color: Md3.cOnSurfaceVar
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.parent.width - 54
                    }
                }
            }
        }

        Repeater {
            model: root.items
            delegate: Rectangle {
                id: navItem
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: root.navItemH
                radius: Md3.rFull
                color: (root.current === modelData.key)
                       ? Md3.cSecondaryCont
                       : (navMa.containsMouse ? Md3.cContHigh : "transparent")
                Behavior on color { ColorAnimation { duration: Md3.durShort } }

                // 终末地工业青 · 选中项左侧细规（"当前终端"冷调点缀）
                // 2026-10-22：硬编码 #2FB8C6 → 令牌 cAccent（零硬编码纪律）
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
                    color: Md3.cAccent
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Md3.spM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    SvgIcon {
                        name: modelData.icon
                        tint: (root.current === modelData.key) ? Md3.cOnSecondaryCont : Md3.cOnSurfaceVar
                        px: root.iconPx
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        font.pixelSize: Md3.bodyM
                        font.weight: (root.current === modelData.key) ? Font.Medium : Font.Normal
                        color: (root.current === modelData.key) ? Md3.cOnSecondaryCont : Md3.cOnSurfaceVar
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
            Layout.preferredHeight: root.backRowH
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Md3.spM
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                SvgIcon {
                    name: "arrow-left"
                    tint: Md3.cOnSurfaceVar
                    px: root.iconPx
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "返回经典界面"
                    font.pixelSize: Md3.bodyM
                    color: Md3.cOnSurfaceVar
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
