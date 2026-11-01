// 新版外壳 · 竖屏底部导航（2026-11-01 · 转正后唯一竖屏导航）
//
// 转正时从经典外壳搬来：与 PreviewNavRail 同一份 navItems 的横排形态。
// 竖屏（shell.portrait）时左导航轨让位给本件，横屏时本件不占空间。
import QtQuick
import "../components/theme"
import "../island"

Rectangle {
    id: root

    property var navItems: []
    property string section: "dash"
    signal navActivated(string key)

    height: 80
    color: Theme.color.surface

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.color.accent
    }

    Row {
        anchors.centerIn: parent
        spacing: Theme.spacing.xl

        Repeater {
            model: root.navItems
            delegate: Item {
                required property var modelData
                width: 64
                height: 56
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacing.xs
                    SvgIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: modelData.icon
                        px: 22
                        tint: root.section === modelData.key
                              ? Theme.color.primary
                              : Theme.color.textSecondary
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        font.pixelSize: 11
                        color: root.section === modelData.key
                              ? Theme.color.primary
                              : Theme.color.textSecondary
                    }
                }
                TapHandler { onTapped: root.navActivated(modelData.key) }
            }
        }
    }
}
