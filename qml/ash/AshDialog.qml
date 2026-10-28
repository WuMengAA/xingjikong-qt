import QtQuick

// 对话框（shadcn Dialog）—— 替代原 Md3Dialog
//
// 调用签名与原 Md3Dialog 一致（opened / title / text / actions / actionClicked），
// 这样重置确认那处只改类型名：
//   AshDialog {
//       id: dlg; anchors.fill: parent; opened: false
//       title: "..."; text: "..."
//       actions: [{ text: "取消", role: "cancel" }, { text: "确认重置", role: "danger" }]
//       onActionClicked: function (index, role) { ... }
//   }
//
// shadcn 的 Dialog 与 MD3 的差别：卡片圆角小（12 而不是 28）、没有大面积 elevation 阴影，
// 遮罩同样 50% 黑。
Item {
    id: root

    property var th
    property bool opened: false
    property string title: ""
    property string text: ""
    /** [{ text: "...", role: "cancel" | "danger" | "" }] */
    property var actions: []

    signal actionClicked(int index, string role)

    visible: opened || scrim.opacity > 0.01
    z: 100

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: root.th ? root.th.overlay : Qt.rgba(0, 0, 0, 0.50)
        opacity: root.opened ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: root.th ? root.th.dur : 150 } }
        MouseArea { anchors.fill: parent }   // 吃掉点击，防止透到下面
    }

    Rectangle {
        id: card
        width: parent.width - 96 < 460 ? (parent.width - 96) : 460
        height: body.implicitHeight + (root.th ? root.th.cardPadLg : 24) * 2
        anchors.centerIn: parent
        radius: root.th ? root.th.radiusXl : 12
        color: root.th ? root.th.popover : "#ffffff"
        border.width: 1
        border.color: root.th ? root.th.cardBorder : Qt.rgba(0.035, 0.035, 0.043, 0.10)

        opacity: root.opened ? 1 : 0
        scale: root.opened ? 1 : 0.96
        Behavior on opacity { NumberAnimation { duration: root.th ? root.th.dur : 150 } }
        Behavior on scale { NumberAnimation { duration: root.th ? root.th.dur : 150 } }

        Column {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: root.th ? root.th.cardPadLg : 24
            spacing: 8

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: root.th ? root.th.fsLg : 18
                font.weight: Font.DemiBold
                color: root.th ? root.th.foreground : "#09090b"
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                visible: root.text.length > 0
                text: root.text
                font.pixelSize: root.th ? root.th.fsSm : 14
                color: root.th ? root.th.mutedFg : "#71717a"
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            Row {
                anchors.right: parent.right
                topPadding: 8
                spacing: 8

                Repeater {
                    model: root.actions
                    AshButton {
                        required property var modelData
                        required property int index
                        th: root.th
                        text: modelData.text
                        // role 直接当 variant 传：cancel→outline / danger→destructive，
                        // 其余落到 base（primary）。判断全在 AshButton 内部，这里不做三元。
                        variant: modelData.role
                        onClicked: root.actionClicked(index, modelData.role)
                    }
                }
            }
        }
    }
}
