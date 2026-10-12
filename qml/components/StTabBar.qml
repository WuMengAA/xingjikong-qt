// 星集控 · 通用标签栏（2026-10-12 · UI 评审一.2：监控中心拆标签页）
//
// 项目铁律：不用 Qt 原生 TabBar/TabButton（走系统调色板，暗色界面里变白）。
// 这份用我们自己的令牌（th.*）画，选中的那档用反白（inv，黑白稿唯一强调手段），
// 未选中用次文字（fg3），悬停用 hover 底——和 Btn 四态一个语言。
//
// 用法：
//   StTabBar { theme: th; width: parent.width; model: ["实时","录制回放","诊断"]; currentIndex: 0 }
//   读 currentIndex；点选由组件内部改 currentIndex（父级 onCurrentIndexChanged 接）。
//   ⚠️ 命名特意加 St 前缀：避免与 QtQuick.Controls 自带的 TabBar 冲突（后者无 model 属性，
//     同名会导致 CameraCenter 里 model: 赋值报 "Cannot assign to non-existent property"）。
import QtQuick

Rectangle {
    id: tabbar
    property var theme: ({})
    property var model: []
    property int currentIndex: 0

    height: 32
    color: "transparent"

    // 等宽分栏：总宽扣掉间距后平分。
    Row {
        id: row
        anchors.fill: parent
        spacing: 4
        Repeater {
            model: tabbar.model
            delegate: Rectangle {
                width: (tabbar.width - row.spacing * (tabbar.model.length - 1)) / tabbar.model.length
                height: 28
                radius: tabbar.theme.rCtrl !== undefined ? tabbar.theme.rCtrl : 8
                color: (index === tabbar.currentIndex)
                       ? (tabbar.theme.inv || "#F0F0F0")
                       : (ma.containsMouse ? (tabbar.theme.hover || "#1E1E1E") : "transparent")
                border.color: (index === tabbar.currentIndex)
                              ? (tabbar.theme.inv || "#F0F0F0")
                              : (tabbar.theme.stroke || "#2A2A2A")
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    // ⚠️ 反白态（选中）字必须反转成 win（近黑）；未选中用 fg3（次文字）。
                    //    亮色下 inv 是近黑，字用 win 依旧成立——两套都用 win，不写死。
                    color: (index === tabbar.currentIndex)
                           ? (tabbar.theme.win || "#0A0A0A")
                           : (tabbar.theme.fg3 || "#8A8A8A")
                    font.pixelSize: 13
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tabbar.currentIndex = index
                }
            }
        }
    }
}
