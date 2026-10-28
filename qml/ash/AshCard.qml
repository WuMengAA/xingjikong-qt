import QtQuick

// 卡片（shadcn Card）
//
// 照 ShadcnCard.qml:38-44：
//   color: theme.card / radius: theme.radius(8) / border.width: 1 /
//   border.color: foreground 加透明度（**不是**直接用 border 色）
//
// shadcn 的卡与 MD3 的卡最大差别：**没有阴影**。层级靠 1px 细边框 + 底色差，
// 不靠 elevation。加阴影立刻变成 MD3 那张"纸"。
//
// 用法（与原 Md3Card 一致，只是换个名字和默认内距）：
//   AshCard {
//       Column { anchors.fill: parent; ... }
//   }
Rectangle {
    id: root

    /** 主题令牌（AshTheme 实例，由页面注入 ctx.ash） */
    property var th

    /** 内距：shadcn p-4(16)；传 24 用 p-6 的大卡 */
    property int pad: th ? th.cardPad : 16
    /** 是否画边框（内嵌在已带边框的容器里时可关） */
    property bool bordered: true

    default property alias content: inner.data

    color: th ? th.card : "#ffffff"
    radius: th ? th.radiusLg : 8
    border.width: bordered ? 1 : 0
    border.color: th ? th.cardBorder : Qt.rgba(0.035, 0.035, 0.043, 0.10)
    implicitHeight: inner.implicitHeight + pad * 2

    Item {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.pad
        implicitHeight: childrenRect.height
    }
}
