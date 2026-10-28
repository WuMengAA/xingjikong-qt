import QtQuick

// 信息行：label 左 / value 右 —— 替代原 SecInfoRow（那份绑的是 MD3 令牌）
//
// shadcn 的清单式信息行：label 用 muted-foreground、value 用 foreground，
// 两者同字号（14），层级靠颜色而不是字重 —— 与 MD3 的"label 灰、value 亮"同理但更平。
// vColor 仍由调用方传 C++ 算好的结果色（连接状态/升级状态这类）。
Item {
    id: root

    property var th
    property string label: ""
    property string value: ""
    property color vColor: th ? th.foreground : "#09090b"

    width: parent ? parent.width : 0
    implicitHeight: 32

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 160
        text: root.label
        font.pixelSize: root.th ? root.th.fsSm : 14
        color: root.th ? root.th.mutedFg : "#71717a"
        elide: Text.ElideRight
    }
    Text {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 160
        horizontalAlignment: Text.AlignRight
        text: root.value
        font.pixelSize: root.th ? root.th.fsSm : 14
        color: root.vColor
        elide: Text.ElideMiddle
    }
}
