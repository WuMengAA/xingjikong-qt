import QtQuick

// 输入框 —— 替代原 Md3TextField（被控端只有 2 处用到：云端地址、VoiceHub 服务地址）
//
// 照 ShadcnInput.qml：h-9(36px) + rounded-md(6px) + border 1px input 色 +
// 聚焦时 border 换 ring 并叠 3px 外环（ring/30）。
//
// ⚠️ 用 QtQuick 基础的 TextInput，**不是** QtQuick.Controls 的 TextField ——
//    被控端部署历史上缺 QQC 运行库导致整页空白过，本组件库一律不碰 Controls。
Item {
    id: root

    /** 转发内部 TextInput 的 accepted（回车提交）。QML 里用 `onAccepted: {...}` 接。 */
    signal accepted()

    property var th
    property string placeholder: ""
    /** 输入框下方的辅助说明（原 Md3TextField.helper） */
    property string helper: ""
    property alias text: input.text
    property alias inputItem: input

    /** 输入框本体高度（不含 helper） */
    readonly property int fieldH: th ? th.controlH : 36

    implicitHeight: fieldH + (helper.length > 0 ? helperText.implicitHeight + 6 : 0)

    Rectangle {
        id: bg
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.fieldH
        radius: root.th ? root.th.radiusMd : 6
        color: root.th ? root.th.background : "#ffffff"
        border.width: 1
        border.color: root.th ? root.th.input : "#e4e4e7"
        Behavior on border.color { ColorAnimation { duration: root.th ? root.th.durFast : 120 } }
    }

    // 聚焦环：3px、ring 的 30% 透明（ShadcnInput.qml:44-48 同款）
    Rectangle {
        anchors.fill: bg
        anchors.margins: -3
        radius: (bg.height + 6) / 2 - 3
        color: "transparent"
        border.width: 3
        border.color: root.th ? Qt.rgba(root.th.ring.r, root.th.ring.g, root.th.ring.b, 0.30)
                              : Qt.rgba(0.63, 0.63, 0.67, 0.30)
        visible: input.activeFocus
    }

    TextInput {
        id: input
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.th ? root.th.rowPadH : 12
        anchors.rightMargin: root.th ? root.th.rowPadH : 12
        height: root.fieldH
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: root.th ? root.th.fsSm : 14
        color: root.th ? root.th.foreground : "#09090b"
        selectionColor: root.th ? root.th.primary : "#18181b"
        selectedTextColor: root.th ? root.th.primaryFg : "#fafafa"
        selectByMouse: true
        clip: true
        // 聚焦时边框提到 ring 色
        onActiveFocusChanged: bg.border.color = activeFocus
            ? (root.th ? root.th.ring : "#a1a1aa")
            : (root.th ? root.th.input : "#e4e4e7")
        onAccepted: root.accepted()
    }

    Text {
        anchors.left: input.left
        anchors.right: input.right
        anchors.verticalCenter: bg.verticalCenter
        visible: input.text.length === 0
        text: root.placeholder
        font.pixelSize: root.th ? root.th.fsSm : 14
        color: root.th ? root.th.mutedFg : "#71717a"
        elide: Text.ElideRight
    }

    Text {
        id: helperText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: bg.bottom
        anchors.topMargin: 6
        visible: root.helper.length > 0
        text: root.helper
        font.pixelSize: root.th ? root.th.fsXs : 12
        color: root.th ? root.th.mutedFg : "#71717a"
        wrapMode: Text.WordWrap
        lineHeight: 1.25
    }
}
