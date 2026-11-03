// IslandTileGrid.qml —— 灵动岛「平铺」模式（2026-11-03 · m14970 ① · 用户裁决）
// 多面同屏网格排布，无翻页概念；与轮播（IslandRoot fpFlick）二选一，整岛切换。
// 面实体组件经 components 映射注入（id → Component），本文件不 import 任何业务面；
// 「概」(id 0) 是空面，平铺里没有信息量 ⇒ 过滤掉。点击某格 = 回轮播并翻到那一页。
//
// ⚠️ 缩放口径（2026-11-04 实测补）：各面组件是**按整幅展开区**设计的，直接
//   anchors.fill 塞进半宽格子会把右半边裁掉 —— 实测「笔」面的橡皮/撤销按钮被切在
//   格线外、「设」面的滑块文字被挤成两行。所以改成「按设计尺寸渲染 + 等比 contain」：
//   宁可留白也不裁字。代价是小格子里字会变小（"总览"的固有代价，点进去仍是全尺寸）。
import QtQuick

Item {
    id: tileRoot

    property var th: null
    property var faceModel: []            // islandLayout.faceList() 原样传入
    property var components: ({})         // { 7: compToday, ... } 由宿主注入
    signal facePicked(int id)

    // 面组件的设计画布尺寸 = 展开态几何（island.cpp:429 展开宽 kBarW=520；
    //   island.h Shape::Expanded=320）。这两个数必须与 C++ 同步改，否则 contain
    //   会算错比例（第一次写成 680 就是错的，来自面板/媒体分支的宽度，不是通知展开）。
    readonly property real designW: 520
    readonly property real designH: 320
    readonly property int tagH: 22         // 左上角面签占位高

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

                // 面画布：先按设计尺寸渲染，再整体缩到格子内（contain）。
                // ⚠️ 不给 anchors.fill：宽高同时被 anchor 与显式值驱动会「属性打架」，
                //   QML 会静默取一边，比例算出来是错的。这里只居中 + 显式设计尺寸。
                Item {
                    id: canvas
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: tileRoot.tagH / 2
                    width: tileRoot.designW
                    height: tileRoot.designH
                    // contain：取两边缩放比的小者，绝不把内容挤出格子
                    readonly property real fit: Math.min((tileCell.width - 8) / tileRoot.designW,
                                                        (tileCell.height - tileRoot.tagH - 8) / tileRoot.designH)
                    scale: Math.max(0.1, fit)
                           * (tileCell.modelData.scale ? tileCell.modelData.scale / 100 : 1)
                    transformOrigin: Item.Center

                    IslandFaceLoader {
                        anchors.fill: parent
                        faceId: tileCell.modelData.id
                        components: tileRoot.components
                    }
                }

                TapHandler {
                    onTapped: tileRoot.facePicked(tileCell.modelData.id)
                }
            }
        }
    }
}
