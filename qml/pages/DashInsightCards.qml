// 仪表盘 · 洞察区（S3 动作排行 + S2 云端存储 + S4 最近回执）
//
// 从 shell/MainWindow.qml 内联段抽出（#473 拆件）。横屏双卡并排、竖屏单列堆叠，
// 靠 GridLayout 列数随朝向变，不是两套布局代码。
//
// 数据来源：backend（actionRanking / storage）与宿主的回执流水（host.results）。
// 空态一律写人话（"连上云端后自动统计"），不摆 0 假装有数据。
import QtQuick
import QtQuick.Layouts
import "../components/md3"     // Md3Card

GridLayout {
    id: cards

    property var md3: ({})
    property var host: null           // ctx.window（actionTitleOf / results）
    property bool portrait: false
    property var toastFn: null

    columns: cards.portrait ? 1 : 2
    columnSpacing: md3 ? md3.spM : 16
    rowSpacing: md3 ? md3.spM : 16
    Layout.fillWidth: true
    Layout.bottomMargin: md3 ? md3.spS : 8

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
    Md3Card {
        Layout.fillWidth: true
        Layout.preferredHeight: 208
        md3: cards.md3
        variant: "elevated"

        ColumnLayout {
            anchors.fill: parent
            spacing: cards.md3 ? cards.md3.spS : 8

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "动作排行 · 近 7 天"
                    font.pixelSize: cards.md3 ? cards.md3.titleS : 14
                    font.weight: Font.Medium
                    color: cards.md3 ? cards.md3.cOnSurface : "#1D1B20"
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "刷新"
                    font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                    color: cards.md3 ? cards.md3.cPrimary : "#6750A4"
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
                model: backend.actionRanking ? backend.actionRanking.slice(0, 5) : []
                delegate: RowLayout {
                    required property var modelData
                    // ⚠️ 不用 required property int index：Repeater 的 index 是上下文属性，
                    //    声明成 required 在部分版本取不到值。
                    Layout.fillWidth: true
                    spacing: cards.md3 ? cards.md3.spS : 8

                    Text {
                        text: (index + 1)
                        font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                        font.weight: index < 3 ? Font.Bold : Font.Normal
                        color: index < 3 ? (cards.md3 ? cards.md3.cPrimary : "#6750A4")
                                         : (cards.md3 ? cards.md3.cOnSurfaceVar : "#49454F")
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cards.actionLabel(modelData.action)
                        font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                        color: cards.md3 ? cards.md3.cOnSurface : "#1D1B20"
                    }
                    Text {
                        text: "成功 " + modelData.ok + " / " + modelData.total
                        font.pixelSize: cards.md3 ? cards.md3.labelS : 11
                        color: cards.md3 ? cards.md3.cOnSurfaceVar : "#49454F"
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !backend.actionRanking || backend.actionRanking.length === 0
                text: "还没有排行数据（连上云端后自动统计）"
                font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                wrapMode: Text.WordWrap
                color: cards.md3 ? cards.md3.cOutline : "#79747E"
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ── S2 云端存储概况 + S4 最近回执 ──
    Md3Card {
        Layout.fillWidth: true
        Layout.preferredHeight: 208
        md3: cards.md3
        variant: "elevated"

        ColumnLayout {
            anchors.fill: parent
            spacing: cards.md3 ? cards.md3.spS : 8

            Text {
                text: "云端存储概况"
                font.pixelSize: cards.md3 ? cards.md3.titleS : 14
                font.weight: Font.Medium
                color: cards.md3 ? cards.md3.cOnSurface : "#1D1B20"
            }
            Text {
                Layout.fillWidth: true
                text: "已知设备 " + (backend.storage.knownDevices !== undefined ? backend.storage.knownDevices : "—")
                      + " · 班级 " + (backend.storage.classes !== undefined ? backend.storage.classes : "—")
                      + " · 事件文件 " + (backend.storage.eventsFiles !== undefined ? backend.storage.eventsFiles : "—")
                font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                wrapMode: Text.WordWrap
                color: cards.md3 ? cards.md3.cOnSurfaceVar : "#49454F"
            }
            Text {
                Layout.fillWidth: true
                text: "最近回执"
                font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                font.weight: Font.Medium
                color: cards.md3 ? cards.md3.cOnSurface : "#1D1B20"
                topPadding: cards.md3 ? cards.md3.spXS : 4
            }

            Repeater {
                model: cards.recentResults().slice(0, 4)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: cards.md3 ? cards.md3.spS : 8

                    Text {
                        text: modelData.t || ""
                        font.pixelSize: cards.md3 ? cards.md3.labelS : 11
                        color: cards.md3 ? cards.md3.cOutline : "#79747E"
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cards.actionLabel(modelData.action) + " · " + (modelData.uid || "")
                        font.pixelSize: cards.md3 ? cards.md3.labelS : 11
                        color: cards.md3 ? cards.md3.cOnSurfaceVar : "#49454F"
                    }
                    Text {
                        text: modelData.ok ? "成功" : "失败"
                        font.pixelSize: cards.md3 ? cards.md3.labelS : 11
                        color: modelData.ok
                               ? (cards.md3 ? cards.md3.cPrimary : "#6750A4")
                               : (cards.md3 ? cards.md3.cError : "#B3261E")
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: cards.recentResults().length === 0
                text: "还没有回执（下发动作后这里会显示执行结果）"
                font.pixelSize: cards.md3 ? cards.md3.bodyS : 12
                wrapMode: Text.WordWrap
                color: cards.md3 ? cards.md3.cOutline : "#79747E"
            }
            Item { Layout.fillHeight: true }
        }
    }
}
