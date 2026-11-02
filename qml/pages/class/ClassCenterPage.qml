// 班级中心（模块一 · 1）—— 全局概览，纯声明式，零逻辑
//
// 2026-10-28 B0/B1：本件**不含任何逻辑**。
//   · 数据（班级/异常/设备）来自 C++ `classCenter`（真数据来自 /api/classes /api/devices
//     /api/events，由 main.cpp 推进控制器）；
//   · 在线率、圆饼弧端点、网格列宽、卡片宽度、抽屉 x 与遮罩透明度、视图可见性
//     全部是 C++ 算好的结果属性，本件只绑定；
//   · 本件无 Math.*、无三元、无 JS function、无 console.log、无占位数据数组、无通信代码。
//
// 蓝图：主视图网格阵列；顶部左侧/居中统计圆饼（在线率）、右侧最近异常面板；
//       点班级卡片 → 右侧滑出 Drawer → 点缩略图进远程桌面。
//       交互细节：紧急通知**长按 3 秒**发送（进度由 C++ 计时，QML 只显示）。
import QtQuick 2.15
import QtQuick.Shapes 1.15
import "../../components/theme"
import "../../island"                 // SvgIcon

Item {
    id: page

    // 视口尺寸与可见性回写给 C++（声明式 Binding，不写 JS）。
    // pageActive 决定控制器是否开 15s 轮询 —— 不可见还轮询是白烧云端与设备。
    Binding { target: classCenter; property: "viewWidth";  value: page.width }
    Binding { target: classCenter; property: "viewHeight"; value: page.height }
    Binding { target: classCenter; property: "pageActive"; value: page.visible }

    // ── 头部 ─────────────────────────────────────────────────────
    Item {
        id: header
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top
        height: 76
        Column {
            anchors.left: parent.left; anchors.leftMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                text: "班级设备总览"
                color: Theme.color.textPrimary
                font.pixelSize: 24
                font.bold: true
            }
            // 概况文本由 C++ 拼（班级数/设备数/在线数），QML 不做字符串拼接
            Text {
                text: classCenter.classSummaryText
                color: Theme.color.textSecondary
                font.pixelSize: 13
            }
        }

        // 紧急通知：**长按 3 秒**发送（原型：进度条填充 3s linear，松手回弹 0.2s）
        // 计时与"到点就发"都在 C++（beginEmergencyHold / cancelEmergencyHold），
        // 本件只报按下/松手、只显示进度比例。
        Rectangle {
            id: emergencyBtn
            anchors.right: parent.right; anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            width: 176; height: 40
            radius: 20
            color: classCenter.dangerBase
            clip: true

            // 按住时轻微内缩：长按控件必须有"按到了"的反馈，否则用户不知道计时有没开始。
            // 只做 scale（不碰内部进度条宽度 —— 那是 3s linear，加 Behavior 会把线性变成缓动）。
            scale: 1.0
            Behavior on scale { NumberAnimation { duration: Theme.motion.fast } }
            states: State {
                name: "held"
                when: holdArea.pressed
                PropertyChanges { target: emergencyBtn; scale: 0.97 }
            }

            // 进度：宽度 = 按钮宽 × C++ 给的比例（比例是结果值，不是判断）
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: emergencyBtn.width * classCenter.emergencyHoldProgress
                color: classCenter.dangerShade
            }
            Text {
                anchors.centerIn: parent
                text: "按住 3 秒 · 紧急通知"
                color: classCenter.dangerFg
                font.pixelSize: 13
                font.bold: true
            }
            MouseArea {
                id: holdArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onPressed: classCenter.beginEmergencyHold()
                onReleased: classCenter.cancelEmergencyHold()
                onCanceled: classCenter.cancelEmergencyHold()
            }
        }
    }

    // ── 顶部统计区 ───────────────────────────────────────────────
    Row {
        id: stats
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 32; anchors.rightMargin: 32
        anchors.top: header.bottom
        height: 128
        spacing: 24

        // ① 在线率圆饼（1/3）：宽度由 C++ 算（QML 不写 (w-24)/3）
        Rectangle {
            width: classCenter.statCardNarrowW
            height: 128
            radius: 24
            color: Theme.color.surface
            border.width: 0
            border.color: Theme.color.textSecondary

            Row {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 24
                Item {
                    width: 80; height: 80
                    anchors.verticalCenter: parent.verticalCenter

                    // 底环（灰）
                    Shape {
                        anchors.fill: parent
                        ShapePath {
                            strokeColor: Theme.color.textSecondary
                            strokeWidth: 8
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            startX: 40; startY: 8
                            PathArc { x: 40; y: 72; radiusX: 32; radiusY: 32; useLargeArc: true;  direction: PathArc.Clockwise }
                            PathArc { x: 40; y: 8;  radiusX: 32; radiusY: 32; useLargeArc: true;  direction: PathArc.Clockwise }
                        }
                    }
                    // 在线弧（绿）：端点由 C++ 算好，QML 不出现 sin/cos
                    Shape {
                        anchors.fill: parent
                        ShapePath {
                            strokeColor: Theme.color.success
                            strokeWidth: 8
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            startX: 40; startY: 8
                            PathArc {
                                x: classCenter.donutEndX
                                y: classCenter.donutEndY
                                radiusX: 32; radiusY: 32
                                useLargeArc: classCenter.donutLargeArc
                                direction: PathArc.Clockwise
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: classCenter.onlinePercentText
                        color: Theme.color.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "设备在线率"
                        color: Theme.color.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        text: classCenter.statSummaryText
                        color: Theme.color.textSecondary
                        font.pixelSize: 13
                    }
                }
            }
        }

        // ② 最近异常面板（2/3）
        Rectangle {
            width: classCenter.statCardWideW
            height: 128
            radius: 24
            color: Theme.color.surface
            border.width: 0
            border.color: Theme.color.textSecondary
            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 8
                Text {
                    text: "最近异常"
                    color: Theme.color.destructive
                    font.pixelSize: 16
                    font.bold: true
                }
                // 整行文案（时间 · 班级 · 事件）由 C++ 拼好，QML 只显示 modelData.line
                Repeater {
                    model: classCenter.anomalies
                    delegate: Text {
                        text: modelData.line
                        color: Theme.color.textSecondary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                // 空态：说清"没有异常"，不是一片空白（否则老师以为面板坏了）
                Text {
                    visible: !classCenter.hasAnomalies
                    text: classCenter.anomalyEmptyText
                    color: Theme.color.textSecondary
                    font.pixelSize: 13
                }
            }
        }
    }

    // ── 班级阵列 ─────────────────────────────────────────────────
    Text {
        id: gridTitle
        anchors.left: parent.left; anchors.leftMargin: 32
        anchors.top: stats.bottom; anchors.topMargin: 24
        text: "班级阵列"
        color: Theme.color.textPrimary
        font.pixelSize: 16
        font.bold: true
    }

    GridView {
        id: grid
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 32; anchors.rightMargin: 32
        anchors.top: gridTitle.bottom; anchors.topMargin: 12
        anchors.bottom: parent.bottom; anchors.bottomMargin: 24
        clip: true
        model: classCenter.classes
        cellWidth: classCenter.gridCellWidth
        cellHeight: 132

        delegate: Rectangle {
            id: card
            width: grid.cellWidth - 16
            height: 116
            radius: 20
            color: Theme.color.surface
            border.width: 0
            border.color: Theme.color.textSecondary

            // 果冻曲线（原型 .jelly-effect）：
            //   hover  scale(1.05) —— 400ms cubic-bezier(.34,1.7,.64,1) ⇒ Easing.OutBack（overshoot）
            //   press  scale(0.90)
            scale: 1.0
            Behavior on scale {
                NumberAnimation { duration: Theme.motion.slow; easing.type: Easing.OutBack }
            }

            // hover 描边层：原型 hover:border-green-200。
            // ⚠️ QML 无法对 grouped property（border.color）加 Behavior，
            //    所以另起一层只动 opacity —— 切色有过渡，且不触发 "Property value set multiple times"。
            Rectangle {
                id: hoverRing
                anchors.fill: parent
                anchors.margins: -1
                radius: card.radius + 1
                color: "transparent"
                border.width: 0
                border.color: Theme.color.accent
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 8
                Item {
                    width: parent.width
                    height: 26
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.color.textPrimary
                        font.pixelSize: 20
                        font.bold: true
                    }
                    // 状态点颜色由 C++ 给（modelData.dot），QML 不做 online>0 判断。
                    // 外圈微光是原型的 shadow-[0_0_8px_rgba(...)] 的等价物：
                    // Qt 没有 drop-shadow，用「同色半透明放大圈」近似，不引额外模块。
                    Item {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12; height: 12
                        Rectangle {
                            anchors.centerIn: parent
                            width: 20; height: 20
                            radius: 10
                            color: modelData.dot
                            opacity: modelData.dotOpacity * 0.25
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 12; height: 12
                            radius: 6
                            color: modelData.dot
                            opacity: modelData.dotOpacity
                        }
                    }
                }
                Text {
                    text: modelData.summaryText
                    color: Theme.color.textSecondary
                    font.pixelSize: 13
                }
            }

            // 果冻曲线与 hover 描边：用 State 表达，不用三元
            states: [
                State {
                    name: "pressed"
                    when: ma.pressed
                    PropertyChanges { target: card; scale: Theme.motion.pressScale }
                    PropertyChanges { target: hoverRing; opacity: 1 }
                },
                State {
                    name: "hovered"
                    when: ma.containsMouse && !ma.pressed
                    PropertyChanges { target: card; scale: Theme.motion.hoverScale }
                    PropertyChanges { target: hoverRing; opacity: 1 }
                }
            ]
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // 选中该班 + 展开抽屉（两件事由 C++ 的 openClass 一起做）
                onClicked: classCenter.openClass(modelData.code)
            }
        }
    }

    // 班级阵列空态（没拉到数据 / 我这个身份没带班）
    Column {
        anchors.centerIn: grid
        spacing: 6
        visible: !classCenter.hasClasses
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "还没有班级数据"
            color: Theme.color.textPrimary
            font.pixelSize: 15
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: classCenter.classSummaryText
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
    }

    // ── 下发结果条（点完按钮要说话：成功绿/失败红由 C++ 给色）────────
    // 出现/消失走淡入淡出（原型是瞬时插入，但瞬时会有"闪一下"的观感，这里补过渡）。
    // ⚠️ 不用 `visible` 硬切（关闭时看不到淡出），也不用三元（铁律），改用 State。
    Rectangle {
        id: resultBar
        anchors.left: parent.left; anchors.leftMargin: 32
        anchors.bottom: parent.bottom; anchors.bottomMargin: 32
        height: 32
        width: resultText.implicitWidth + 28
        radius: 16
        color: Theme.color.surface
        border.width: 0
        border.color: classCenter.lastOpColor
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        states: State {
            name: "shown"
            when: classCenter.lastOpText.length > 0
            PropertyChanges { target: resultBar; opacity: 0.96 }
        }
        Text {
            id: resultText
            anchors.centerIn: parent
            text: classCenter.lastOpText
            color: classCenter.lastOpColor
            font.pixelSize: 12
        }
    }

    // ── 遮罩（透明度由 C++ 给）────────────────────────────────────
    // 原型：bg-black/20 + backdrop-blur-sm，transition: opacity 300ms ease。
    // 不用 visible 硬切 —— 那会在关闭时"啪"一下消失，看不到淡出；
    // 鼠标拦截改由 MouseArea.enabled 控制（opacity 归零后不再吃掉点击）。
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: classCenter.overlayOpacity
        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutQuad }
        }
        MouseArea {
            anchors.fill: parent
            enabled: classCenter.drawerOpen
            onClicked: classCenter.closeDrawer()
        }
    }

    // ── 右侧抽屉：班级设备面板 ────────────────────────────────────
    // 原型：transition: transform 400ms cubic-bezier(.2,.8,.2,1) ⇒ Easing.OutCubic
    ClassDeviceDrawer {
        id: drawer
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: classCenter.drawerWidth
        x: classCenter.drawerX
        Behavior on x {
            NumberAnimation { duration: Theme.motion.slow; easing.type: Easing.OutCubic }
        }
        onCloseDrawer: classCenter.closeDrawer()
        onEnterRemote: classCenter.enterRemote()
    }

    // ── 远程桌面（可见性由 C++ 给）────────────────────────────────
    // 进入补一段淡入 + 轻微放大：网格 ↔ 远程桌面是**页面级**切换，
    // 硬切会让人觉得"画面闪了一下"（原型没画这段，属缺啥补啥）。
    Rectangle {
        id: remoteLayer
        anchors.fill: parent
        visible: classCenter.remoteVisible
        color: Theme.color.surface
        opacity: 0
        scale: 0.98
        states: State {
            name: "shown"
            when: classCenter.remoteVisible
            PropertyChanges { target: remoteLayer; opacity: 1.0; scale: 1.0 }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutQuad }
        }
        Behavior on scale {
            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutCubic }
        }
        RemoteDesktopStage {
            anchors.fill: parent
            anchors.margins: 24
        }
    }
}
