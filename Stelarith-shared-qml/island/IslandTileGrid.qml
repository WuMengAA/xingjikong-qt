// IslandTileGrid.qml —— 灵动岛「平铺」模式（2026-11-03 · m14970 ① · 用户裁决）
// 多面同屏网格排布，无翻页概念；与轮播（IslandRoot fpFlick）二选一，整岛切换。
// 面实体组件经 components 映射注入（id → Component），本文件不 import 任何业务面；
// 「概」(id 0) 是空面，平铺里没有信息量 ⇒ 过滤掉。点击某格 = 回轮播并翻到那一页。
import QtQuick

Item {
    id: tileRoot

    property var th: null
    property var faceModel: []            // islandLayout.faceList() 原样传入
    property var components: ({})         // { 7: compToday, ... } 由宿主注入
    signal facePicked(int id)

    readonly property var tiles: (faceModel || []).filter(function (e) { return e.id !== 0 })
    readonly property int cols: Math.max(2, Math.min(4, Math.ceil(Math.sqrt(tiles.length))))
    readonly property int rowCount: Math.ceil(Math.sqrt(tiles.length))

    Grid {
        id: tileGrid
        anchors.fill: parent
        columns: tileRoot.cols
        rows: tileRoot.rowCount
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            model: tileRoot.tiles
            delegate: Item {
                id: tileCell
                required property var modelData
                width: (tileGrid.width - (tileGrid.columns - 1) * tileGrid.columnSpacing) / tileGrid.columns
                height: (tileGrid.height - (tileGrid.rows - 1) * tileGrid.rowSpacing) / tileGrid.rows

                // 面签（小胶囊，左上角）—— 平铺下老师要一眼知道每格是什么
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 4
                    width: tagText.implicitWidth + 12
                    height: 18
                    radius: 9
                    color: tileRoot.th ? tileRoot.th.surfaceVariant : "#EEEEEE"
                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: tileCell.modelData.label
                        font.pixelSize: 11
                        color: tileRoot.th ? tileRoot.th.textPrimary : "#222222"
                    }
                }

                IslandFaceLoader {
                    anchors.fill: parent
                    anchors.topMargin: 22     // 让出面签
                    faceId: tileCell.modelData.id
                    scalePercent: tileCell.modelData.scale
                    components: tileRoot.components
                }

                TapHandler {
                    onTapped: tileRoot.facePicked(tileCell.modelData.id)
                }
            }
        }
    }
}
