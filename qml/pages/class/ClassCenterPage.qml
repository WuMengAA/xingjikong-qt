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
import "../../components/theme"       // Md3 令牌
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
                color: Md3.cOnSurface
                font.pixelSize: 24
                font.bold: true
            }
            // 概况文本由 C++ 拼（班级数/设备数/在线数），QML 不做字符串拼接
            Text {
                text: classCenter.classSummaryText
                color: Md3.cOnSurfaceVar
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
            color: "#EF4444"
            clip: true

            // 进度：宽度 = 按钮宽 × C++ 给的比例（比例是结果值，不是判断）
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: emergencyBtn.width * classCenter.emergencyHoldProgress
                color: "#7F1D1D"
            }
            Text {
                anchors.centerIn: parent
                text: "按住 3 秒 · 紧急通知"
                color: "#FFFFFF"
                font.pixelSize: 13
                font.bold: true
            }
            MouseArea {
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
            color: Md3.cSurface
            border.width: 1
            border.color: Md3.cOutlineVar

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
                            strokeColor: Md3.cOutlineVar
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
                            strokeColor: "#22C55E"
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
                        color: Md3.cOnSurface
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: "设备在线率"
                        color: Md3.cOnSurface
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        text: classCenter.statSummaryText
                        color: Md3.cOnSurfaceVar
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
            color: Md3.cSurface
            border.width: 1
            border.color: Md3.cOutlineVar
            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 8
                Text {
                    text: "最近异常"
                    color: "#EF4444"
                    font.pixelSize: 16
                    font.bold: true
                }
                // 整行文案（时间 · 班级 · 事件）由 C++ 拼好，QML 只显示 modelData.line
                Repeater {
                    model: classCenter.anomalies
                    delegate: Text {
                        text: modelData.line
                        color: Md3.cOnSurfaceVar
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                // 空态：说清"没有异常"，不是一片空白（否则老师以为面板坏了）
                Text {
                    visible: !classCenter.hasAnomalies
                    text: classCenter.anomalyEmptyText
                    color: Md3.cOutline
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
        color: Md3.cOnSurface
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
            color: Md3.cSurface
            border.width: 1
            border.color: Md3.cOutlineVar

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
                        color: Md3.cOnSurface
                        font.pixelSize: 20
                        font.bold: true
                    }
                    // 状态点颜色由 C++ 给（modelData.dot），QML 不做 online>0 判断
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12; height: 12
                        radius: 6
                        color: modelData.dot
                        opacity: modelData.dotOpacity
                    }
                }
                Text {
                    text: modelData.summaryText
                    color: Md3.cOnSurfaceVar
                    font.pixelSize: 13
                }
            }

            // 果冻曲线与 hover 描边：用 State 表达，不用三元
            scale: 1.0
            Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
            states: [
                State {
                    name: "pressed"
                    when: ma.pressed
                    PropertyChanges { target: card; scale: 0.96; border.color: "#22C55E" }
                },
                State {
                    name: "hovered"
                    when: ma.containsMouse && !ma.pressed
                    PropertyChanges { target: card; scale: 1.03; border.color: "#22C55E" }
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
            color: Md3.cOnSurface
            font.pixelSize: 15
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: classCenter.classSummaryText
            color: Md3.cOutline
            font.pixelSize: 13
        }
    }

    // ── 下发结果条（点完按钮要说话：成功绿/失败红由 C++ 给色）────────
    Rectangle {
        anchors.left: parent.left; anchors.leftMargin: 32
        anchors.bottom: parent.bottom; anchors.bottomMargin: 32
        height: 32
        width: resultText.implicitWidth + 28
        radius: 16
        color: Md3.cSurface
        border.width: 1
        border.color: classCenter.lastOpColor
        visible: classCenter.lastOpText.length > 0
        opacity: 0.96
        Text {
            id: resultText
            anchors.centerIn: parent
            text: classCenter.lastOpText
            color: classCenter.lastOpColor
            font.pixelSize: 12
        }
    }

    // ── 遮罩（透明度由 C++ 给）────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: classCenter.overlayOpacity
        visible: classCenter.drawerOpen
        Behavior on opacity { NumberAnimation { duration: 300 } }
        MouseArea { anchors.fill: parent; onClicked: classCenter.closeDrawer() }
    }

    // ── 右侧抽屉：班级设备面板 ────────────────────────────────────
    ClassDeviceDrawer {
        id: drawer
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: classCenter.drawerWidth
        x: classCenter.drawerX
        Behavior on x { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
        onCloseDrawer: classCenter.closeDrawer()
        onEnterRemote: classCenter.enterRemote()
    }

    // ── 远程桌面（可见性由 C++ 给）────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: classCenter.remoteVisible
        color: Md3.cSurface
        RemoteDesktopStage {
            anchors.fill: parent
            anchors.margins: 24
        }
    }
}
