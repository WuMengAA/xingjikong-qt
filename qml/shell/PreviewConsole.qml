// 星集控管理端 · 新版外壳预览（2026-11-01）
//
// 这是 qml/layout/DesktopConsole.qml（新版外壳设计稿，2026-10-14 起的假数据版式）
// 的**真数据**版本：观感照设计稿，数据全部接 backend。
//
// 定位：**预览版**。用户在设置里打开「新版外观预览」后切到这一套；
// 关掉则回到经典外壳（qml/shell/Sidebar.qml + TopBar.qml + ActionBar.qml）。
// 确认满意后才考虑把整套外壳换成这一版（§10.8 需点单）。
//
// 与设计稿的两处**有意偏差**（理由写在各节注释里）：
//   ① 图标用 lucide SVG（image://svgicon/<name>）而不是 emoji
//      —— 设计稿用 emoji，本机会渲染成方块；且项目已有 SVG 图标库（随 exe 打包）。
//   ② 设备卡显示真实设备（backend.devices 按当前班过滤），不是假数据 6 台。
//
// 分层（AGENTS §10.9）：QML 只画图。数据从 backend 读、点击调 shell 既有方法，
// 不在 QML 里做业务判断。
import QtQuick
import QtQuick.Layouts
import "../components"
import "../components/theme"
import "../island"          // SvgIcon（lucide SVG）

