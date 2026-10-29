// 班级课表 · 空态（2026-10-25 · 从 ClassTimetablePane 抽出，守 200 行红线）
//
// 空态不是"没数据"这么简单：它得告诉老师「这里会显示什么」和「怎么让它有内容」，
// 否则就是用户说的"文字游戏"——一块空白谁也不知道下一步点哪。
import QtQuick
import "../../components/theme"  // Theme 设计令牌单例（pragma Singleton）
import "../../island"            // SvgIcon

Column {
    id: empty

    property bool subscribed: false
    property int maxWidth: 360

    anchors.centerIn: parent
    spacing: 8

    SvgIcon {
        name: "clock"; px: 32; tint: Theme.color.textSecondary || "#D1D5DB"
        anchors.horizontalCenter: parent.horizontalCenter
    }
    Text {
        text: empty.subscribed ? "本班还没导入课表" : "先从左侧选择一个班级"
        font.pixelSize: 13
        color: Theme.color.textSecondary || "#6B7280"
        anchors.horizontalCenter: parent.horizontalCenter
    }
    Text {
        text: "点右上「导入档案」，选 ClassIsland 的 Default.json"
        width: Math.min(empty.maxWidth, empty.parent ? empty.parent.width - 40 : empty.maxWidth)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: 12
        color: Theme.color.textSecondary || "#D1D5DB"
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
