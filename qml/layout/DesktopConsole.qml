// 桌面集控台布局模板（1280×800 · MD3 唯一模板 · 2026-10-14）
//
// 管理端桌面主界面骨架：左侧导航轨 + 顶栏（作用域切换器/搜索/账户）+
// 统计行 + 设备网格卡（实时画面占位 + 状态 + 快捷指令）。
// 这是 LayoutLab 的布局演示件（假数据），供评价后按此迁移桌面 Main.qml。
import QtQuick
import QtQuick.Layouts
import "../components"
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton，2026-10-25；dark 由主外壳统一写）

Item {
    id: root
    width: 1280; height: 800
    // MD3 令牌：内部自建实例（深/浅由 dark 驱动）。
    // ⚠️ 不许让宿主晚赋值替换整个 md3 实例——创建期 md3 为空会让 Behavior 时长
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    //    与手机端 mobile/ 页面同一模式：页面自建 Md3，宿主只传 dark 布尔。
    property bool dark: false
    property string currentUid: "PC-CLASS-01"
    property int page: 1
    readonly property var navItems: [
        { icon: "📊", label: "概览" },
        { icon: "🖥", label: "集控" },
        { icon: "📹", label: "监控" },
        { icon: "📅", label: "课表" },
        { icon: "📜", label: "日志" },
        { icon: "⚙️", label: "设置" }
    ]
    property var devices: [
        { name: "高一1班-01", uid: "PC-CLASS-01", version: "0.6.31", online: true,  live: true },
        { name: "高一1班-02", uid: "PC-CLASS-02", version: "0.6.31", online: true,  live: true },
        { name: "教师机",     uid: "PC-TEACHER-02", version: "0.6.30", online: true,  live: false },
        { name: "机房-03",    uid: "PC-ROOM-03", version: "0.6.31", online: false, live: false },
        { name: "机房-04",    uid: "PC-ROOM-04", version: "0.6.29", online: true,  live: false },
        { name: "备课室",     uid: "PC-PREP-05", version: "0.6.31", online: false, live: false }
    ]

    Rectangle { anchors.fill: parent; color: Theme.color.surface }

    // ── 左侧导航轨（MD3 navigation rail：220 宽，激活项全宽 pill）──
    Rectangle {
        width: 220; height: parent.height
        color: Theme.color.background
        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            // 品牌行
            Item {
                width: parent.width; height: 56
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text { text: "🐋"; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "星集控"; font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.textPrimary; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.color.textSecondary }
            Item { width: parent.width; height: 8 }
            // 导航项
            Repeater {
                model: root.navItems
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: 196; height: 56
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius.full
                        color: root.page === index ? Theme.color.primary : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                        Rectangle {
                            anchors.fill: parent; radius: parent.radius
                            color: Theme.color.textPrimary
                            opacity: navMa.pressed ? 0.10 : (navMa.containsMouse ? 0.08 : 0)
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                    }
                    Row {
                        anchors.left: parent.left; anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text { text: modelData.icon; font.pixelSize: 18 }
                        Text {
                            text: modelData.label
                            font.pixelSize: 14; font.weight: Font.Medium
                            color: root.page === index ? Theme.color.primaryForeground : Theme.color.textSecondary
                        }
                    }
                    MouseArea { id: navMa; anchors.fill: parent; hoverEnabled: true; onClicked: root.page = index }
                }
            }
            // 弹性占位（把底部块推到底）
            Item {
                width: parent.width
                height: Math.max(0, root.height - 12 - 56 - 1 - 8 - root.navItems.length * 56 - 16 - 116)
            }
            // 底部：主题切换 + 账户
            Rectangle { width: parent.width; height: 1; color: Theme.color.textSecondary }
            Item {
                width: parent.width; height: 64
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Rectangle {
                        width: 36; height: 36; radius: 18
                        color: Theme.color.surface
                        Text { anchors.centerIn: parent; text: "🌗"; font.pixelSize: 16 }
                    }
                    Column {
                        spacing: 1
                        Text { text: "张老师"; font.pixelSize: 14; color: Theme.color.textPrimary }
                        Text { text: "教师 · 高一1班"; font.pixelSize: 11; color: Theme.color.textSecondary }
                    }
                }
            }
        }
    }

    // ── 顶栏（MD3 top app bar 64）──
    Rectangle {
        x: 220
        width: parent.width - 220; height: 64
        color: Theme.color.surface
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.color.textSecondary }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: root.navItems[root.page].label + " · 教室总览"
            font.pixelSize: 20; color: Theme.color.textPrimary
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            // 作用域切换器（多校设计：顶栏 chip，点击展开学校/班级树）
            Rectangle {
                width: 132; height: 36; radius: 18
                color: Theme.color.surface
                border.color: Theme.color.textSecondary; border.width: 1
                Text { anchors.centerIn: parent; text: "高一1班 ▾"; font.pixelSize: 14; color: Theme.color.textPrimary }
            }
            // 搜索（filled 字段样式）
            Rectangle {
                width: 240; height: 40; radius: Theme.radius.sm
                color: Theme.color.surfaceVariant
                Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: "🔍  搜索设备 / UID"; font.pixelSize: 14; color: Theme.color.textSecondary }
            }
            // 账户头像
            Rectangle {
                width: 36; height: 36; radius: 18
                color: Theme.color.primary
                Text { anchors.centerIn: parent; text: "张"; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.primaryForeground }
            }
        }
    }

    // ── 内容区 ──
    Column {
        x: 220; y: 64
        width: parent.width - 220; height: parent.height - 64
        anchors.margins: 20
        spacing: 16

        // 统计行（4 卡）
        Row {
            spacing: 16
            Repeater {
                model: [
                    { k: "设备总数", v: "6 台",  icon: "💻" },
                    { k: "在线",     v: "4 台",  icon: "🟢" },
                    { k: "告警",     v: "1 条",  icon: "⚠️" },
                    { k: "今日指令", v: "128",   icon: "📨" }
                ]
                delegate: AppCard {
                    required property var modelData
                    shadow: false                  // 原 variant: "filled"（无阴影）
                    width: 252; height: 84
                    Row {
                        // ⚠️ AppCard 内部已是 ColumnLayout：不能 anchors，改走 Layout 附加属性
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 14
                        Rectangle {
                            width: 40; height: 40; radius: 20
                            color: Theme.color.primary
                            Text { anchors.centerIn: parent; text: modelData.icon; font.pixelSize: 18 }
                        }
                        Column {
                            spacing: 2
                            Text { text: modelData.k; font.pixelSize: 14; color: Theme.color.textSecondary }
                            Text { text: modelData.v; font.pixelSize: 20; font.weight: Font.Medium; color: Theme.color.textPrimary }
                        }
                    }
                }
            }
        }

        // 设备网格（3 列卡）
        GridView {
            width: parent.width; height: parent.height - 116
            clip: true
            model: root.devices
            cellWidth: 344; cellHeight: 252
            // ⚠️ 委托用 plain Item 自绘卡片，不直接拿 Md3Card 当委托——
            //    GridView 委托池 + Md3Card（Rectangle+default alias）实测内容不渲染
            //    （2026-10-14 探针）。Row/Repeater 里的 Md3Card 没问题（统计卡就在用）。
            delegate: Item {
                required property var modelData
                width: 328; height: 236
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.md
                    color: Theme.color.surface
                    Column {
                    anchors.fill: parent
                    spacing: 10
                    // 画面区（占位）
                    Rectangle {
                        width: parent.width; height: 148
                        radius: Theme.radius.sm
                        color: modelData.live ? Theme.color.surfaceVariant : Theme.color.background
                        Text {
                            anchors.centerIn: parent
                            text: modelData.live ? "📹  实时画面" : (modelData.online ? "🖥  待唤醒画面" : "📴  离线")
                            font.pixelSize: 14
                            color: modelData.online ? Theme.color.textSecondary : Theme.color.textSecondary
                        }
                    }
                    // 信息行
                    // ⚠️ height 必须显式给：本行在 Column 里 implicitHeight 实测被算成 0
                    //    （Row 高度 0 ⇒ 子项全不画，2026-10-14 探针坑）。48 = 两行文字 + 按钮行。
                    Row {
                        width: parent.width
                        height: 48
                        spacing: 8
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.online ? Theme.color.success : Theme.color.destructive
                        }
                        Column {
                            width: parent.width - 150
                            Text { text: modelData.name; font.pixelSize: 14; color: Theme.color.textPrimary; elide: Text.ElideRight; width: parent.width }
                            Text { text: "v" + modelData.version + " · " + modelData.uid; font.pixelSize: 12; color: Theme.color.textSecondary; elide: Text.ElideRight; width: parent.width }
                        }
                        // 快捷指令（MD3 text 按钮）
                        Row {
                            anchors.right: parent.right
                            spacing: 4
                            Rectangle {
                                width: 48; height: 32; radius: 16
                                color: pingMa.pressed ? Theme.color.surfaceVariant : "transparent"
                                Text { anchors.centerIn: parent; text: "ping"; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.primary }
                                MouseArea { id: pingMa; anchors.fill: parent; hoverEnabled: true; onClicked: root.currentUid = modelData.uid }
                            }
                            Rectangle {
                                width: 48; height: 32; radius: 16
                                color: sdMa.pressed ? Theme.color.destructive : "transparent"
                                Text { anchors.centerIn: parent; text: "关机"; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.destructive }
                                MouseArea { id: sdMa; anchors.fill: parent; hoverEnabled: true; onClicked: root.currentUid = modelData.uid }
                            }
                        }
                    }
                }
            }
        }
    }
}
    }
