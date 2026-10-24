// 班级工作台 · 发布中心（P1 第一刀：通知三件套 + 课后作业）—— 页级模态弹窗
//
// 为什么是模态而不是右栏第四格：右栏已三格（管理/动作/时间线），再叠一格会把
// 整页撑爆；发布是一次性表单动作，收起后不占版面（对齐「掌上看班」发布弹窗）。
// 视觉语言沿用 Md3Dialog：50% 黑 scrim + 居中卡 + 进场缩放淡入。
// 表单（类型/标题/正文）抽到 ClassPublishForm，本件只管：开合、校验、发请求、收回执。
//
// 铁律：
//   · 只在已订阅（有当前班）时可发 —— 操作对象是当前班，未订阅整组禁用；
//   · 标题/正文至少一个非空（云端 .trim() 后判空）⇒ 否则按钮禁用，不让人点空发；
//   · 失败必 toast 且**保留弹窗内容**（让人改完重试，绝不静默清空）；成功才关闭；
//   · 重认证门（403 REAUTH_REQUIRED）：后端接去弹重登提示，本件只解 sending 锁。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshBtn
import "../../island"               // SvgIcon

Item {
    id: dlg

    property var md3: ({})
    property var cs: null            // ClassService（取当前班码/订阅态）
    property var toastFn: null       // (msg, isErr) 外壳页级 toast
    property bool opened: false
    /** 已发且在等回执（防连点：云端 class-event 有 1s 限流，UI 必须先挡一道）。 */
    property bool sending: false

    signal closed()

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""
    readonly property bool canSend: dlg.subbed && !dlg.sending
                                   && (form.titleText.trim() !== "" || form.bodyText.trim() !== "")
    /** 动效时长：令牌优先，({}) 兜底 200（与 Md3Dialog 同口径）。 */
    readonly property int animDur: (dlg.md3 && dlg.md3.durShort !== undefined) ? dlg.md3.durShort : 200

    visible: opened
    anchors.fill: parent
    onOpenedChanged: if (dlg.opened) openAnim.restart()

    function toast(msg, isErr) { if (dlg.toastFn) dlg.toastFn(msg, isErr === true) }

    /** 班级管理调用层（backend.classAdmin，C++ ClassAdmin）。 */
    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    function open() { form.reset(); dlg.sending = false; dlg.opened = true }
    function close() { dlg.opened = false; dlg.closed() }

    function doPublish() {
        const a = dlg.admin()
        if (!a || !dlg.canSend) return
        dlg.sending = true
        a.publish(dlg.code, form.type, form.titleText.trim(), form.bodyText.trim())
    }

    // 发布回执：只认 op="publish"（其余写操作归 ClassAdminPane，两处不混）。
    Connections {
        target: dlg.admin()
        ignoreUnknownSignals: true
        function onOpFinished(op, ok, message) {
            if (op !== "publish") return
            dlg.sending = false
            if (ok) {
                dlg.toast(message, false)      // 云端回的"已发布（送达 N / 在线 M）"
                form.reset()
                dlg.opened = false
                dlg.closed()
            } else {
                dlg.toast("发布失败：" + message, true)   // 内容保留，改完可重试
            }
        }
    }

    ParallelAnimation {
        id: openAnim
        NumberAnimation { target: scrim; property: "opacity"; from: 0; to: 1; duration: dlg.animDur; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "scale"; from: 0.85; to: 1.0; duration: dlg.animDur; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: dlg.animDur; easing.type: Easing.OutCubic }
    }

    // ── 遮罩（挡背后点击，MD3 模态）──
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }
    }

    // ── 卡面（显式高，不碰 FreshCard.implicitHeight —— 头文件明令会成环）──
    FreshCard {
        id: card
        anchors.centerIn: parent
        width: Math.min(dlg.width - 48, 480)
        height: col.implicitHeight + 32
        md3: dlg.md3

        Column {
            id: col
            anchors.fill: parent
            spacing: 12

            // 标题行
            Row {
                width: parent.width
                spacing: 8
                SvgIcon {
                    name: "monitor"
                    px: 16
                    tint: md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "发布中心"
                    font.pixelSize: md3.body13
                    font.weight: Font.Medium
                    color: md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Item { width: Math.max(8, parent.width - 180); height: 1 }
                Text {
                    text: dlg.subbed ? ("当前班：" + ((dlg.cs && dlg.cs.className) ? dlg.cs.className : dlg.code))
                                     : "未订阅班级"
                    font.pixelSize: md3.bodyS
                    color: dlg.subbed ? (md3.frInk2 || "#6B7280") : (md3.frDanger || "#EF4444")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            ClassPublishForm {
                id: form
                width: parent.width
                md3: dlg.md3
            }

            // 按钮行
            Row {
                width: parent.width
                spacing: 8
                FreshBtn {
                    md3: dlg.md3
                    text: dlg.sending ? "发布中…" : "发布到全班"
                    variant: "primary"
                    enabled: dlg.canSend
                    width: (parent.width - 8) / 2
                    onClicked: dlg.doPublish()
                }
                FreshBtn {
                    md3: dlg.md3
                    text: "取消"
                    variant: "outlined"
                    width: (parent.width - 8) / 2
                    onClicked: dlg.close()
                }
            }
        }
    }
}
