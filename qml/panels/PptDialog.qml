// PPT 放映面板（2026-10-27 · I 批 · viewer 侧）
//
// 动线：老师站讲台，最常用「上一页 / 下一页」，其次「跳到第 N 页」，最后「现在第几页」
// ⇒ 排布顺序 状态 → 大按钮 → 页码 → 控制哪个程序。
//
// 口径（用户明确要求）：**没开 PPT 或没在放映 → 如实 failed，绝不假成功**。
// 被控端 `ppt_status` 在那种情况下也回 failed，但 data 里带完整状态
// （app / presenting / slide / total）⇒ 这里按 presented=false 显示"未在放映"中性徽标，
// **不弹红色错误**（否则"没开 PPT"会被渲染成"系统故障"）。
// 设计细节见 ppt_domain.h（口径）与 PptAppSwitch.qml（Office/WPS COM 互斥）。
//
// 拆件：状态卡 → PptStatusCard.qml，三档切换 → PptAppSwitch.qml（QML 单文件红线 200 行）。
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
    width: 520
    height: 396
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // ── 状态（全部来自被控端回执，界面不自己猜）──────────────────────────
    // ⚠️ 不能叫 state / data / status：分别是 Item 状态机、默认子元素列表、Popup 既有属性，
    //    重名会让整页静默失效（G 批实证过）。
    property string appLabel: ""
    property bool   presenting: false
    property int    curPage: 0
    property int    totalPages: 0
    property string preferred: "auto"
    property string hint: "点「刷新」看一下这台设备的放映情况"
    property string askUid: ""            // 本次问的是哪台（回执按它过滤，防串台）

    function ask(action, params) {
        const uid = backend.currentUid
        if (!uid) { dlg.hint = "先在控制页选中一台设备"; return }
        dlg.askUid = uid
        backend.sendAction(action, params || {})
    }

    function refresh() { dlg.ask("ppt_status", {}) }

    function switchApp(a) {
        if (dlg.preferred === a) return
        dlg.ask("ppt_app", { "app": a })
    }

    function gotoPage() {
        const n = parseInt(pageField.text, 10)
        if (!(n >= 1)) { dlg.hint = "页码从 1 开始"; return }
        dlg.ask("ppt_goto", { "index": n })
    }

    // 每次打开都**重新问真实状态**（不缓存：PPT 可能刚被关掉）
    onOpened: refresh()

    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, error, detail, info) {
            if (action.indexOf("ppt_") !== 0) return
            if (dlg.askUid !== "" && uid !== dlg.askUid) return   // 别台的 PPT 回执不掺进来

            if (state === "sent") { dlg.hint = "指令已发出，等设备回执…"; return }
            if (state !== "executed") { dlg.hint = detail ? detail : "指令没送到设备"; return }

            if (info && info.app !== undefined) {
                dlg.appLabel   = info.appLabel !== undefined ? info.appLabel : ""
                dlg.presenting = info.presenting === true
                dlg.curPage    = info.slide !== undefined ? info.slide : 0
                dlg.totalPages = info.total !== undefined ? info.total : 0
            }
            if (info && info.preferred !== undefined) dlg.preferred = info.preferred
            if (action === "ppt_app" && info && info.saved === false)
                dlg.hint = "偏好已生效，但没写进设备的 ppt.json（设备重启后会丢）"

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
            title: "PPT 放映"
            onClosed: dlg.close()
        }

        PptStatusCard {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.preferredHeight: 86
            theme: dlg.theme
            appLabel: dlg.appLabel
            presenting: dlg.presenting
            curPage: dlg.curPage
            totalPages: dlg.totalPages
            hint: dlg.hint
        }

        // ── 翻页大按钮 ────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 12
            spacing: 10

            Btn {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                theme: dlg.ctx ? dlg.ctx.window.th : null
                text: "上一页"
                disabled: !dlg.presenting
                tip: dlg.presenting ? "放映上一页" : "设备当前没有在放映"
                onClicked: dlg.ask("ppt_prev", {})
            }
            Btn {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                theme: dlg.ctx ? dlg.ctx.window.th : null
                text: "下一页"
                strong: true
                disabled: !dlg.presenting
                tip: dlg.presenting ? "放映下一页" : "设备当前没有在放映"
                onClicked: dlg.ask("ppt_next", {})
            }
        }

        // ── 页码跳转 ──────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 10
            spacing: 10

            InputField {
                id: pageField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                th: dlg.ctx ? dlg.ctx.window.th : null
                placeholderText: "跳到第几页"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 9999 }
                onAccepted: dlg.gotoPage()
            }
            Btn {
                Layout.preferredWidth: 88
                theme: dlg.ctx ? dlg.ctx.window.th : null
                text: "跳转"
                disabled: !dlg.presenting
                onClicked: dlg.gotoPage()
            }
            Btn {
                Layout.preferredWidth: 88
                theme: dlg.ctx ? dlg.ctx.window.th : null
                text: "刷新"
                onClicked: dlg.refresh()
            }
        }

        PptAppSwitch {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 12
            theme: dlg.theme
            preferred: dlg.preferred
            onPicked: function (app) { dlg.switchApp(app) }
        }

        Item { Layout.fillHeight: true }
    }
}
