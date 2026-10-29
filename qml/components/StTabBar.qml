// 星集控 · 通用标签栏（2026-10-19 · MD3 重构：实心药丸 → 标准下划线 Tab）
//
// 项目铁律：不用 Qt 原生 TabBar/TabButton（走系统调色板，暗色界面里变白）。
// MD3 规格：文字 labelL(14)；选中 = 主色 + Medium + 底部 3px 主色指示器；
// 未选中 = onSurfaceVar + Normal。**无底色、无边框、无药丸**——选中态只靠
// 文字颜色 + 下划线指示器表达（用户 2026-10-19 明确拒绝黑色实心药丸）。
// Tab 按内容宽度左对齐排布（MD3 规范），档间距 24。
//
// 用法（API 与旧版完全一致，调用方零改动）：
//   StTabBar { theme: th; width: parent.width; model: ["实时","录制回放"]; currentIndex: 0 }
//   读 currentIndex；点选由组件内部改 currentIndex（父级 onCurrentIndexChanged 接）。
//   ⚠️ 命名特意加 St 前缀：避免与 QtQuick.Controls 自带的 TabBar 冲突（后者无 model 属性，
//     同名会导致 CameraCenter 里 model: 赋值报 "Cannot assign to non-existent property"）。
import QtQuick
import "./theme"   // Theme 设计令牌单例（2026-10-29 迁移：弃 Md3 theme 令牌，走 Theme.color.*）

Rectangle {
    id: tabbar
    // ⚠️ 保留 theme 属性仅为兼容既有调用方（DiagFeedbackDialog 透传 theme: dlg.theme）；
    //    本组件已不再消费 Md3 令牌，颜色一律走 Theme 单例。
    property var theme: ({})
    property var model: []
    property int currentIndex: 0

    height: 40
    color: "transparent"

    // 令牌便捷取值（统一走 Theme 单例；无字号令牌，labelL 用字面量 14）
    readonly property color tAcc:    Theme.color.primary
    readonly property color tFg3:    Theme.color.textSecondary
    readonly property int  tLabelL:  14

    Row {
        anchors.fill: parent
        spacing: 24
        Repeater {
            model: tabbar.model
            delegate: Item {
                width: tabTx.implicitWidth + 24
                height: parent.height
                Text {
                    id: tabTx
                    anchors.centerIn: parent
                    text: modelData
                    color: (index === tabbar.currentIndex) ? tabbar.tAcc : tabbar.tFg3
                    font.pixelSize: tabbar.tLabelL
                    font.weight: (index === tabbar.currentIndex) ? Font.Medium : Font.Normal
                }
                // 下划线指示器（3px 主色，仅选中档）
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: tabTx.width
                    height: 3
                    radius: 1.5
                    color: tabbar.tAcc
                    visible: index === tabbar.currentIndex
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tabbar.currentIndex = index
                }
            }
        }
    }
}
