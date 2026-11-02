// 批注 / 白板面板（2026-10-27 · H 批 · viewer 侧）
//
// 动线：老师站讲台 → 点「开始批注」→ 在屏幕上圈画 → 点「结束批注」。
// 所以排布顺序是 状态 → 开关 → 笔 → 白板 → 页，和手伸过去的距离一致。
//
// 三条设计口径（都是踩过的）：
// 1. **界面不自己猜状态**：全部来自被控端回执（active/tool/color/width/board/pageIndex…），
//    回执里没有的字段一律保持原值。
//    唯一的例外是颜色/粗细的"本地回显"——点色块必须立刻有反馈，
//    但设备回执一到就以回执为准（回执是权威，本地只是先画一下）。
// 2. **不解析 status 字符串**：那是给人看的。结构化字段由被控端单独下发，
//    面板只读字段（见 main.cpp 的 annotate_ 分支）。
// 3. **批注层无焦点**：落笔不激活窗口，PPT 的键盘翻页笔照常可用 —— 面板文案要把
//    这条讲清楚，否则老师会以为"点了批注 PPT 就不能翻了"。
//
// 拆件（QML 单文件 200 行红线）：AnnotateToolRow / AnnotateBoardRow / AnnotatePageRow。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../island"       // SvgIcon（唯一真源在共享岛目录）

Popup {
    id: dlg

    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 540
    height: 452
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // ── 设备侧状态（全部来自回执）──────────────────────────────────────
    // ⚠️ 不能叫 state / status / data：分别是 Item 状态机、Popup 既有属性、默认子元素列表。
    property bool   ovActive: false
    property string toolName: "pen"
    property string penColor: "#E53935"
    property int    penWidth: 4
    property string boardBg: ""
    property int    pageIdx: 0
    property int    pageCnt: 0
    property int    strokes: 0
    property string rawStatus: ""
    property string hint: "先在控制页选中一台设备"
    property string askUid: ""            // 本次问的是哪台（回执按它过滤，防串台）

    function ask(action, params) {
        const uid = backend.currentUid
        if (!uid) { dlg.hint = "先在控制页选中一台设备"; return }
        dlg.askUid = uid
        backend.sendAction(action, params || {})
    }

    function refresh() { dlg.ask("annotate_status", {}) }

    function toggleOverlay() {
        dlg.ask(dlg.ovActive ? "annotate_stop" : "annotate_start", {})
    }

    // 每次打开都重新问真实状态（设备可能刚被别的老师收掉批注层）
    onOpened: refresh()

    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, error, detail, info) {
            if (action.indexOf("annotate_") !== 0) return
            if (dlg.askUid !== "" && uid !== dlg.askUid) return

            if (state === "sent") { dlg.hint = "指令已发出，等设备回执…"; return }
            if (state !== "executed") { dlg.hint = detail ? detail : "指令没送到设备"; return }

            if (info) {
                if (info.active !== undefined)    dlg.ovActive  = info.active === true
                if (info.tool !== undefined)      dlg.toolName  = String(info.tool)
                if (info.color !== undefined)     dlg.penColor  = String(info.color)
                if (info.width !== undefined)     dlg.penWidth  = Math.round(Number(info.width))
                if (info.board !== undefined)     dlg.boardBg   = String(info.board)
                if (info.pageIndex !== undefined) dlg.pageIdx   = info.pageIndex
                if (info.pageCount !== undefined) dlg.pageCnt   = info.pageCount
                if (info.strokes !== undefined)   dlg.strokes   = info.strokes
                if (info.status !== undefined)    dlg.rawStatus = String(info.status)
            }

            if (result === "done")
                dlg.hint = (info && info.note) ? info.note : "完成"
            else
                dlg.hint = error ? error : "失败（设备没给原因）"
        }
    }

    background: Rectangle {
        color: theme ? theme.panel : "#1D1B20"
        radius: theme ? theme.rCard : 12
        border.color: theme ? theme.card : "#2B2930"
        border.width: 0
    }

    contentItem: ColumnLayout {
        spacing: 0

        PptDialogHeader {
            Layout.fillWidth: true
            theme: dlg.theme
            title: "批注 / 白板"
            onClosed: dlg.close()
        }

        // ── 状态 + 开关（状态行拆到 AnnotateStatusLine：撞 200 行红线）──────────
        AnnotateStatusLine {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 4
            theme: dlg.theme
            ovActive: dlg.ovActive
            pageIdx: dlg.pageIdx
            pageCnt: dlg.pageCnt
            strokes: dlg.strokes
            hint: dlg.hint
        }

        Btn {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 10
            Layout.preferredHeight: 48
            theme: dlg.ctx ? dlg.ctx.window.th : null
            text: dlg.ovActive ? "结束批注" : "开始批注"
            strong: !dlg.ovActive
            tip: dlg.ovActive ? "收回批注层，屏幕恢复正常" : "在设备屏幕上层叠一层透明画布"
            onClicked: dlg.toggleOverlay()
        }

        AnnotateToolRow {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 12
            theme: dlg.theme
            tool: dlg.toolName
            penColor: dlg.penColor
            penWidth: dlg.penWidth
            enabled: dlg.ovActive
            onPickedTool: function (t) { dlg.ask("annotate_tool", { "tool": t }) }
            onPickedColor: function (c) { dlg.penColor = c; dlg.ask("annotate_color", { "color": c }) }
            onPickedWidth: function (w) { dlg.penWidth = w; dlg.ask("annotate_width", { "width": w }) }
        }

        AnnotateBoardRow {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 10
            theme: dlg.theme
            boardBg: dlg.boardBg
            enabled: dlg.ovActive
            onPicked: function (b) { dlg.ask("annotate_board", { "board": b }) }
            onUndo: dlg.ask("annotate_undo", {})
            onRedo: dlg.ask("annotate_redo", {})
            onClearAll: dlg.ask("annotate_clear", {})
        }

        AnnotatePageRow {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 10
            theme: dlg.theme
            pageIdx: dlg.pageIdx
            pageCnt: dlg.pageCnt
            enabled: dlg.ovActive
            onPrev: dlg.ask("annotate_page_prev", {})
            onNext: dlg.ask("annotate_page_next", {})
            onAdd: dlg.ask("annotate_page_add", {})
        }

        Item { Layout.fillHeight: true }

        // 原始状态行（排障用：hook=0 说明键盘逃生口没装上，现场可据此报障）
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 10
            visible: dlg.rawStatus !== ""
            text: dlg.rawStatus
            color: dlg.theme ? dlg.theme.fg3 : "#71717A"
            font.pixelSize: 11
            font.family: "Consolas"
            elide: Text.ElideRight
        }
    }
}
