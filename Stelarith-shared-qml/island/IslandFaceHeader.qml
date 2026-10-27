import QtQuick 2.15
import "."

// 灵动岛 · 面头部（统一布局规范 · 基元一）
//
// 所有 face 的标题头统一走这里，取代历史/课表/设置…各自手搓的标题 Text +
// 计数 Text（视觉漂移根因之一）。提供：标题 + 未读点 + 副标题（标题右侧紧跟
// 的小字，如条数/未读/周次）+ 尾部操作槽（筛选/已读/清空等按钮放 trailing）。
//
// 接口：
//   th        主题令牌（必传）
//   title     标题
//   subtitle  副标题小字（可选）
//   showDot   标题前未读小圆点
//   dotColor  小圆点色（默认 accent）
//   trailing  调用方放按钮（默认属性，写在标签体内即可）
Item {
    id: hdr
    property var th: null
    property string title: ""
    property string subtitle: ""
    property bool showDot: false
    property color dotColor: (th ? th.accent : "#D0BCFF")
    property int pad: (th ? th.spM : 12)
    // ⚠️ Qt6：Item.height 是 FINAL，不能 redeclare。改用自定义名 + 绑定继承的 height。
    property int headHeight: (th ? th.headH : 32)
    implicitHeight: hdr.headHeight
    height: hdr.headHeight

    Rectangle {
        visible: hdr.showDot
        anchors.left: parent.left
        anchors.leftMargin: hdr.pad + 2
        anchors.verticalCenter: parent.verticalCenter
        width: 6; height: 6; radius: 3
        color: hdr.dotColor
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: hdr.pad + (hdr.showDot ? 14 : 0)
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Text {
            text: hdr.title
            color: hdr.th ? hdr.th.fg : "#FAFAFA"
            font.pixelSize: hdr.th ? hdr.th.fsTitle : 15
            font.weight: Font.DemiBold
        }
        Text {
            visible: hdr.subtitle !== ""
            text: hdr.subtitle
            color: hdr.th ? hdr.th.sub : "#C8C8C8"
            font.pixelSize: hdr.th ? hdr.th.fsNote : 12
        }
    }

    // 尾部操作槽（筛选 / 已读 / 清空等按钮）
    Item {
        id: trailArea
        anchors.right: parent.right
        anchors.rightMargin: hdr.pad
        anchors.verticalCenter: parent.verticalCenter
    }

    default property alias trailing: trailArea.data
}
