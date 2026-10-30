// 仪表盘 · 洞察区（S3 动作排行 + S2 云端存储 + S4 最近回执）
//
// 从 shell/MainWindow.qml 内联段抽出（#473 拆件）。横屏双卡并排、竖屏单列堆叠，
// 靠 GridLayout 列数随朝向变，不是两套布局代码。
//
// 数据来源：backend（actionRanking / storage）与宿主的回执流水（host.results）。
// 空态一律写人话（"连上云端后自动统计"），不摆 0 假装有数据。
import QtQuick
import QtQuick.Layouts
import "../../components"         // AppCard
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

GridLayout {
    id: cards

    property var host: null           // ctx.window（actionTitleOf / results）
    property bool portrait: false
    property var toastFn: null

    columns: cards.portrait ? 1 : 2
    columnSpacing: Theme.spacing.md
    rowSpacing: Theme.spacing.md
    Layout.fillWidth: true
    Layout.bottomMargin: Theme.spacing.sm

    /** 动作名 → 中文（复用宿主映射表，不另建一份）。 */
    function actionLabel(a) {
        if (cards.host && cards.host.actionTitleOf) {
            const t = cards.host.actionTitleOf(a)
            if (t && t !== "") return t
        }
        return a || ""
    }

    /** S4：宿主最近回执（真回执流水）。 */
    function recentResults() {
        if (cards.host && cards.host.results) return cards.host.results
        return []
    }

    // ── S3 动作排行 · 近 7 天 ──
    AppCard {
        Layout.fillWidth: true
        Layout.preferredHeight: 208
        // 原 variant: "elevated" ⇒ AppCard 默认 shadow: true（极柔阴影）

        ColumnLayout {
            // ⚠️ AppCard 内部已是 ColumnLayout：子项不能 anchors.fill，必须走 Layout
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.sm

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "动作排行 · 近 7 天"
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "刷新"
                    font.pixelSize: 12
                    color: Theme.color.primary
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.fetchActionRanking()
                            if (cards.toastFn) cards.toastFn("已刷新动作排行")
                        }
                    }
                }
            }

            Repeater {
                // 排名算进 model：delegate 声明 required property 后，Qt 6 不再把上下文
                // 属性 index 注入委托链（实测 ReferenceError: index is not defined；
                // 存量 Md3Shell.qml:449 同坑）。这里不依赖 index。
                model: {
                    const src = backend.actionRanking ? backend.actionRanking.slice(0, 5) : []
                    const out = []
                    for (let i = 0; i < src.length; ++i)
                        out.push({ rank: i + 1, action: src[i].action, ok: src[i].ok, total: src[i].total })
                    return out
                }
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm
                    Text {
                        text: modelData.rank
                        font.pixelSize: 12
                        font.weight: modelData.rank <= 3 ? Font.Bold : Font.Normal
                        color: modelData.rank <= 3 ? (Theme.color.primary)
                                                   : (Theme.color.textSecondary)
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cards.actionLabel(modelData.action)
                        font.pixelSize: 12
                        color: Theme.color.textPrimary
                    }
                    Text {
                        text: "成功 " + modelData.ok + " / " + modelData.total
                        font.pixelSize: 11
                        color: Theme.color.textSecondary
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !backend.actionRanking || backend.actionRanking.length === 0
                text: "还没有排行数据（连上云端后自动统计）"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ── S2 云端存储概况 + S4 最近回执（已拆出为 DashStorageCard）──
    DashStorageCard {
        Layout.fillWidth: true
        Layout.preferredHeight: 208
        host: cards.host
    }
}
