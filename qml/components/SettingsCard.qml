// 设置 / 个人中心通用卡片（2026-10-25 拆页时抽出）
//
// 为什么要抽：原 SettingsPage 里 7 张卡各自重复 8 行「Rectangle + 圆角 + 边框 +
// Column + margins」样板，598 行里近 60 行是同一段复制。抽成一件后：
//   · 新增分类只是写内容，不再复制样板；
//   · 卡片视觉只有一个真源，改一次全部一致（这正是用户说的"不美观"的根因之一：
//     原来每张卡的标题字号还不一样，有的 14px Medium、有的 12px）。
//
// 用法（默认属性 = 内容，子项按声明顺序纵向排）：
//   SettingsCard {
//       md3: th
//       title: "连接"
//       Text { ... }
//       Btn { ... }
//   }
import QtQuick
import "./theme"   // Theme 设计令牌单例（pragma Singleton）

Rectangle {
    id: card

    property string title: ""
    property string subtitle: ""
    property int gap: 12

    /** 默认属性：声明在本件里的子项直接落到内容列（标题永远在最上）。 */
    default property alias bodyData: col.data

    radius: 12
    color: Theme.color.background
    border.color: Theme.color.textSecondary
    border.width: 1
    implicitHeight: col.implicitHeight + 32

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: card.gap

        // 标题块（无标题时不占位）
        Column {
            width: parent.width
            spacing: 4
            visible: card.title !== ""
            Text {
                width: parent.width
                text: card.title
                color: Theme.color.textSecondary
                font.pixelSize: 14
                font.weight: Font.Medium
                font.letterSpacing: 0.1
            }
            Text {
                width: parent.width
                visible: card.subtitle !== ""
                wrapMode: Text.Wrap
                text: card.subtitle
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
        }
    }
}
