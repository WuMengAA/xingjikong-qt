import QtQuick 2.15
import "."

// 灵动岛 · 历史通知「紧凑预览」面（面 2）
//
// 从 IslandRoot 内联板抽离（2026-10-27 · 统一布局规范 Phase 1）。
// 复用 IslandListRow：消除「各面手搓 48px 行」的漂移；溢出给余量提示而非滚动
// （面板高度有限，多了给提示，完整列表回全量历史面板看）。
//
// 接口：
//   th          主题令牌（必传）
//   history     [{icon,title,desc,timeFull,timeStr,read,id}...]（与 bridge.history 同形）
//   bridgeRef   桥（showHistoryById / showHistoryItem 回调）
//   maxItems    最多平铺条数（默认 4）
Item {
    id: face
    property var th: null
    property var history: []
    property var bridgeRef: null
    property int maxItems: 4

    readonly property int shown: Math.min(face.maxItems, face.history.length)

    Column {
        id: col
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        Repeater {
            model: face.shown
            delegate: IslandListRow {
                width: col.width
                th: face.th
                icon: (face.history[index] && face.history[index].icon) || "bell"
                title: face.history[index] ? (face.history[index].title || "") : ""
                desc: face.history[index] ? (face.history[index].desc || "") : ""
                // 紧凑预览没有分组标题兜日期 ⇒ 用 timeFull（今天给 HH:mm，更早带前缀）
                time: face.history[index]
                      ? (face.history[index].timeFull || face.history[index].timeStr || "")
                      : ""
                unread: face.history[index] ? (face.history[index].read !== true) : false
                onClicked: {
                    if (!face.bridgeRef) return
                    var it = face.history[index]
                    if (face.bridgeRef.showHistoryById && it && it.id)
                        face.bridgeRef.showHistoryById(it.id)
                    else if (face.bridgeRef.showHistoryItem)
                        face.bridgeRef.showHistoryItem(index)
                }
            }
        }

        Text {
            visible: face.history.length > face.maxItems
            anchors.left: parent.left
            anchors.leftMargin: 4
            text: "还有 " + (face.history.length - face.maxItems) + " 条（回概览面看完整列表）"
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: 10
        }
    }
}
