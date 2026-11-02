// 新版外壳 · 统计行（2026-11-01 · QtShadcn 全量）
//
// 从 PreviewConsole.qml 拆出（该文件超 200 行红线，见 §10.5）。
// 观感照设计稿 qml/layout/DesktopConsole.qml：4 个统计项（总数 / 在线 / 离线 / 今日指令）。
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 之前每一项是手画 Rectangle 圆底 + 图标 + 两行 Text。本版换成库组件：
//   · 每项一张 **ShadcnCard**（自带圆角/内边距/背景分层，替代手画 Rectangle）
//   · 在线/离线状态点 → **ShadcnStatusDot**（Online / Offline 语义枚举）
// 令牌统一走 Theme.*（唯一真源）。
import QtQuick
import QtShadcn
import "../components/theme"
import "../island"          // SvgIcon（封装 image://svgicon，tint/px 两个属性）

Item {
    id: root

    property int deviceTotal: 0
    property int onlineCount: 0
    property int offlineCount: 0
    /** 今日指令数：暂无真源，显示「—」（不假装是 0）。 */
    property string todayCount: "—"

    height: 88

    Row {
        anchors.fill: parent
        spacing: Theme.spacing.md

        Repeater {
            model: [
                { icon: "monitor",        label: "设备总数", value: String(root.deviceTotal),  dot: ShadcnStatusDot.Status.None },
                { icon: "wifi",           label: "在线",     value: String(root.onlineCount),  dot: ShadcnStatusDot.Status.Online },
                { icon: "triangle-alert", label: "离线",     value: String(root.offlineCount), dot: ShadcnStatusDot.Status.Offline },
                { icon: "send",           label: "今日指令", value: root.todayCount,           dot: ShadcnStatusDot.Status.None }
            ]
            delegate: ShadcnCard {
                required property var modelData
                width: (root.width - Theme.spacing.md * 3) / 4
                height: root.height

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.md
                    spacing: Theme.spacing.md

                    Rectangle {
                        width: 44; height: 44; radius: Theme.radius.full
                        color: Theme.color.accent
                        anchors.verticalCenter: parent.verticalCenter
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
                        Row {
                            spacing: Theme.spacing.xs
                            ShadcnStatusDot {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.dot !== ShadcnStatusDot.Status.None
                                status: modelData.dot
                                size: 6
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.pixelSize: 12
                                color: Theme.color.textSecondary
                            }
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
}
