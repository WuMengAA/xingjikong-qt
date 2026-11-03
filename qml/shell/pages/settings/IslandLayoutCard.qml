// 被控端 · 设置页「灵动岛组件」卡片（2026-11-03）
//
// 用户 m09876 原话：「多一些控件还有可配置可拖动组件」。
// 改造前岛的面清单写死在 `IslandRoot.qml:308`（概/今/媒/笔/设/自），
// 老师既不能关掉不看的、也不能把常用的挪到最前。这里把三件事一起给：
//   · 开关：某个面压根不出现（比如不用批注的班）
//   · 拖动：按住左侧 ≡ 上下拖，松手即定序（ClassIsland 的"组件排序"就是这个）
//   · 缩放：面内容 80%~140%（讲台远看要更大、小屏要省地方）
//
// ── 为什么拖动自己算而不用 ListView ──────────────────────────────────
//   ① 被控端部署**不含 QtQuick Controls 运行库**（真机实证 `ScrollBar is not a type`
//      ⇒ 面板空白），ListView 的 displaced 动画在无 Controls 风格时退化成瞬移；
//   ② 行高固定 64，位置 = index*64 + 拖拽偏移，二十行算得准；
//      ListView 的 delegate 复用会在拖动时重建行、把 pressed 状态弄丢。
//
// ── 为什么状态全在 C++（islandLayout）────────────────────────────
//   §10.9：QML 只画图。顺序/开关/缩放要跨重启，落 QSettings 才有意义。
//   设置页与岛壳是两个独立 engine，QML 侧存状态必然出现"设置页改了、岛还是老样"。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

AppCard {
    id: card

    // C++ `IslandLayoutModel`（AgentShell 与岛壳共用同一实例）。
    // typeof 守卫不能省：探针/极端环境里它是未定义标识符，裸引用直接 ReferenceError。
    readonly property var lay: (typeof islandLayout !== "undefined" && islandLayout !== null)
        ? islandLayout : null
    readonly property int rowH: 64
    readonly property int rowsN: lay ? lay.count : 0
    property int dragRow: -1
    property real dragDy: 0

    /** 拖到哪儿：以起点行为基准按偏移取整，夹在合法范围内。 */
    function targetRowOf(from, dy) {
        if (from < 0 || rowsN <= 0) return -1
        return Math.max(0, Math.min(rowsN - 1, from + Math.round(dy / rowH)))
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        AppCardTitle { text: "灵动岛组件" }
        Text {
            Layout.fillWidth: true
            Layout.bottomMargin: 10
            text: card.lay
                ? (card.lay.summary + "　按住左侧 ≡ 上下拖动可排序")
                : "当前环境未注入组件布局模型"
            color: Theme.color.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // 行容器用绝对定位（不是 Column）—— 拖动要能自由改 y，Column 会抢管
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: card.rowsN * card.rowH

            Repeater {
                model: card.lay
                delegate: IslandLayoutRow {
                    // index / model 由 IslandLayoutRow 自己声明（required），这里不重复声明 ——
                    // 重复声明会触发 property-override 告警，且容易让人以为有两份。
                    rowH: card.rowH
                    dragging: card.dragRow === index
                    dragDy: card.dragDy
                    shift: {
                        if (card.dragRow < 0 || card.dragRow === index) return 0
                        const t = card.targetRowOf(card.dragRow, card.dragDy)
                        if (card.dragRow < t && index > card.dragRow && index <= t) return -card.rowH
                        if (card.dragRow > t && index < card.dragRow && index >= t) return card.rowH
                        return 0
                    }
                    onDragStarted: { card.dragRow = index; card.dragDy = 0 }
                    onDragMoved: function (dy) { card.dragDy = dy }
                    onDragFinished: function (dy) {
                        const from = card.dragRow
                        const to = card.targetRowOf(from, dy)
                        card.dragRow = -1
                        card.dragDy = 0
                        if (from >= 0 && to >= 0 && to !== from && card.lay) card.lay.moveRow(from, to)
                    }
                    onDragAborted: { card.dragRow = -1; card.dragDy = 0 }
                    onScaleDown: if (card.lay) card.lay.setScaleById(model.faceId, model.scale - 10)
                    onScaleUp: if (card.lay) card.lay.setScaleById(model.faceId, model.scale + 10)
                    onToggled: if (card.lay) card.lay.setEnabledById(model.faceId, !model.enabled)
                }
            }
        }

        // 显示方向（2026-11-04 · 用户 m14970「不要仅限于列表式的展示」）
        // 二选一，整岛切换（不是 per-face）：轮播=一次一页横滑；平铺=多面同屏网格。
        // 为什么不做成"每面可选"：面组件按 520×320 设计，per-face 混排会让同一屏里
        // 格子尺寸不一致，平铺的网格线立刻变锯齿 —— 视觉代价大于收益。
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 8
            Text {
                text: "显示方向"
                color: Theme.color.textPrimary
                font.pixelSize: 13
            }
            AppButton {
                text: "轮播"
                size: "sm"
                variant: (card.lay && card.lay.direction === "carousel") ? "solid" : "secondary"
                onClicked: if (card.lay) card.lay.setDirection("carousel")
            }
            AppButton {
                text: "平铺"
                size: "sm"
                variant: (card.lay && card.lay.direction === "tiled") ? "solid" : "secondary"
                onClicked: if (card.lay) card.lay.setDirection("tiled")
            }
            Text {
                Layout.fillWidth: true
                text: (card.lay && card.lay.direction === "tiled")
                      ? "多面同屏，一眼看全；点子可放大" : "一次一页，左右滑"
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "关掉的面不会出现在岛上，也不会被左右滑到；「概」即使关掉也保留 —— "
                      + "它是通知的落点，全关会让展开区变空白。"
                color: Theme.color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            AppButton {
                text: "恢复默认"
                size: "sm"
                variant: "secondary"
                onClicked: if (card.lay) card.lay.resetToDefault()
            }
        }
    }
}
