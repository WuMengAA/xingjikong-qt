// 仪表盘 · 洞察区 · 云端存储概况 + 最近回执卡（2026-10-30 从 DashInsightCards 拆出）
//
// 数据来源：backend（storage）与宿主的回执流水（host.results）。
// 空态一律写人话（"还没有回执…"），不摆 0 假装有数据。
import QtQuick
import QtQuick.Layouts
import "../../components"         // AppCard
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

AppCard {
    id: root
    // 宿主（ctx.window：actionTitleOf / results）
    property var host: null

    /** 动作名 → 中文（复用宿主映射表，不另建一份）。 */
    function actionLabel(a) {
        if (root.host && root.host.actionTitleOf) {
            const t = root.host.actionTitleOf(a)
            if (t && t !== "") return t
        }
        return a || ""
    }

    /** 宿主最近回执（真回执流水）。 */
    function recentResults() {
        if (root.host && root.host.results) return root.host.results
        return []
    }

    ColumnLayout {
        // ⚠️ AppCard 内部已是 ColumnLayout：子项不能 anchors.fill，必须走 Layout
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Theme.spacing.sm

        Text {
            text: "云端存储概况"
            font.pixelSize: 16
            font.weight: Font.Medium
            color: Theme.color.textPrimary
        }
        Text {
            Layout.fillWidth: true
            text: "已知设备 " + (backend.storage.knownDevices !== undefined ? backend.storage.knownDevices : "—")
                  + " · 班级 " + (backend.storage.classes !== undefined ? backend.storage.classes : "—")
                  + " · 事件文件 " + (backend.storage.eventsFiles !== undefined ? backend.storage.eventsFiles : "—")
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
        }
        Text {
            Layout.fillWidth: true
            text: "最近回执"
            font.pixelSize: 12
            font.weight: Font.Medium
            color: Theme.color.textPrimary
            topPadding: Theme.spacing.xs
        }

        Repeater {
            model: root.recentResults().slice(0, 4)
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.spacing.sm

                Text {
                    text: modelData.t || ""
                    font.pixelSize: 11
                    color: Theme.color.textSecondary
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.actionLabel(modelData.action) + " · " + (modelData.uid || "")
                    font.pixelSize: 11
                    color: Theme.color.textSecondary
                }
                Text {
                    text: modelData.ok ? "成功" : "失败"
                    font.pixelSize: 11
                    color: modelData.ok
                           ? (Theme.color.primary)
                           : (Theme.color.destructive)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.recentResults().length === 0
            text: "还没有回执（下发动作后这里会显示执行结果）"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
        }
        Item { Layout.fillHeight: true }
    }
}
