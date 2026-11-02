// 被控端 · 灵动岛组件 · 单行（2026-11-03，从 IslandLayoutCard.qml 拆出守 200 行红线）
//
// 一行 = 一个岛面：≡ 拖拽把手 / 单字面签 / 名称与说明 / 缩放 −/+ / 启用开关。
// 拖动状态留在宿主（Card）—— 它需要知道"整体几行、拖到第几行"，
// 把这份状态放进每一行必然出现两份互相打架的 dragRow。
// 缩放/启用也往上抛（scaleDown/scaleUp/toggle），因为写状态的是 C++ 模型
//（`islandLayout.setScaleById`），行里只发意图 —— QML 不存业务状态（AGENTS §10.9）。
//
// ⚠️ 本件无业务计算：序号/缩放值/启用态都来自 model（C++ IslandLayoutModel）。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

Item {
    id: rowItem

    required property int index
    required property var model

    // 宿主注入
    property int rowH: 64
    property bool dragging: false
    property real dragDy: 0
    property real shift: 0

    // 上报
    signal dragStarted()
    signal dragMoved(real dy)
    signal dragFinished(real dy)
    signal dragAborted()
    signal scaleDown()
    signal scaleUp()
    signal toggled()

    width: parent ? parent.width : 0
    height: rowH
    y: index * rowH + (dragging ? dragDy : shift)
    z: dragging ? 2 : 0
    opacity: dragging ? 0.92 : 1.0

    Behavior on y {
        enabled: !rowItem.dragging
        NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
    }

    // 拖动高亮：只在被拖时出底色（无边框原则靠底色差，不画线）
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.radius.md
        color: rowItem.dragging ? Theme.color.surfaceVariant : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        spacing: 10

        // ── ≡ 拖拽把手（36 宽，手指好按）──
        Item {
            Layout.preferredWidth: 36
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                text: "≡"
                color: Theme.color.textSecondary
                font.pixelSize: 20
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                property real y0: 0
                onPressed: { y0 = 0; rowItem.dragStarted() }
                onPositionChanged: if (pressed) rowItem.dragMoved(mouse.y - y0)
                onReleased: rowItem.dragFinished(mouse.y - y0)
                onCanceled: rowItem.dragAborted()
            }
        }

        // ── 单字面签（与岛上那颗同一字形，方便老师对上号）──
        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 14
            color: Theme.color.surfaceVariant
            Text {
                anchors.centerIn: parent
                text: rowItem.model.label
                color: Theme.color.textPrimary
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }

        // ── 名称 + 说明（长文案省略，避免把行撑高）──
        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: rowItem.model.orderNo + " · " + rowItem.model.title
                color: Theme.color.textPrimary
                font.pixelSize: 14
                elide: Text.ElideRight
                width: parent.width
            }
            Text {
                text: rowItem.model.note
                color: Theme.color.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width
            }
        }

        // ── 缩放：−  100%  + ──
        Row {
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            AppButton {
                text: "−"
                size: "sm"
                variant: "ghost"
                enabled: rowItem.model.scale > 80
                onClicked: rowItem.scaleDown()
            }
            Text {
                width: 42
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: rowItem.model.scale + "%"
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
            AppButton {
                text: "+"
                size: "sm"
                variant: "ghost"
                enabled: rowItem.model.scale < 140
                onClicked: rowItem.scaleUp()
            }
        }

        // ── 启用开关（自绘：被控端无 QtQuick Controls 运行库，Switch 会整页空白）──
        Item {
            Layout.preferredWidth: 46
            Layout.preferredHeight: 28
            Rectangle {
                anchors.centerIn: parent
                width: 44
                height: 26
                radius: 13
                color: rowItem.model.enabled ? Theme.color.primary : Theme.color.surfaceVariant
                Behavior on color {
                    ColorAnimation { duration: Theme.motion.fast }
                }
                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    y: 3
                    x: rowItem.model.enabled ? 21 : 3
                    color: Theme.color.background
                    Behavior on x {
                        NumberAnimation {
                            duration: Theme.motion.fast
                            easing.type: Easing.OutBack
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: rowItem.toggled()
                }
            }
        }
    }
}
