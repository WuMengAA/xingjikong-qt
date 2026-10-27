import QtQuick 2.15
import "."

// 灵动岛 · 分组标签（统一布局规范 · 基元四）
//
// ClassIsland 式「按天分组」的段头：沉在底色之上的小字（今天 / 昨天 / M月d日）。
// 取代各 ListView.section.delegate 各自手搓的 18px 标签，统一走 th.sectionH + 配色。
//
// 接口：
//   th    主题令牌（必传）
//   text  分组文案（如 "今天" / "10月25日"）
Item {
    id: lbl
    property var th: null
    property string text: ""
    // ⚠️ Qt6：Item.height 是 FINAL，不能 redeclare。改用自定义名 + 绑定继承的 height。
    property int secHeight: (th ? th.sectionH : 18)
    implicitHeight: lbl.secHeight
    height: lbl.secHeight

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: lbl.text
        color: lbl.th ? lbl.th.sub : "#C8C8C8"
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
}
