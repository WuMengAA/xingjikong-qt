// 星集控 · 输入框（AppInput）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · **禁裸用 TextField**（规范红线）⇒ 自绘：Rectangle 底 + TextInput
// · **无边框**：靠 Theme.color.surfaceVariant 底与页面 background 拉开
// · 圆角：Theme.radius.md；高度 40（与 AppButton md 对齐）
// · 文字：Theme.color.textPrimary；占位：Theme.color.textSecondary
// · 聚焦：不画边框（无边框铁律），改用底色提亮 + 光标色表达
//
// ── 为什么不用 QQC TextField ────────────────────────────────────────────
// 规范禁止裸控件默认样式；且 Basic 风格的 TextField 自带下划线/边框，
// 与"去边框"冲突。自绘可完全按 Theme 令牌控制。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppInput { placeholder: "班级名称"; text: foo; onTextEdited: foo = text }
//   AppInput { placeholder: "密码"; echoMode: TextInput.Password }

import QtQuick
import "theme"

Item {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property int echoMode: TextInput.Normal
    /** 只读态（如展示用）。 */
    property bool readOnly: false

    signal accepted()                 // 回车
    signal textEdited(string t)

    implicitWidth: 240
    implicitHeight: 40

    // 底色：聚焦时略提亮（无边框，靠色差表达状态）
    readonly property color _bg: input.activeFocus || inputHover.containsMouse
                                 ? Theme.color.surfaceVariant
                                 : Theme.color.surface

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.md
        color: root._bg
        // 无边框（规范铁律）

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.md
        anchors.rightMargin: Theme.spacing.md
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.color.textPrimary
        font.pixelSize: 14
        echoMode: root.echoMode
        readOnly: root.readOnly
        selectByMouse: true
        clip: true

        onAccepted: root.accepted()
        onTextEdited: root.textEdited(text)

        // 占位（TextInput 自带 placeholderText 也能用，这里显式控制配色）
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.placeholder
            visible: input.text.length === 0
            color: Theme.color.textSecondary
            font.pixelSize: 14
        }
    }

    MouseArea {
        id: inputHover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton      // 只做 hover 探测，不抢点击
        cursorShape: root.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
    }

    // 点空白处也能聚焦（MouseArea 不抢事件，故用 TapHandler 更稳）
    TapHandler {
        onTapped: {
            if (!root.readOnly) input.forceActiveFocus()
        }
    }
}
