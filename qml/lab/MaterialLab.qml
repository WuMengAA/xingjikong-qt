// ══════════════════════════════════════════════════════════════════════════════
// 星集控 · Material 3 测试页（MaterialLab）—— 2026-10-13
//
// 目的：给用户评价「UI 转 Material 3 + 手机竖屏」方向的**样品**。过了就是全项目
//   唯一优秀模板的种子；不过就重做。所以：一页之内把 MD3 的全部核心语言摊开——
//   色板 / 字阶 / 形状 / 高度 / 状态层 / 动效 / 组件族，浅深双主题。
//
// 技术路线（为什么这么选）：
//   · 不引 material-components-qml 外部库 —— 那是「把地基换掉」；本页用纯 Qt Quick
//     自绘 MD3 令牌，零新依赖、可热更、可完整回退，审美判断不受库的实现细节绑架。
//   · 不 import QtQuick.Controls —— 连标准控件都不用，全部 Item 级自绘，
//     每个像素都在 MD3 token 管辖内（也证明将来换皮只换这张令牌表）。
//   · 根是 Item（390×844，手机竖屏逻辑分辨率），不写死设备 —— 探针/真机/桌面通用。
//
// MD3 令牌来源：Material Design 3 baseline scheme + type scale + shape scale +
//   state layer(opacity) + motion easing。中文用 Microsoft YaHei（探针侧注册）。
// ══════════════════════════════════════════════════════════════════════════════

import QtQuick

