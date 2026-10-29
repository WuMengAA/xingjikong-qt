// 远程桌面 · 右侧「操作面板」（9:16 比例区）—— 纯声明式，零逻辑
//
// 2026-10-28 B0 重构：
//   · 所有配色/角度/透明度/渐变半径均绑定 C++ `classCenter` 的属性，本件不判断、不计算。
//   · 每个按钮点什么都**由父级显式给出**（通知用 notify*、监控用 monitor*），
//     不靠 QML 里的 theme 三元切换 ⇒ 零三元。
//   · 点击只把操作名交给 C++（classCenter.requestOp），指令映射在 C++（B1）。
//
// ⚠️ 布局纪律：纵向面板必须进 Flickable，否则按钮落到可视区外会表现为「按不动」。
import QtQuick 2.15
import "../../components/theme"       // Md3 令牌
import "../../island"                 // SvgIcon

Item {
    id: panel

    Text {
        id: title
        anchors.left: parent.left
        anchors.top: parent.top
        text: "操作面板"
        color: Theme.color.textPrimary
        font.pixelSize: 14
        font.bold: true
    }

    // MD 风格分割线
    Rectangle {
        id: sep
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.topMargin: 8
        height: 1
        color: Theme.color.textSecondary
        opacity: 0.6
    }

    Flickable {
        id: flick
        objectName: "opPanelFlick"   // 探针滚动取证用（B3 验收：底部两卡必须真能滚到）
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: sep.bottom
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        contentHeight: tray.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: tray
            width: flick.width
            spacing: 12

            // ── 大长条底托：所有按钮统一躺在上面 ──────────────────
            Rectangle {
                width: tray.width
                height: inner.height + 24
                radius: 24
                color: Theme.color.surface
                border.width: 1
                border.color: Theme.color.textSecondary

                Column {
                    id: inner
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    // ① 通知（2:1）—— 树叶浅绿，麦克风 +45°，长条框 −27°
                    OpHeroButton {
                        width: inner.width
                        height: width / 2
                        label: "通知"
                        iconName: "mic"
                        c0: classCenter.notifyC0
                        c1: classCenter.notifyC1
                        c2: classCenter.notifyC2
                        textColor: classCenter.notifyText
                        focalX: classCenter.notifyFocalX
                        iconRot: classCenter.notifyIconRot
                        barRot: classCenter.notifyBarRot
                        iconOpacity: classCenter.decoIconOpacity
                        barOpacity: classCenter.decoBarOpacity
                        gradRadius: classCenter.heroGradRadius
                        mirror: false
                        onClicked: classCenter.requestOp("notify")
                    }

                    // ② 监控（2:1）—— 浅蓝，方向与角度镜像
                    OpHeroButton {
                        width: inner.width
                        height: width / 2
                        label: "监控"
                        iconName: "monitor"
                        c0: classCenter.monitorC0
                        c1: classCenter.monitorC1
                        c2: classCenter.monitorC2
                        textColor: classCenter.monitorText
                        focalX: classCenter.monitorFocalX
                        iconRot: classCenter.monitorIconRot
                        barRot: classCenter.monitorBarRot
                        iconOpacity: classCenter.decoIconOpacity
                        barOpacity: classCenter.decoBarOpacity
                        gradRadius: classCenter.heroGradRadius
                        mirror: true
                        onClicked: classCenter.requestOp("monitor")
                    }

                    // ③ 竖排：锁屏 / 重启 / 关机
                    OpActionButton {
                        width: inner.width
                        height: 48
                        label: "锁屏"
                        iconName: "lock"
                        baseColor: classCenter.warnBase
                        fgColor: classCenter.warnFg
                        shadeColor: classCenter.warnShade
                        onClicked: classCenter.requestOp("lock")
                    }
                    OpActionButton {
                        width: inner.width
                        height: 48
                        label: "重启"
                        iconName: "refresh-cw"
                        baseColor: classCenter.warnBase
                        fgColor: classCenter.warnFg
                        shadeColor: classCenter.warnShade
                        onClicked: classCenter.requestOp("reboot")
                    }
                    OpActionButton {
                        width: inner.width
                        height: 48
                        label: "关机"
                        iconName: "power"
                        baseColor: classCenter.dangerBase
                        fgColor: classCenter.dangerFg
                        shadeColor: classCenter.dangerShade
                        onClicked: classCenter.requestOp("shutdown")
                    }

                    // ④ 同排 1:1：定时 / 软件
                    Row {
                        width: inner.width
                        height: 56
                        spacing: 16
                        OpActionButton {
                            width: (parent.width - 16) / 2
                            height: 56
                            label: "定时"
                            iconName: "clock"
                            baseColor: classCenter.neutralBase
                            fgColor: classCenter.neutralFg
                            shadeColor: classCenter.neutralShade
                            onClicked: classCenter.requestOp("schedule")
                        }
                        OpActionButton {
                            width: (parent.width - 16) / 2
                            height: 56
                            label: "软件"
                            iconName: "layout-grid"
                            baseColor: classCenter.neutralBase
                            fgColor: classCenter.neutralFg
                            shadeColor: classCenter.neutralShade
                            onClicked: classCenter.requestOp("software")
                        }
                    }

                    // ⑤ 占位提示
                    Text {
                        width: inner.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "—— 功能完善中 ——"
                        color: Theme.color.textSecondary
                        font.pixelSize: 12
                        topPadding: 4
                        bottomPadding: 4
                    }

                    // ⑥ 底部分割线
                    Rectangle {
                        width: inner.width
                        height: 1
                        color: Theme.color.textSecondary
                        opacity: 0.6
                    }

                    // ⑦ 底部两张 2:1 卡片（蓝图：底部分割线下方 · 班级编辑 / 设备设置）
                    //   2:1 = 高为宽的一半 ⇒ 高 = (inner.width - 间距) / 4
                    Row {
                        width: inner.width
                        height: (inner.width - 16) / 4
                        spacing: 16
                        Rectangle {
                            width: (parent.width - 16) / 2
                            height: parent.height
                            radius: 16
                            color: Theme.color.surface
                            border.width: 1
                            border.color: Theme.color.textSecondary
                            Text {
                                anchors.centerIn: parent
                                text: "班级编辑"
                                color: Theme.color.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: classCenter.requestOp("class_edit")
                            }
                        }
                        Rectangle {
                            width: (parent.width - 16) / 2
                            height: parent.height
                            radius: 16
                            color: Theme.color.surface
                            border.width: 1
                            border.color: Theme.color.textSecondary
                            Text {
                                anchors.centerIn: parent
                                text: "设备设置"
                                color: Theme.color.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: classCenter.requestOp("device_settings")
                            }
                        }
                    }
                }
            }
        }
    }
}
