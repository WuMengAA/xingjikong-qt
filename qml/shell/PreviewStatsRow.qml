// 新版外壳 · 统计行（2026-11-01）
//
// 从 PreviewConsole.qml 拆出（该文件超 200 行红线，见 §10.5）。
// 观感照设计稿 qml/layout/DesktopConsole.qml：4 个圆图标统计（总数 / 在线 / 离线 / 今日指令）。
import QtQuick
import "../components/theme"
import "../island"          // SvgIcon

Item {
    id: root

    property int deviceTotal: 0
    property int onlineCount: 0
    property int offlineCount: 0
    /** 今日指令数：暂无真源，显示「—」（不假装是 0）。 */
    property string todayCount: "—"

    height: 72

    Row {
        anchors.fill: parent
        spacing: Theme.spacing.xl

        Repeater {
            model: [
                { icon: "monitor",       label: "设备总数", value: String(root.deviceTotal) },
                { icon: "wifi",          label: "在线",     value: String(root.onlineCount) },
                { icon: "triangle-alert", label: "离线",    value: String(root.offlineCount) },
                { icon: "send",          label: "今日指令", value: root.todayCount }
            ]
            delegate: Row {
                required property var modelData
                spacing: Theme.spacing.md
                Rectangle {
                    width: 44; height: 44; radius: Theme.radius.full
                    color: Theme.color.accent
                    SvgIcon {
                        anchors.centerIn: parent
                        name: modelData.icon
                        px: 22
                        tint: Theme.color.primary
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: modelData.label
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                    }
                    Text {
                        text: modelData.value
                        font.pixelSize: 22
                        color: Theme.color.textPrimary
                    }
                }
            }
        }
    }
}
