// 星集控 · 手机端底部导航（2026-10-11 · 独立 QML 方案落地）
// 文档 5.1：手机端用 TabBar 扁平导航，不用 StackView 多层。
// 四标签：概览 / 设备 / 监控 / 我的。currentIndex 双向绑到 Main 的 StackLayout。
import QtQuick

Item {
    id: nav
    implicitHeight: 54
    property var th: ({})
    property int currentIndex: 0
    property var tabs: ["概览", "设备", "监控", "我的"]
    // 切换信号（Main 监听做额外处理）
    signal tabChanged(int index)

    Row {
        anchors.fill: parent
        Repeater {
            model: nav.tabs
            delegate: Item {
                width: parent.width / nav.tabs.length
                height: parent.height
                Rectangle {
                    anchors.fill: parent
                    color: (nav.currentIndex === index) ? nav.th.cream : nav.th.panel
                    border.color: nav.th.sepline
                    border.width: (index === 0) ? 1 : 0
                }
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: (nav.currentIndex === index) ? nav.th.inv : nav.th.fg3
                    font.pixelSize: 13
                    font.weight: (nav.currentIndex === index) ? Font.Medium : Font.Normal
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        nav.currentIndex = index
                        nav.tabChanged(index)
                    }
                }
            }
        }
    }
}
