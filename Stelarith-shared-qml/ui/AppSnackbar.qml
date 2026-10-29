// 星集控 · 轻提示（AppSnackbar）—— 2026-10-29 QtShadcn 规范
//
// ── 定位 ────────────────────────────────────────────────────────────────
// 取代共享库 `md3/Md3Snackbar`（共享库 App 系补齐第 4 件）。
// **API 不变**：text / actionText / opened / actionClicked() / show(t, act) / hide()，
// 4s 自动消失也照旧 —— 手机端壳 `toast(s) { snack.show(s) }` 零改动。
//
// ── 与 Md3Snackbar 的差异 ───────────────────────────────────────────────
// 1. **反色底改用 Theme 令牌**：旧件用 Md3.cInverseSurf / cInverseOn；Theme 无 inverse 语义
//    （shadcn 也没有）⇒ 底取 `Theme.color.textPrimary`（浅色＝近黑）、字取 `background`，
//    等价于原来的反色效果，且不新造令牌。
// 2. **圆角**：旧件 rXS(4) → 规范统一 `Theme.radius.md`(10)，与其它浮层同语言。
// 3. **动作按钮**：旧件是裸文字（primaryContainer 色，在黑底上偏暗、且与正文难区分）；
//    新件给它一个 surfaceVariant 底的小 pill —— 去边框时代靠**底色差**分区（规范 §1）。
//
// 用法：
//   AppSnackbar { id: snack; anchors.bottom: nav.top; anchors.bottomMargin: 12; z: 10 }
//   function toast(s) { snack.show(s) }

import QtQuick
import "theme"

Item {
    id: root

    property alias text: snackText.text
    property string actionText: ""
    property bool opened: false

    signal actionClicked()

    visible: opened
    implicitWidth: snackRow.implicitWidth + Theme.spacing.lg
    implicitHeight: 48

    Timer {
        id: autoHide
        interval: 4000
        onTriggered: root.opened = false
    }

    function show(t, act) {
        snackText.text = t
        root.actionText = (act === undefined) ? "" : act
        root.opened = true
        autoHide.restart()
    }
    function hide() { root.opened = false }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.md
        color: Theme.color.textPrimary          // 反色底（近黑）

        Row {
            id: snackRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.md
            spacing: Theme.spacing.sm

            Text {
                id: snackText
                font.pixelSize: 14
                color: Theme.color.background   // 反色字（近白）
                maximumLineCount: 2
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 480)
                anchors.verticalCenter: parent.verticalCenter
            }

            // 动作：不给裸文字（黑底上看不清），给一枚小 pill 靠底色差分区
            Rectangle {
                visible: root.actionText !== ""
                radius: Theme.radius.sm
                color: Theme.color.surfaceVariant
                width: actTx.implicitWidth + Theme.spacing.md
                height: 28
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: actTx
                    anchors.centerIn: parent
                    text: root.actionText
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.actionClicked()
                        root.opened = false
                    }
                }
            }
        }
    }
}