Item {
    id: root

    // ── 输入（由 MainWindow 注入，与经典外壳同一套数据源）──────────────
    property var ctx: null
    property var backendRef: null
    property var navItems: []
    property string section: "dash"
    property string accountTitle: ""
    property string accountSub: ""
    property string accountAvatarText: ""
    property string currentClass: ""
    signal navActivated(string key)
    signal avatarClicked()
    /** 点开某台设备（uid）—— 由 MainWindow 接到既有 openUid 逻辑。 */
    signal deviceActivated(string uid)

    /** 设备网格用真设备（当前班过滤；未选班 = 全部）。 */
    property var devices: {
        if (!backend || !backend.devices) return []
        const cc = backend.currentClass || ""
        if (cc === "") return backend.devices
        return backend.devices.filter(function (d) { return d && (d.classCode || "") === cc })
    }

    readonly property int onlineCount: {
        var n = 0
        for (var i = 0; i < devices.length; ++i) if (devices[i].online !== false) n++
        return n
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.color.background
    }

    // ══ 左导航轨（设计稿：品牌区 + 图标导航项 + 底部用户卡）══════════
    Rectangle {
        id: rail
        width: 240
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: Theme.color.surface

        // 品牌区
        Item {
            id: brand
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 64
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.sm
                Rectangle {
                    width: 32; height: 32; radius: Theme.radius.md
                    color: Theme.color.primary
                    Text {
                        anchors.centerIn: parent
                        text: "星"
                        color: Theme.color.primaryForeground
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "星集控"
                    color: Theme.color.textPrimary
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.color.accent
            }
        }

        // 导航项
        Column {
            anchors.top: brand.bottom
            anchors.topMargin: Theme.spacing.md
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacing.xs

            Repeater {
                model: root.navItems
                delegate: Item {
                    required property var modelData
                    width: parent.width
                    height: 48
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing.sm
                        anchors.rightMargin: Theme.spacing.sm
                        radius: Theme.radius.full
                        color: root.section === modelData.key
                               ? Theme.color.primary
                               : (hover.hovered ? Theme.color.surfaceVariant : "transparent")
                        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                        HoverHandler { id: hover }
                    }
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing.lg
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing.md
                        SvgIcon {
                            name: modelData.icon
                            px: 20
                            tint: root.section === modelData.key
                                   ? Theme.color.primaryForeground
                                   : Theme.color.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            font.pixelSize: 14
                            color: root.section === modelData.key
                                  ? Theme.color.primaryForeground
                                  : Theme.color.textPrimary
                        }
                    }
                    TapHandler {
                        onTapped: root.navActivated(modelData.key)
                    }
                }
            }
        }

        // 底部用户卡（设计稿）
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 72
            color: Theme.color.surfaceVariant

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.md
                Rectangle {
                    width: 32; height: 32; radius: Theme.radius.full
                    color: Theme.color.primary
                    Text {
                        anchors.centerIn: parent
                        text: root.accountAvatarText || "?"
                        color: Theme.color.primaryForeground
                        font.pixelSize: 14
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: root.accountTitle || "未登录"
                        color: Theme.color.textPrimary
                        font.pixelSize: 14
                    }
                    Text {
                        text: root.accountSub
                        color: Theme.color.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: 120
                    }
                }
            }
            TapHandler { onTapped: root.avatarClicked() }
        }
    }

    // ══ 右侧主区（顶栏 + 统计行 + 内容）══════════════════════════════
    Item {
        id: main
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: rail.right
        anchors.right: parent.right

        // 顶栏（设计稿：班级下拉 + 搜索 + 头像）
        Item {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 64

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.color.accent
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.md

                // 班级下拉胶囊
                Rectangle {
                    width: 132; height: 40; radius: Theme.radius.full
                    color: Theme.color.surface
                    border.color: Theme.color.accent
                    border.width: 1
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing.md
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.currentClass || "全部班级"
                        font.pixelSize: 14
                        color: Theme.color.textPrimary
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacing.md
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                    }
                }

                // 搜索框
                Rectangle {
                    width: 240; height: 40; radius: Theme.radius.md
                    color: Theme.color.surfaceVariant
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing.md
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing.sm
                        SvgIcon { name: "search"; px: 18; tint: Theme.color.textSecondary }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "搜索设备 / UID"
                            font.pixelSize: 14
                            color: Theme.color.textSecondary
                        }
                    }
                }
            }

            // 头像（右上）
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36; radius: Theme.radius.full
                color: Theme.color.primary
                Text {
                    anchors.centerIn: parent
                    text: root.accountAvatarText || "?"
                    color: Theme.color.primaryForeground
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
                TapHandler { onTapped: root.avatarClicked() }
            }
        }

        // 统计行（设计稿：4 个圆图标统计）
        Item {
            id: stats
            anchors.top: topBar.bottom
            anchors.topMargin: Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.lg
            height: 72

            Row {
                anchors.fill: parent
                spacing: Theme.spacing.xl

                Repeater {
                    model: [
                        { icon: "monitor",  label: "设备总数", value: String(root.devices.length) },
                        { icon: "wifi",     label: "在线",     value: String(root.onlineCount) },
                        { icon: "triangle-alert", label: "离线", value: String(root.devices.length - root.onlineCount) },
                        { icon: "send",     label: "今日指令", value: "—" }
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: Theme.spacing.md
                        Rectangle {
                            width: 44; height: 44; radius: Theme.radius.full
                            color: Theme.color.accent
                            SvgIcon {
                                anchors.centerIn: parent
                                name: modelData.icon
                                px: 22
                                tint: Theme.color.primary
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: modelData.label
                                font.pixelSize: 12
                                color: Theme.color.textSecondary
                            }
                            Text {
                                text: modelData.value
                                font.pixelSize: 22
                                color: Theme.color.textPrimary
                            }
                        }
                    }
                }
            }
        }

        // 内容区（设备网格 · 设计稿 3 列）
        GridView {
            id: deviceGrid
            anchors.top: stats.bottom
            anchors.topMargin: Theme.spacing.lg
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.lg
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacing.lg
            clip: true
            model: root.devices
            // ⚠️ GridView **没有** columns 属性（那是 ListView 的）；GridView 是 flow 布局，
            //    列数由 cellWidth 决定：给一个 1/3 宽的格子 ⇒ 每行 3 个。
            cellWidth: Math.floor(width / 3)
            cellHeight: 190

            delegate: Rectangle {
                width: deviceGrid.cellWidth - Theme.spacing.md
                height: deviceGrid.cellHeight - Theme.spacing.md
                x: deviceGrid.cellX + Theme.spacing.sm / 2
                y: deviceGrid.cellY + Theme.spacing.sm / 2
                radius: Theme.radius.lg
                color: Theme.color.surface

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.md
                    spacing: Theme.spacing.sm

                    // 画面占位
                    Rectangle {
                        width: parent.width
                        height: 96
                        radius: Theme.radius.md
                        color: Theme.color.surfaceVariant
                        SvgIcon {
                            anchors.centerIn: parent
                            name: modelData.online !== false ? "monitor" : "triangle-alert"
                            px: 28
                            tint: Theme.color.textSecondary
                        }
                        Text {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: 28
                            text: modelData.online !== false ? "实时画面" : "离线"
                            font.pixelSize: 12
                            color: Theme.color.textSecondary
                        }
                    }

                    // 设备名 + 版本
                    Text {
                        width: parent.width
                        text: modelData.name || modelData.uid || "（未命名设备）"
                        font.pixelSize: 14
                        color: Theme.color.textPrimary
                        elide: Text.ElideRight
                    }
                    Row {
                        spacing: 4
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: modelData.online !== false ? Theme.color.success : Theme.color.destructive
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData.version || "—") + " · " + (modelData.uid || "")
                            font.pixelSize: 11
                            color: Theme.color.textSecondary
                            elide: Text.ElideRight
                            width: Math.max(0, parent.parent.width - 16)
                        }
                    }
                }

                TapHandler {
                    onTapped: {
                        if (!root.backendRef) return
                        root.backendRef.currentUid = modelData.uid || ""
                        root.deviceActivated(modelData.uid || "")
                    }
                }
            }
        }
    }
}
