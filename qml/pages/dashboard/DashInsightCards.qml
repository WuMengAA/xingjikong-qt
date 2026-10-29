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
import "../../components/md3"     // Md3Card
import "../../components/theme"   // Md3 令牌（pragma Singleton）

GridLayout {
    id: cards

    property var host: null           // ctx.window（actionTitleOf / results）
    property bool portrait: false
    property var toastFn: null

    columns: cards.portrait ? 1 : 2
    columnSpacing: Md3.spM
    rowSpacing: Md3.spM
    Layout.fillWidth: true
    Layout.bottomMargin: Md3.spS

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
            spacing: Md3.spS

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "动作排行 · 近 7 天"
                    font.pixelSize: Md3.titleS
                    font.weight: Font.Medium
                    color: Md3.cOnSurface
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "刷新"
                    font.pixelSize: Md3.bodyS
                    color: Md3.cPrimary
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
                    spacing: Md3.spS
                    Text {
                        text: modelData.rank
                        font.pixelSize: Md3.bodyS
                        font.weight: modelData.rank <= 3 ? Font.Bold : Font.Normal
                        color: modelData.rank <= 3 ? (Md3.cPrimary)
                                                   : (Md3.cOnSurfaceVar)
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cards.actionLabel(modelData.action)
                        font.pixelSize: Md3.bodyS
                        color: Md3.cOnSurface
                    }
                    Text {
                        text: "成功 " + modelData.ok + " / " + modelData.total
                        font.pixelSize: Md3.labelS
                        color: Md3.cOnSurfaceVar
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !backend.actionRanking || backend.actionRanking.length === 0
                text: "还没有排行数据（连上云端后自动统计）"
                font.pixelSize: Md3.bodyS
                wrapMode: Text.WordWrap
                color: Md3.cOutline
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ── S2 云端存储概况 + S4 最近回执 ──
    AppCard {
        Layout.fillWidth: true
        Layout.preferredHeight: 208
        // 原 variant: "elevated" ⇒ AppCard 默认 shadow: true（极柔阴影）

        ColumnLayout {
            // ⚠️ AppCard 内部已是 ColumnLayout：子项不能 anchors.fill，必须走 Layout
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Md3.spS

            Text {
                text: "云端存储概况"
                font.pixelSize: Md3.titleS
                font.weight: Font.Medium
                color: Md3.cOnSurface
            }
            Text {
                Layout.fillWidth: true
                text: "已知设备 " + (backend.storage.knownDevices !== undefined ? backend.storage.knownDevices : "—")
                      + " · 班级 " + (backend.storage.classes !== undefined ? backend.storage.classes : "—")
                      + " · 事件文件 " + (backend.storage.eventsFiles !== undefined ? backend.storage.eventsFiles : "—")
                font.pixelSize: Md3.bodyS
                wrapMode: Text.WordWrap
                color: Md3.cOnSurfaceVar
            }
            Text {
                Layout.fillWidth: true
                text: "最近回执"
                font.pixelSize: Md3.bodyS
                font.weight: Font.Medium
                color: Md3.cOnSurface
                topPadding: Md3.spXS
            }

            Repeater {
                model: cards.recentResults().slice(0, 4)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Md3.spS

                    Text {
                        text: modelData.t || ""
                        font.pixelSize: Md3.labelS
                        color: Md3.cOutline
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cards.actionLabel(modelData.action) + " · " + (modelData.uid || "")
                        font.pixelSize: Md3.labelS
                        color: Md3.cOnSurfaceVar
                    }
                    Text {
                        text: modelData.ok ? "成功" : "失败"
                        font.pixelSize: Md3.labelS
                        color: modelData.ok
                               ? (Md3.cPrimary)
                               : (Md3.cError)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: cards.recentResults().length === 0
                text: "还没有回执（下发动作后这里会显示执行结果）"
                font.pixelSize: Md3.bodyS
                wrapMode: Text.WordWrap
                color: Md3.cOutline
            }
            Item { Layout.fillHeight: true }
        }
    }
}
