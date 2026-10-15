// 星集控管理端 · 互动审批面板（2026-10-15 · 灵动岛扩展组件6 配套）
//
// 用户点单：「管理端审批面板 UI，灵动岛使用被控端一模一样的」。
//   · 面板形态 = 独立弹窗（用户裁决），仿 NotifyDialog/FilePushDialog 的范式；
//   · 面板右侧那颗岛**就是被控端那颗** —— 真源在根仓库
//     `Stelarith-shared-qml/island/IslandRoot.qml`，两端共用同一份文件，
//     桥走 main.cpp 注入的上下文属性 `bridge`（InteractBridge）。
//
// 数据面：InteractBridge → 云端 HTTP /api/interact/*（见 interactbridge.cpp）。
//   左列表 = 待审点歌 / 待审祝福（可按 state 筛），右预览 = 选中条目在教室机上的样子。
//   反馈页只读（点赞汇总 + 按班 Top）—— 反馈是"统计"，没有"审批"这个动作。
//
// 为什么列表用 Repeater 内容 + ListView：待审量可能上百（三表容量护栏 500），
//   ListView 的虚拟化是真需要；而 tab 只有 3 个，手写足够。

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "components"      // Btn（项目唯一按钮组件）
import "island"          // IslandRoot —— 与被控端逐字同源的那份

