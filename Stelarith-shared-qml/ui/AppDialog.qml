// 星集控 · 弹窗（AppDialog）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 遮罩：Theme.color.scrim（50% 黑）
// · 卡片：居中、Theme.color.surface 底、Theme.radius.lg 圆角、**无边框**
// · 动效：弹出时 scale 0.9 → 1.0、opacity 0 → 1（果冻曲线）
// · 淡出/收起同理反向
//
// ── 为什么自绘而不用 QQC Dialog ──────────────────────────────────────────
// 规范红线：禁止原生控件默认样式；且 QQC Dialog 在 Basic 风格下带自己的边框/标题栏，
// 与本规范"无边框 + 底色分区"冲突。自绘可控，且能精确套 Theme 令牌。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppDialog {
//       opened: someBool
//       onClosed: someBool = false
//       ColumnLayout { Text { text: "标题" } AppButton { text: "确定" } }
//   }
// 直接写的子项进入内部卡片容器（ColumnLayout + padding lg）。
// 点遮罩或按 Esc 触发 closed()。

import QtQuick
import QtQuick.Layouts
import "theme"

Item {
    id: root

    /** 是否展开。切换时自动跑进入/退出动效。 */
    property bool opened: false
    /** 点遮罩或 Esc 时发出；**不自行改 opened**（受控组件，状态归调用方）。 */
    signal closed()

    property alias content: contentBox
    default property alias contentData: contentBox.data

    visible: opened || _anim.running
    anchors.fill: parent
    // 模态：吃掉下层所有鼠标事件
    z: 1000

    // 进出动效驱动（0 = 收起，1 = 展开）
    QtObject {
        id: _anim
        property real p: root.opened ? 1 : 0
        property bool running: false
        onPChanged: running = true
        Behavior on p {
            NumberAnimation {
                duration: Theme.motion.normal
                easing.type: Easing.OutBack          // 果冻回弹
                onRunningChanged: if (!running) _anim.running = false
            }
        }
    }

    // ── 遮罩 ────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.color.scrim
        opacity: _anim.p

        MouseArea {
            anchors.fill: parent
            enabled: root.opened
            onClicked: root.closed()
        }
    }

    // ── 卡片（居中 + 缩放淡入）──────────────────────────────────────────
    Rectangle {
        id: cardBg
        anchors.centerIn: parent
        width: Math.min(contentBox.implicitWidth + Theme.spacing.lg * 2, root.width - Theme.spacing.xl * 2)
        height: contentBox.implicitHeight + Theme.spacing.lg * 2
        radius: Theme.radius.lg
        color: Theme.color.surface
        // 无边框（规范铁律）

        // 0.9 → 1.0 缩放 + 0 → 1 透明度，都跟 _anim.p 走
        scale: 0.9 + 0.1 * _anim.p
        opacity: _anim.p

        // 内容容器
        ColumnLayout {
            id: contentBox
            anchors.fill: parent
            anchors.margins: Theme.spacing.lg
            spacing: Theme.spacing.md
        }
    }

    // Esc 关闭
    Keys.onEscapePressed: root.closed()
    focus: root.opened
}