Item {
    id: root
    width: 390
    height: 844

    // ── 主题（浅 / 深）──────────────────────────────────────────────────────
    property bool dark: false
    // 探针专用：截图时把 Flickable 滚到指定偏移（分段抓全页；软件渲染抓不到视口外）
    property int captureOffset: 0
    function toggleTheme() { dark = !dark; }
    // 探针专用：抓对话框 / Snackbar 触发态
    function showDemoDialog() { dlg.open(); }
    function showDemoSnack() { snack.show("已保存配置"); }

    // MD3 baseline 色板（浅）
    readonly property color cPrimary:        root.dark ? "#D0BCFF" : "#6750A4"
    readonly property color cOnPrimary:      root.dark ? "#381E72" : "#FFFFFF"
    readonly property color cPrimaryCont:    root.dark ? "#4F378B" : "#EADDFF"
    readonly property color cOnPrimaryCont:  root.dark ? "#EADDFF" : "#21005D"
    readonly property color cSecondary:      root.dark ? "#CCC2DC" : "#625B71"
    readonly property color cSecondaryCont:  root.dark ? "#4A4458" : "#E8DEF8"
    readonly property color cOnSecondaryCont:root.dark ? "#E8DEF8" : "#1D192B"
    readonly property color cTertiary:       root.dark ? "#EFB8C8" : "#7D5260"
    readonly property color cTertiaryCont:   root.dark ? "#633B48" : "#FFD8E4"
    readonly property color cOnTertiaryCont: root.dark ? "#FFD8E4" : "#31111D"
    readonly property color cError:          root.dark ? "#F2B8B5" : "#B3261E"
    readonly property color cErrorCont:      root.dark ? "#8C1D18" : "#F9DEDC"
    readonly property color cOnErrorCont:    root.dark ? "#F9DEDC" : "#410E0B"
    readonly property color cSurface:        root.dark ? "#1C1B1F" : "#FFFBFE"
    readonly property color cOnSurface:      root.dark ? "#E6E1E5" : "#1C1B1F"
    readonly property color cSurfaceVar:     root.dark ? "#49454F" : "#E7E0EC"
    readonly property color cOnSurfaceVar:   root.dark ? "#CAC4D0" : "#49454F"
    readonly property color cOutline:        root.dark ? "#938F99" : "#79747E"
    readonly property color cOutlineVar:     root.dark ? "#49454F" : "#CAC4D0"
    readonly property color cContLowest:     root.dark ? "#0F0D13" : "#FFFFFF"
    readonly property color cContLow:        root.dark ? "#1D1B20" : "#F7F2FA"
    readonly property color cCont:           root.dark ? "#211F26" : "#F3EDF7"
    readonly property color cContHigh:       root.dark ? "#2B2930" : "#ECE6F0"
    readonly property color cContHighest:    root.dark ? "#36343B" : "#E6E0E9"
    readonly property color cInverseSurf:    root.dark ? "#E6E1E5" : "#313033"
    readonly property color cInverseOn:      root.dark ? "#313033" : "#F4EFF4"
    // 状态层不透明度（MD3 规范值）
    readonly property real kHover: 0.08
    readonly property real kPress: 0.10
    readonly property real kFocus: 0.10
    // 形状（MD3 shape scale）
    readonly property int rXS: 4
    readonly property int rS: 8
    readonly property int rM: 12
    readonly property int rL: 16
    readonly property int rXL: 28
    // 动效（MD3 easing）
    readonly property var easingEmphasized: [0.05, 0.7, 0.1, 1.0, 4]  // emphasized decelerate
    readonly property var easingStandard:  [0.2, 0.0, 0.0, 1.0, 4]
    readonly property int durShort: 200
    readonly property int durMed: 350
    readonly property int durLong: 500

    // 字体：探针侧 app.setFont("Microsoft YaHei") 全局继承（Item 无 font 属性，
    // 写在这里就是 "Cannot assign to non-existent property"，2026-10-13 踩过）

    Rectangle { anchors.fill: parent; color: root.cSurface }

    // ── 内容滚动区 ──────────────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: root.width
        contentHeight: col.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentY: root.captureOffset   // 探针分段截图用（真机上用户自己滚）
        Column {
            id: col
            x: 16
            width: root.width - 32
            spacing: 20

            // ═══ 顶栏（MD3 small top app bar）═══
            Item {
                width: parent.width; height: 64
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "星集控 · Material 3"
                    font.pixelSize: 22; font.weight: Font.Medium
                    color: root.cOnSurface
                }
                // 主题切换（MD3 icon button）
                Rectangle {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 48; height: 48; radius: 24
                    color: themeMa.containsMouse ? Qt.rgba(root.cOnSurface.r, root.cOnSurface.g, root.cOnSurface.b, root.kHover) : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: root.dark ? "☀" : "☾"
                        font.pixelSize: 20; color: root.cOnSurface
                    }
                    MouseArea {
                        id: themeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.toggleTheme()
                    }
                }
            }

            // ═══ 0 大标题区（hero）═══
            Column {
                width: parent.width; spacing: 4
                Text {
                    text: "学生端界面语言"
                    font.pixelSize: 32; font.weight: Font.Normal
                    color: root.cOnSurface
                }
                Text {
                    text: "Material 3 · 竖屏 · 深浅双主题 —— 评价样张"
                    font.pixelSize: 14; color: root.cOnSurfaceVar
                }
            }

            // ═══ 1 色板 ═══
            SectionTitle { text: "色彩 · Color" }
            Grid {
                columns: 5; columnSpacing: 8; rowSpacing: 8
                width: parent.width
                Repeater {
                    model: [
                        {c: "cPrimary", n: "Primary"}, {c: "cPrimaryCont", n: "P.Cont"},
                        {c: "cSecondary", n: "Secondary"}, {c: "cSecondaryCont", n: "S.Cont"},
                        {c: "cTertiary", n: "Tertiary"}, {c: "cTertiaryCont", n: "T.Cont"},
                        {c: "cError", n: "Error"}, {c: "cErrorCont", n: "E.Cont"},
                        {c: "cContHigh", n: "C.High"}, {c: "cContHighest", n: "C.Highest"}
                    ]
                    delegate: Column {
                        required property var modelData
                        width: 58
                        Rectangle {
                            width: 58; height: 44; radius: root.rS
                            color: root[modelData.c]
                            border.width: 1; border.color: root.cOutlineVar
                        }
                        Text {
                            text: modelData.n; font.pixelSize: 10
                            color: root.cOnSurfaceVar; anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }

            // ═══ 2 字阶 ═══
            SectionTitle { text: "字体排印 · Type scale" }
            Column {
                width: parent.width; spacing: 6
                Repeater {
                    model: [
                        {t: "Display Large 57", s: 34, w: Font.Normal,  tag: "display"},
                        {t: "Headline Medium 28", s: 26, w: Font.Normal,  tag: "headline"},
                        {t: "Title Large 22", s: 21, w: Font.Medium,  tag: "titleL"},
                        {t: "Title Medium 16", s: 15, w: Font.Medium,  tag: "titleM"},
                        {t: "Body Large 16", s: 15, w: Font.Normal,  tag: "bodyL"},
                        {t: "Body Medium 14", s: 13, w: Font.Normal,  tag: "bodyM"},
                        {t: "Label Large 14", s: 13, w: Font.Medium,  tag: "labelL"},
                        {t: "Label Medium 12", s: 11, w: Font.Medium,  tag: "labelM"}
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 12
                        Text {
                            text: modelData.t
                            font.pixelSize: modelData.s; font.weight: modelData.w
                            color: root.cOnSurface
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.tag
                            font.pixelSize: 10; font.family: "Consolas"
                            color: root.cOnSurfaceVar
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }

            // ═══ 3 按钮族 ═══
            SectionTitle { text: "按钮 · Buttons" }
            Flow {
                width: parent.width; spacing: 10
                MD3Button { label: "Filled"; variant: "filled" }
                MD3Button { label: "Tonal"; variant: "tonal" }
                MD3Button { label: "Elevated"; variant: "elevated" }
                MD3Button { label: "Outlined"; variant: "outlined" }
                MD3Button { label: "Text"; variant: "text" }
                MD3Button { label: "错误"; variant: "filled"; errorMode: true }
                MD3Button { label: "禁用"; variant: "filled"; enabled: false }
                MD3Button { label: "带图标"; variant: "tonal"; iconText: "▶" }
            }

            // FAB（MD3 primary container 圆角方形 + extended）
            Row {
                spacing: 12
                MD3Fab { iconText: "＋" }
                MD3Fab { label: "开始广播"; iconText: "▶"; extended: true }
            }

            // ═══ 4 选择控件 ═══
            SectionTitle { text: "选择 · Selection" }
            Column {
                width: parent.width; spacing: 4
                MD3SwitchRow { title: "允许语音对讲"; subtitle: "关闭后老师无法呼叫本机" }
                MD3SwitchRow { title: "考试模式"; subtitle: "由云端强制下发时不可本地拒绝"; checked: true }
                MD3SwitchRow { title: "摄像头"; subtitle: "已授权班级：高一1班"; checked: false; enabled: true }
                Row {
                    spacing: 16
                    MD3CheckRow { text: "自动更新" ; checked: true}
                    MD3CheckRow { text: "加入体验计划" }
                    MD3RadioRow { text: "静默安装"; checked: true; group: "inst" }
                    MD3RadioRow { text: "询问我"; group: "inst" }
                }
                Flow {
                    width: parent.width; spacing: 8
                    MD3Chip { text: "全部班级"; selected: true }
                    MD3Chip { text: "高一1班" }
                    MD3Chip { text: "高一2班"; leading: "✓" }
                    MD3Chip { text: "已停用"; enabled: false }
                }
            }

            // ═══ 5 输入框 ═══
            SectionTitle { text: "输入 · Text fields" }
            Column {
                width: parent.width; spacing: 16
                MD3TextField { placeholder: "班级接入码"; helper: "形如 XJK-XXXX-XXX，由管理员发放" }
                MD3TextField { placeholder: "搜索设备"; supportingIcon: "🔍" }
                MD3TextField { placeholder: "设备名"; value: "class_xlzx_2028_08-03" }
                MD3TextField { placeholder: "端口"; value: "8788"; errorMode: true; errorText: "仅允许数字（示例错误态）" }
            }

            // ═══ 6 列表 ═══
            SectionTitle { text: "列表 · Lists" }
            MD3Card {
                width: parent.width
                Column {
                    width: parent.width
                    MD3ListItem {
                        headline: "高一1班 · 03号机"; supporting: "在线 · 画面 12fps · 延迟 34ms"
                        leading: "🖥"; trailingSwitch: true; trailingOn: true
                    }
                    MD3Divider { }
                    MD3ListItem {
                        headline: "高一2班 · 07号机"; supporting: "离线 · 最后心跳 2 分钟前"
                        leading: "🖥"; trailingText: "离线"; trailingWarn: true
                    }
                    MD3Divider { }
                    MD3ListItem {
                        headline: "正在播放：眼保健操"; supporting: "来源：广播站 · 来自 排制播控"
                        leading: "📢"; trailingText: "播放中"
                    }
                }
            }

            // ═══ 7 卡片三态 ═══
            SectionTitle { text: "卡片 · Cards" }
            Row {
                spacing: 12
                MD3Card {
                    width: (root.width - 32 - 24) / 3; height: 96
                    Column {
                        x: 12; y: 10; spacing: 2
                        Text { text: "Elevated"; font.pixelSize: 13; font.weight: Font.Medium; color: root.cOnSurface }
                        Text { text: "高度层 1"; font.pixelSize: 11; color: root.cOnSurfaceVar }
                    }
                }
                MD3Card {
                    variant: "filled"
                    width: (root.width - 32 - 24) / 3; height: 96
                    Column {
                        x: 12; y: 10; spacing: 2
                        Text { text: "Filled"; font.pixelSize: 13; font.weight: Font.Medium; color: root.cOnSurface }
                        Text { text: "surface 容器"; font.pixelSize: 11; color: root.cOnSurfaceVar }
                    }
                }
                MD3Card {
                    variant: "outlined"
                    width: (root.width - 32 - 24) / 3; height: 96
                    Column {
                        x: 12; y: 10; spacing: 2
                        Text { text: "Outlined"; font.pixelSize: 13; font.weight: Font.Medium; color: root.cOnSurface }
                        Text { text: "描边容器"; font.pixelSize: 11; color: root.cOnSurfaceVar }
                    }
                }
            }

            // ═══ 8 底部导航 ═══
            SectionTitle { text: "底部导航 · Navigation bar" }
            MD3NavBar {
                width: parent.width
                model: [
                    {icon: "🏫", label: "班级"},
                    {icon: "🖥", label: "设备"},
                    {icon: "📢", label: "播控", badge: 3},
                    {icon: "⚙", label: "设置"}
                ]
            }

            // ═══ 9 对话框 / 消息条触发 ═══
            SectionTitle { text: "反馈 · Dialog & Snackbar" }
            Row {
                spacing: 12
                MD3Button { label: "打开对话框"; variant: "tonal"; onClicked: dlg.open() }
                MD3Button { label: "弹 Snackbar"; variant: "outlined"; onClicked: snack.show("已保存到 %LOCALAPPDATA%\\xingjikong") }
            }
            Item { width: 1; height: 8 }
        }
    }

    // ── MD3 对话框（basic dialog + scrim + 28 圆角）─────────────────────────
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.32)
        visible: false
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: root.durMed; easing.type: Easing.Bezier; easing.bezierCurve: root.easingStandard } }
        onVisibleChanged: opacity = visible ? 1 : 0

        Rectangle {
            anchors.centerIn: parent
            width: root.width - 64; height: 220
            radius: root.rXL; color: root.cContHigh
            Column {
                x: 24; y: 24; width: parent.width - 48; spacing: 8
                Text { text: "结束本节网课？"; font.pixelSize: 22; font.weight: Font.Medium; color: root.cOnSurface }
                Text {
                    text: "将向高一1班全部 3 台设备下发「解锁」。考试模式下该操作会被云端拒绝。"
                    font.pixelSize: 14; color: root.cOnSurfaceVar
                    wrapMode: Text.WordWrap; width: parent.width
                }
                Row {
                    anchors.right: parent.right; spacing: 8
                    MD3Button { label: "取消"; variant: "text"; onClicked: dlg.close() }
                    MD3Button { label: "确认结束"; variant: "filled"; onClicked: dlg.close() }
                }
            }
        }
        MouseArea { anchors.fill: parent; onClicked: dlg.close() }
    }
    QtObject {
        id: dlg
        function open() { scrim.visible = true }
        function close() { scrim.visible = false }
    }

    // ── MD3 Snackbar（inverse surface + action）────────────────────────────
    Rectangle {
        id: snack
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        width: snackText.implicitWidth + (snackAct.visible ? snackAct.width + 24 : 24) + 24
        height: 48; radius: root.rXS
        color: root.cInverseSurf
        visible: false
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingStandard } }
        Timer { id: snackTimer; interval: 3000; onTriggered: snack.hide() }
        function show(t) {
            snackText.text = t; visible = true; opacity = 1
            snackTimer.restart()
        }
        function hide() { opacity = 0; visible = false }
        Text {
            id: snackText
            anchors.verticalCenter: parent.verticalCenter; x: 16
            font.pixelSize: 14; color: root.cInverseOn
            maximumLineCount: 1; elide: Text.ElideRight
            width: Math.min(implicitWidth, root.width - 32 - snackAct.width - 32)
        }
        Text {
            id: snackAct
            anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 16
            text: "撤销"; font.pixelSize: 14; font.weight: Font.Medium
            color: root.dark ? root.cPrimaryCont : "#D0BCFF"
            MouseArea { anchors.fill: parent; onClicked: snack.hide() }
        }
    }

    // ══════════════════════ 内部组件 ══════════════════════

    // 节标题（MD3 title small + on-surface-variant）
    component SectionTitle: Text {
        font.pixelSize: 14; font.weight: Font.Medium
        color: root.cOnSurfaceVar
    }

    // MD3 通用按钮（filled/tonal/elevated/outlined/text + 状态层 + 图标）
    component MD3Button: Rectangle {
        property string label: ""
        property string variant: "filled"
        property string iconText: ""
        property bool errorMode: false
        signal clicked()
        height: 40; radius: 20
        implicitWidth: btnText.implicitWidth + 48
        color: {
            if (!enabled) return variant === "filled" ? Qt.rgba(root.cOnSurface.r, root.cOnSurface.g, root.cOnSurface.b, 0.12)
                               : (variant === "text" || variant === "outlined" ? "transparent" : root.cCont)
            switch (variant) {
            case "filled":   return errorMode ? root.cError : root.cPrimary
            case "tonal":    return root.cSecondaryCont
            case "elevated": return root.cContLow
            default:         return "transparent"
            }
        }
        border.width: variant === "outlined" ? 1 : 0
        border.color: errorMode ? root.cError : root.cOutline
        opacity: enabled ? 1 : 0.38

        Text {
            id: btnText
            anchors.centerIn: parent
            text: (iconText !== "" ? iconText + "  " : "") + label
            font.pixelSize: 14; font.weight: Font.Medium
            color: {
                if (!enabled) return root.cOnSurface
                switch (variant) {
                case "filled":   return errorMode ? root.cOnError : root.cOnPrimary
                case "tonal":    return root.cOnSecondaryCont
                case "elevated": return root.cPrimary
                default:         return errorMode ? root.cError : root.cPrimary
                }
            }
        }
        // 状态层（hover/press 覆盖整钮）
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: {
                const b = (variant === "filled" && !errorMode) ? root.cOnPrimary
                        : errorMode ? root.cOnError : root.cOnSurface
                if (ma.pressed) return Qt.rgba(b.r, b.g, b.b, root.kPress)
                if (ma.containsMouse) return Qt.rgba(b.r, b.g, b.b, root.kHover)
                return "transparent"
            }
            Behavior on color { ColorAnimation { duration: root.durShort } }
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }

    // MD3 FAB（primary container + 16 圆角；extended 带标签）
    component MD3Fab: Rectangle {
        property string iconText: "＋"
        property string label: ""
        property bool extended: false
        width: extended ? innerRow.implicitWidth + 32 : 56
        height: 56; radius: root.rL
        color: root.cPrimaryCont
        Behavior on width { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingStandard } }
        Row {
            id: innerRow
            anchors.centerIn: parent; spacing: 8
            Text { text: iconText; font.pixelSize: 22; color: root.cOnPrimaryCont }
            Text {
                text: label; font.pixelSize: 14; font.weight: Font.Medium
                color: root.cOnPrimaryCont
                visible: label !== ""
            }
        }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: ma.pressed ? Qt.rgba(root.cOnPrimaryCont.r, root.cOnPrimaryCont.g, root.cOnPrimaryCont.b, root.kPress)
                  : ma.containsMouse ? Qt.rgba(root.cOnPrimaryCont.r, root.cOnPrimaryCont.g, root.cOnPrimaryCont.b, root.kHover)
                  : "transparent"
            Behavior on color { ColorAnimation { duration: root.durShort } }
        }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true }
    }

    // MD3 开关行（list tile + switch，switch 轨道 52×32、滑块 24/16）
    component MD3SwitchRow: Item {
        property string title: ""
        property string subtitle: ""
        property bool checked: false
        signal toggled(bool on)
        width: parent.width; height: subtitle === "" ? 56 : 72
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 76
            Text { text: title; font.pixelSize: 16; color: root.cOnSurface }
            Text {
                text: subtitle; font.pixelSize: 12; color: root.cOnSurfaceVar
                visible: subtitle !== ""
            }
        }
        // switch
        Item {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 52; height: 32
            Rectangle {
                anchors.fill: parent; radius: 16
                color: checked ? root.cPrimary : root.cSurfaceVar
                border.width: checked ? 0 : 2
                border.color: root.cOutline
                Behavior on color { ColorAnimation { duration: root.durShort } }
            }
            Rectangle {
                x: checked ? 24 : 6
                anchors.verticalCenter: parent.verticalCenter
                width: checked ? 24 : 16
                height: checked ? 24 : 16
                radius: width / 2
                color: checked ? root.cOnPrimary : root.cOutline
                Behavior on x { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingEmphasized } }
                Behavior on width { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingEmphasized } }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: { checked = !checked; toggled(checked) }
            }
        }
    }

    // MD3 复选框
    component MD3CheckRow: Item {
        property string text: ""
        property bool checked: false
        signal toggled(bool on)
        height: 40
        Row {
            anchors.fill: parent; spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18; radius: 2
                color: checked ? root.cPrimary : "transparent"
                border.width: checked ? 0 : 2; border.color: root.cOnSurfaceVar
                Text {
                    anchors.centerIn: parent
                    text: "✓"; font.pixelSize: 12; color: root.cOnPrimary
                    visible: checked
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: parent.parent.text; font.pixelSize: 14; color: root.cOnSurface
            }
        }
        MouseArea { anchors.fill: parent; onClicked: { checked = !checked; toggled(checked) } }
    }

    // MD3 单选（同组圆点）
    component MD3RadioRow: Item {
        property string text: ""
        property bool checked: false
        property string group: ""
        height: 40
        Row {
            anchors.fill: parent; spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20; radius: 10
                color: "transparent"
                border.width: checked ? 0 : 2; border.color: root.cOnSurfaceVar
                Rectangle {
                    anchors.centerIn: parent
                    width: 10; height: 10; radius: 5
                    color: root.cPrimary
                    visible: checked
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: parent.parent.text; font.pixelSize: 14; color: root.cOnSurface
            }
        }
        MouseArea { anchors.fill: parent; onClicked: checked = true }
    }

    // MD3 Chip（assist/filter 两态）
    component MD3Chip: Rectangle {
        property string text: ""
        property bool selected: false
        property string leading: ""
        signal clicked()
        height: 32; radius: 8
        implicitWidth: chipRow.implicitWidth + (selected ? 32 : 16)
        color: selected ? root.cSecondaryCont : "transparent"
        border.width: selected ? 0 : 1; border.color: root.cOutline
        opacity: enabled ? 1 : 0.38
        Row {
            id: chipRow
            anchors.centerIn: parent; spacing: 6
            Text {
                text: selected ? "✓" : leading
                font.pixelSize: 14; color: root.cOnSecondaryCont
                visible: text !== ""
            }
            Text { text: parent.parent.text; font.pixelSize: 14; font.weight: Font.Medium; color: selected ? root.cOnSecondaryCont : root.cOnSurfaceVar }
        }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: ma.pressed ? Qt.rgba(root.cOnSurface.r, root.cOnSurface.g, root.cOnSurface.b, root.kPress) : "transparent"
        }
        MouseArea { id: ma; anchors.fill: parent; onClicked: { selected = !selected; parent.clicked() } }
    }

    // MD3 文本框（filled + floating label + 支持/错误文本）
    component MD3TextField: Item {
        property string placeholder: ""
        property string value: ""
        property string helper: ""
        property string errorText: ""
        property bool errorMode: false
        property string supportingIcon: ""
        width: parent.width; height: 68
        Rectangle {
            anchors.top: parent.top
            width: parent.width; height: 56; radius: root.rS
            color: root.cContHighest
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: input.activeFocus ? Qt.rgba(root.cPrimary.r, root.cPrimary.g, root.cPrimary.b, 0.06) : "transparent"
            }
            Text {
                id: floatingLabel
                anchors.left: parent.left; anchors.leftMargin: 16
                y: (input.activeFocus || input.text !== "") ? 8 : 18
                font.pixelSize: (input.activeFocus || input.text !== "") ? 11 : 16
                color: errorMode ? root.cError : (input.activeFocus ? root.cPrimary : root.cOnSurfaceVar)
                text: placeholder
                Behavior on y { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingStandard } }
                Behavior on font.pixelSize { NumberAnimation { duration: root.durShort } }
            }
            TextInput {
                id: input
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                text: value
                font.pixelSize: 16; color: root.cOnSurface
                selectionColor: root.cPrimaryCont; selectedTextColor: root.cOnPrimaryCont
                clip: true
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: supportingIcon; font.pixelSize: 18; color: root.cOnSurfaceVar
                visible: supportingIcon !== ""
            }
            // 聚焦底线（MD3 2dp）
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: input.activeFocus ? 2 : 1
                color: input.activeFocus ? (errorMode ? root.cError : root.cPrimary) : root.cOutlineVar
                Behavior on height { NumberAnimation { duration: root.durShort } }
            }
        }
        Text {
            anchors.top: parent.top; anchors.topMargin: 60; anchors.left: parent.left; anchors.leftMargin: 16
            text: errorMode ? errorText : helper
            font.pixelSize: 12
            color: errorMode ? root.cError : root.cOnSurfaceVar
            visible: text !== ""
        }
    }

    // MD3 列表项（leading 图标 + 两行 + trailing）
    component MD3ListItem: Item {
        property string headline: ""
        property string supporting: ""
        property string leading: ""
        property string trailingText: ""
        property bool trailingWarn: false
        property bool trailingSwitch: false
        property bool trailingOn: false
        width: parent.width; height: 72
        Rectangle {
            anchors.centerIn: parent
            width: 40; height: 40; radius: 20
            color: root.cPrimaryCont
            Text { anchors.centerIn: parent; text: leading; font.pixelSize: 18 }
        }
        Column {
            anchors.left: parent.left; anchors.leftMargin: 56
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 56 - (trailingSwitch ? 76 : 80)
            Text {
                text: headline; font.pixelSize: 16; color: root.cOnSurface
                elide: Text.ElideRight
            }
            Text {
                text: supporting; font.pixelSize: 12; color: root.cOnSurfaceVar
                elide: Text.ElideRight; visible: supporting !== ""
            }
        }
        Text {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: trailingText; font.pixelSize: 12; font.weight: Font.Medium
            color: trailingWarn ? root.cError : root.cPrimary
            visible: trailingText !== "" && !trailingSwitch
        }
        Item {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 52; height: 32
            visible: trailingSwitch
            Rectangle {
                anchors.fill: parent; radius: 16
                color: trailingOn ? root.cPrimary : root.cSurfaceVar
                border.width: trailingOn ? 0 : 2; border.color: root.cOutline
            }
            Rectangle {
                x: trailingOn ? 24 : 6
                anchors.verticalCenter: parent.verticalCenter
                width: trailingOn ? 24 : 16; height: width
                radius: width / 2
                color: trailingOn ? root.cOnPrimary : root.cOutline
                Behavior on x { NumberAnimation { duration: root.durShort; easing.type: Easing.Bezier; easing.bezierCurve: root.easingEmphasized } }
            }
            MouseArea { anchors.fill: parent; onClicked: trailingOn = !trailingOn }
        }
    }

    // 分隔线
    component MD3Divider: Rectangle {
        width: parent.width; height: 1; color: root.cOutlineVar
    }

    // MD3 卡片（elevated/filled/outlined）
    component MD3Card: Rectangle {
        property string variant: "elevated"
        radius: root.rM
        color: variant === "filled" ? root.cContHighest
             : variant === "outlined" ? root.cSurface
             : root.cContLow
        border.width: variant === "outlined" ? 1 : 0
        border.color: root.cOutlineVar
        implicitHeight: children.length > 0 ? children[0].height + 32 : 96
    }

    // MD3 底部导航（80 高 + 激活指示器 pill 64×32）
    component MD3NavBar: Item {
        id: navRoot
        property var model: []
        property int current: 0
        width: parent.width; height: 80
        Rectangle {
            anchors.fill: parent; color: root.cCont
        }
        Row {
            anchors.centerIn: parent
            spacing: 0
            Repeater {
                model: navRoot.model
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: (root.width - 32) / 4; height: 80
                    // 激活指示器（pill）
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 12
                        width: 64; height: 32; radius: 16
                        color: navRoot.current === index ? root.cSecondaryCont : "transparent"
                        Behavior on color { ColorAnimation { duration: root.durShort } }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.icon
                            font.pixelSize: 18
                        }
                        // badge
                        Rectangle {
                            visible: modelData.badge !== undefined
                            anchors.right: parent.right; anchors.top: parent.top
                            width: 16; height: 16; radius: 8
                            color: root.cError
                            Text {
                                anchors.centerIn: parent
                                text: modelData.badge !== undefined ? modelData.badge : ""
                                font.pixelSize: 9; color: root.cOnError; font.weight: Font.Bold
                            }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 48
                        text: modelData.label
                        font.pixelSize: 12; font.weight: Font.Medium
                        color: navRoot.current === index ? root.cOnSurface : root.cOnSurfaceVar
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: navRoot.current = index
                    }
                }
            }
        }
    }
}