Popup {
    id: dlg
    property var theme: null

    // ⚠️ 与被控端 IslandRoot.qml 同款防御：QML 的名字解析里，对象自身的属性优先于
    //    上下文属性。起个不冲突的名字（br），免得 `bridge` 被自身属性遮蔽。
    readonly property var br: (typeof bridge !== "undefined" && bridge !== null) ? bridge : null

    property string tab: "song"       // song | blessing | feedback
    property int selIndex: -1

    readonly property var items: !dlg.br ? []
        : (dlg.tab === "song" ? dlg.br.songRequests
                              : (dlg.tab === "blessing" ? dlg.br.blessings : []))
    readonly property var sel: (dlg.selIndex >= 0 && dlg.selIndex < dlg.items.length)
                               ? dlg.items[dlg.selIndex] : null

    readonly property color cFg:   dlg.theme ? dlg.theme.fg : "#FAFAFA"
    readonly property color cSub:  dlg.theme ? dlg.theme.fg4 : "#8A8A8A"
    readonly property color cLine: dlg.theme ? dlg.theme.stroke : "#333333"
    readonly property color cInv:  dlg.theme ? dlg.theme.inv : "#F0F0F0"
    readonly property color cWin:  dlg.theme ? dlg.theme.win : "#0A0A0A"
    readonly property color cHov:  dlg.theme ? dlg.theme.hover : "#1E1E1E"
    readonly property color cOk:   dlg.theme ? dlg.theme.ok : "#2E9E5B"
    readonly property color cErr:  dlg.theme ? dlg.theme.err : "#D64545"

    anchors.centerIn: parent
    width: 960
    height: 620
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme ? dlg.theme.cream : "#141414"
        radius: dlg.theme ? dlg.theme.rCard : 12
        border.color: dlg.cLine
        border.width: 1
    }

    onOpened: {
        dlg.selIndex = -1
        if (dlg.br) { dlg.br.light = dlg.theme ? !dlg.theme.dark : false; dlg.br.reload() }
    }
    onClosed: if (dlg.br) dlg.br.clearPreview()

    function stateLabel(s) {
        if (s === "pending")   return "待审"
        if (s === "approved")  return "已通过"
        if (s === "rejected")  return "已驳回"
        if (s === "played")    return "已播"
        if (s === "cancelled") return "已取消"
        return s || ""
    }

    function doHandle(action) {
        if (!dlg.br || !dlg.sel) return
        if (dlg.tab === "song")          dlg.br.handleSong(dlg.sel.id, action)
        else if (dlg.tab === "blessing") dlg.br.handleBlessing(dlg.sel.id, action)
        dlg.selIndex = -1
        dlg.br.clearPreview()
    }

    // ── 头部 ────────────────────────────────────────────────────
    Text {
        id: headTitle
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.top: parent.top
        anchors.topMargin: 16
        text: "互动审批"
        color: dlg.cFg
        font.pixelSize: 16
        font.weight: Font.Bold
    }
    Text {
        id: headState
        anchors.left: headTitle.right
        anchors.leftMargin: 12
        anchors.verticalCenter: headTitle.verticalCenter
        text: {
            if (!dlg.br) return "内部错误：没有桥"
            if (dlg.br.state === "loading") return "正在从云端拉取…"
            if (dlg.br.state === "error")   return "拉取失败：" + dlg.br.error
            return "待审点歌 " + dlg.br.songRequests.length + " · 待审祝福 " + dlg.br.blessings.length
        }
        color: (dlg.br && dlg.br.state === "error") ? dlg.cErr : dlg.cSub
        font.pixelSize: 12
    }
    // 刷新 + 关闭
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.top: parent.top
        anchors.topMargin: 14
        spacing: 8
        Btn {
            width: 72
            theme: dlg.theme
            text: "刷新"
            disabled: !dlg.br || dlg.br.state === "loading"
            onClicked: if (dlg.br) dlg.br.reload()
        }
        Btn {
            width: 72
            theme: dlg.theme
            text: "关闭"
            onClicked: dlg.close()
        }
    }

    // ── 页签 ────────────────────────────────────────────────────
    // 筛选口径：老师打开面板 99% 是想看"还没处理的" ⇒ 默认筛 pending（云端支持 ?state=）。
    Row {
        id: tabRow
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.top: headTitle.bottom
        anchors.topMargin: 12
        spacing: 8
        Repeater {
            model: [
                { k: "song",     label: "待审点歌" },
                { k: "blessing", label: "待审祝福" },
                { k: "feedback", label: "反馈汇总" }
            ]
            delegate: Rectangle {
                id: tabItem
                required property var modelData
                width: 96
                height: 30
                radius: 8
                color: dlg.tab === tabItem.modelData.k ? dlg.cInv : "transparent"
                border.color: dlg.tab === tabItem.modelData.k ? "transparent" : dlg.cLine
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: tabItem.modelData.label
                    color: dlg.tab === tabItem.modelData.k ? dlg.cWin : dlg.cFg
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dlg.tab = tabItem.modelData.k
                        dlg.selIndex = -1
                        if (dlg.br) dlg.br.clearPreview()
                    }
                }
            }
        }
    }

    // ── 左：列表 / 汇总 ─────────────────────────────────────────
    Rectangle {
        id: listPane
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.top: tabRow.bottom
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: 520
        radius: 10
        color: dlg.theme ? dlg.theme.panel : "#101010"
        border.color: dlg.cLine
        border.width: 1
        clip: true

        // 反馈汇总（只读）：反馈是统计不是待办，没有"通过/驳回"。
        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            visible: dlg.tab === "feedback"
            Text {
                text: "点赞总数　" + (dlg.br ? (dlg.br.feedbackSummary.likeTotal || 0) : 0)
                color: dlg.cFg; font.pixelSize: 15; font.weight: Font.Medium
            }
            Text {
                text: "近 24 小时　" + (dlg.br ? (dlg.br.feedbackSummary.likeToday || 0) : 0)
                color: dlg.cFg; font.pixelSize: 15; font.weight: Font.Medium
            }
            Text {
                text: "反馈条数　" + (dlg.br ? (dlg.br.feedbackSummary.feedbackTotal || 0) : 0)
                color: dlg.cFg; font.pixelSize: 15; font.weight: Font.Medium
            }
            Rectangle { width: parent.width; height: 1; color: dlg.cLine }
            Text { text: "按班级（点赞 Top）"; color: dlg.cSub; font.pixelSize: 12 }
            Repeater {
                model: dlg.br ? (dlg.br.feedbackSummary.byClass || []) : []
                delegate: Text {
                    id: clsRow
                    required property var modelData
                    text: "　" + clsRow.modelData.classCode + "　×" + clsRow.modelData.n
                    color: dlg.cSub; font.pixelSize: 13
                }
            }
        }

        // 待审列表
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 8
            visible: dlg.tab !== "feedback"
            clip: true
            model: dlg.items
            spacing: 6
            delegate: Rectangle {
                id: rowItem
                required property var modelData
                required property int index
                width: list.width
                height: 58
                radius: 8
                color: dlg.selIndex === rowItem.index ? dlg.cHov
                       : (rowHover.containsMouse ? dlg.cHov : "transparent")
                border.color: dlg.selIndex === rowItem.index ? dlg.cSub : "transparent"
                border.width: 1

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: dlg.tab === "song"
                              ? (rowItem.modelData.title
                                 + (rowItem.modelData.artist ? " - " + rowItem.modelData.artist : ""))
                              : rowItem.modelData.text
                        color: dlg.cFg
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: (rowItem.modelData.classCode || "未分班") + " · "
                              + rowItem.modelData.uid + " · "
                              + dlg.stateLabel(rowItem.modelData.state)
                        color: dlg.cSub
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dlg.selIndex = rowItem.index
                        if (!dlg.br) return
                        const m = rowItem.modelData
                        if (dlg.tab === "song")
                            dlg.br.preview("song", m.title, m.artist || "")
                        else
                            dlg.br.preview("blessing", m.text,
                                           "来自 " + (m.classCode || m.uid))
                    }
                }
            }
            // 空态：说清"是没数据，还是没拉回来"（两者观感完全不同）
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: (dlg.br && dlg.br.state === "loading") ? "正在拉取…"
                    : (dlg.br && dlg.br.state === "error") ? "拉取失败，点右上「刷新」重试"
                    : "没有待审的" + (dlg.tab === "song" ? "点歌" : "祝福")
                color: dlg.cSub
                font.pixelSize: 13
            }
        }
    }

    // ── 右：岛预览 + 详情 + 审批 ────────────────────────────────
    Item {
        id: rightPane
        anchors.left: listPane.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.top: tabRow.bottom
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20

        Text {
            id: previewCap
            anchors.left: parent.left
            anchors.top: parent.top
            text: "学生在教室机上会看到"
            color: dlg.cSub
            font.pixelSize: 12
        }

        // 岛预览：真源 = Stelarith-shared-qml/island/IslandRoot.qml（与被控端同一份）。
        // 640×320 是岛的展开态硬尺寸（C++ geometryForForm 定），这里缩到 0.5 塞进面板。
        Item {
            id: islandBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: previewCap.bottom
            anchors.topMargin: 10
            height: 168
            clip: true

            IslandRoot {
                visible: !!(dlg.br && dlg.br.previewKind)
                width: 640
                height: 320
                scale: 0.5
                transformOrigin: Item.TopLeft
            }

            Text {
                anchors.centerIn: parent
                visible: !(dlg.br && dlg.br.previewKind)
                text: "← 点左边的条目，看学生在教室机上看到的样子"
                color: dlg.cSub
                font.pixelSize: 12
            }
        }

        // 详情
        Column {
            id: detailCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: islandBox.bottom
            anchors.topMargin: 14
            spacing: 6
            visible: !!dlg.sel
            Text {
                width: parent.width
                text: (dlg.tab === "song" ? "点歌 #" : "祝福 #") + (dlg.sel ? dlg.sel.id : "")
                color: dlg.cFg
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: dlg.sel
                      ? ("来自 " + dlg.sel.uid
                         + (dlg.sel.classCode ? "（" + dlg.sel.classCode + "）" : "")
                         + " · " + dlg.stateLabel(dlg.sel.state))
                      : ""
                color: dlg.cSub
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        // 通过 / 驳回
        Row {
            id: actRow
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            spacing: 10
            visible: !!dlg.sel && dlg.tab !== "feedback"
            Btn {
                width: 96
                theme: dlg.theme
                text: "驳回"
                onClicked: dlg.doHandle("reject")
            }
            Btn {
                width: 96
                theme: dlg.theme
                text: "通过"
                strong: true
                onClicked: dlg.doHandle("approve")
            }
        }
    }
}
