// 班级工作台 · 右栏「班级管理」面板（2026-10-24 · 方案 B）
// 对「班」的写操作：新建 / 改名 / 删班。成员管理（移出/换班）在中栏「成员」
// tab（ClassMemberPane）——右栏管班，中栏管设备，两处不混。
// 铁律：
//   · 操作对象 = 当前订阅的班（cs.classCode）；未订阅整组禁用（删错班不可逆）；
//   · 不持有班级状态：真源是 backend.classes，写成功立即 requestClasses() 重拉；
//   · 失败必须 toast（云端 error 是人话）——不许静默（fail-silent 红线）；
//   · 删除用行内两段确认（不借 Md3Dialog：scrim 的 anchors.fill 会把对话框锁在
//     本卡片内且面板自己会塌高度——踩过）；输入行抽件 ClassNameInputRow，
//     打开时 open(t) 显式预填（用户键入会摧毁绑定，见那件头注）。
import QtQuick
import "../../components/md3"     // Md3Card / Md3Btn
import "../../island"             // SvgIcon

Item {
    id: pane

    property var md3: ({})
    property var cs: null        // ClassService（订阅态 + 班码/班名）
    property var toastFn: null   // (msg, isErr) 外壳页级 toast

    /** 新建/改名的行内输入态："" = 收起；"create" / "rename" = 对应输入行展开中。 */
    property string inputMode: ""
    /** 删除两段确认：false → 只显示按钮；true → 行内变红字确认条。 */
    property bool delArmed: false

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""
    readonly property string name: (cs && cs.className) ? cs.className : ""

    implicitWidth: 320
    implicitHeight: bodyCol.implicitHeight + 24

    function toast(msg, isErr) { if (pane.toastFn) pane.toastFn(msg, isErr === true) }

    /** 班级管理调用层（backend.classAdmin，C++ ClassAdmin）。 */
    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }
    /** 写成功 → 立即重拉班级名单（左栏/中栏换班目标都是它）。 */
    function refreshClasses() {
        if (typeof backend !== "undefined" && backend && backend.requestClasses)
            backend.requestClasses()
    }

    /** 五个写操作的统一回调：ok → toast + 重拉；失败 → toast 云端人话。
     *  bind/unbind 由中栏成员面板发起，反馈汇到这里（两处共用一个调用层）。 */
    Connections {
        target: pane.admin()
        ignoreUnknownSignals: true
        function onOpFinished(op, ok, message) {
            if (op === "create") {
                if (ok) { pane.inputMode = ""; pane.toast("已创建班级「" + message + "」", false) }
                else pane.toast("新建班级失败：" + message, true)
                pane.refreshClasses()
            } else if (op === "rename") {
                if (ok) pane.toast("班名已改为「" + message + "」", false)
                else pane.toast("改名失败：" + message, true)
                pane.refreshClasses()
            } else if (op === "delete") {
                pane.delArmed = false
                if (ok) pane.toast("班级已删除（设备已全部移出）", false)
                else pane.toast("删除失败：" + message, true)
                pane.refreshClasses()
            } else if (op === "bind" || op === "unbind") {
                if (ok) pane.toast(message === "" ? "设备已移出班级" : "设备已加入「" + message + "」", false)
                else pane.toast("调整成员失败：" + message, true)
                pane.refreshClasses()
            }
        }
    }

    Md3Card {
        anchors.fill: parent
        variant: "elevated"
        md3: pane.md3

        Column {
            id: bodyCol
            width: parent.width
            spacing: 10

            // ── 标题行 ──
            Row {
                width: parent.width
                spacing: 8

                SvgIcon {
                    name: "settings-2"
                    px: 16
                    tint: md3.cOnSurfaceVar
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "班级管理"
                    font.pixelSize: md3.body13
                    font.weight: Font.Medium
                    color: md3.cOnSurfaceVar
                    anchors.verticalCenter: parent.verticalCenter
                }
                Item { width: Math.max(8, parent.width - 140); height: 1 }
                Text {
                    visible: !pane.subbed
                    text: "未订阅"
                    font.pixelSize: md3.bodyS
                    color: md3.cOutline
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 当前班（只读展示：名字 + 等宽码）──
            Text {
                width: parent.width
                visible: pane.subbed
                text: "当前班：" + (pane.name !== "" ? pane.name : pane.code)
                font.pixelSize: md3.bodyS
                color: md3.cOnSurfaceVar
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !pane.subbed
                text: "先在左侧选择一个班级，才能对它执行管理操作。"
                font.pixelSize: md3.bodyS
                color: md3.cOutline
                wrapMode: Text.WordWrap
            }

            // ── 新建班级（不需要先订阅：建班本就不绑设备）──
            Md3Btn {
                md3: pane.md3
                text: pane.inputMode === "create" ? "收起" : "新建班级"
                variant: "tonal"
                onClicked: {
                    pane.delArmed = false
                    if (pane.inputMode === "create") pane.inputMode = ""
                    else { pane.inputMode = "create"; createRow.open("") }
                }
            }
            ClassNameInputRow {
                id: createRow
                width: parent.width
                visible: pane.inputMode === "create"
                md3: pane.md3
                placeholder: "班级名称，如：高二(3)班"
                confirmText: "创建"
                confirmEnabled: createRow.text.trim() !== ""
                onConfirmed: (t) => { const a = pane.admin(); if (a) a.createClass(t) }
                onCancelled: pane.inputMode = ""
            }

            // ── 改名 / 删除（都要先订阅：操作对象是当前班）──
            Row {
                width: parent.width
                spacing: 8

                Md3Btn {
                    md3: pane.md3
                    text: pane.inputMode === "rename" ? "收起" : "修改班名"
                    variant: "outlined"
                    enabled: pane.subbed
                    onClicked: {
                        pane.delArmed = false
                        if (pane.inputMode === "rename") pane.inputMode = ""
                        else { pane.inputMode = "rename"; renameRow.open(pane.name) }
                    }
                }
                Md3Btn {
                    md3: pane.md3
                    text: pane.delArmed ? "取消删除" : "删除班级"
                    variant: "outlined"
                    enabled: pane.subbed
                    onClicked: pane.delArmed = !pane.delArmed
                }
            }
            ClassNameInputRow {
                id: renameRow
                width: parent.width
                visible: pane.inputMode === "rename"
                md3: pane.md3
                hint: "只改显示名，班级代码不变（已绑定设备的归属不受影响）。"
                placeholder: "新的班级名称"
                confirmText: "保存"
                confirmEnabled: renameRow.text.trim() !== "" && renameRow.text.trim() !== pane.name
                onConfirmed: (t) => { const a = pane.admin(); if (a) a.renameClass(pane.code, t) }
                onCancelled: pane.inputMode = ""
            }
            ClassDeleteConfirm {
                width: parent.width
                visible: pane.delArmed
                md3: pane.md3
                targetName: pane.name !== "" ? pane.name : pane.code
                onConfirmed: { const a = pane.admin(); if (a) a.deleteClass(pane.code, "release") }
                onCancelled: pane.delArmed = false
            }
        }
    }
}
