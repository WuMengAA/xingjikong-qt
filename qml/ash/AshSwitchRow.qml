import QtQuick

// 开关行（设置页主力控件）—— 替代原 Md3SwitchRow
//
// 结构：左（标题 + 说明两行）／右（AshSwitch）。
// 与 MD3 的 SwitchRow 差别：不画分隔线、不整行变色、标题 14/说明 12，
// 层级只靠字号与字重（shadcn 的做法），没有 MD3 那种"行高 56 + 涟漪"。
//
// 点击范围：标题区与开关都可点（老师站在讲台前用，不用瞄准那个 32×18 的小块）。
Item {
    id: root

    property var th
    property string text: ""
    property string supporting: ""
    property bool checked: false

    signal toggled(bool value)

    // 高度自适应：说明文字可能换行（安全模式那条就三行），不能写死 44
    implicitHeight: col.implicitHeight + 12

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: root.text
            font.pixelSize: root.th ? root.th.fsSm : 14
            font.weight: Font.Medium
            color: root.th ? root.th.foreground : "#09090b"
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: root.supporting.length > 0
            text: root.supporting
            font.pixelSize: root.th ? root.th.fsXs : 12
            color: root.th ? root.th.mutedFg : "#71717a"
            wrapMode: Text.WordWrap
            lineHeight: 1.25
        }
    }

    AshSwitch {
        id: sw
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        th: root.th
        checked: root.checked
        onToggled: function (v) { root.toggled(v) }
    }

    // 标题区也可点（不含右侧开关，避免与控件自身双击互相打架）
    MouseArea {
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
