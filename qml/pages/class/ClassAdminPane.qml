// 班级工作台 · 右栏「班级管理」（方案 B，清新极简）：新建 / 改名 / 删班。
// 成员管理在中栏「成员」tab（ClassMemberPane）；右栏管班、中栏管设备，两处不混。
// 铁律：操作对象=当前班（未订阅整组禁用）；写成功 requestClasses() 重拉；失败必 toast（fail-silent 红线）；删除行内两段确认；输入行抽件 ClassNameInputRow。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshBtn
import "../../island"              // SvgIcon
Item {
    id: pane

    property var md3: ({})
    property var cs: null        // ClassService（订阅态 + 班码/班名）
    property var toastFn: null   // (msg, isErr) 外壳页级 toast

    /** 新建/改名的行内输入态："" = 收起；"create" / "rename" = 对应输入行展开中。 */
    property string inputMode: ""
    /** 删除两段确认：false → 只显示按钮；true → 行内变红字确认条。 */
    property bool delArmed: false

    // ── 班级接入码（2026-10-25 · 收归我方：设备凭它进入当前班）────────────
    /** 最近生成的那枚 6 位码（空串 = 还没生成 / 已作废 → 展示区隐藏）。 */
    property string bindCode: ""
    /** 请求在飞：按钮置灰防连点 —— 云端每次都发一枚新票，连点纯属白烧码表。 */
    property bool issuing: false

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""
    readonly property string name: (cs && cs.className) ? cs.className : ""

    /** 切班就丢掉上一枚码：**绝不把 A 班的码留在 B 班的界面上**，
     *  那种错会让人拿错码去绑设备，排查时最难看出（码本身是合法的）。 */
    onCodeChanged: {
        pane.bindCode = ""
        pane.issuing = false
    }

    implicitWidth: 320
    implicitHeight: bodyCol.implicitHeight + 32
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
        /** 接入码生成成功：结构化结果走这条（码 + 有效期），
         *  opFinished 那条只负责把 issuing 收尾 / 报失败，两处分工不重复弹 toast。 */
        function onBindCodeIssued(classCode, className, code, expiresAt, ttlSec) {
            pane.issuing = false
            pane.bindCode = code
            pane.toast("接入码已生成：" + code, false)
        }
        function onOpFinished(op, ok, message) {
            if (op === "bindcode") {
                // 成功已由 onBindCodeIssued 处理（带结果）；这里只管失败收尾。
                pane.issuing = false
                if (!ok) pane.toast("生成接入码失败：" + message, true)
                return
            }
            if (op === "bindcode-revoke") {
                if (ok) { pane.bindCode = ""; pane.toast("接入码已作废，设备再也用不了这串", false) }
                else pane.toast("作废失败：" + message, true)
                return
            }
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
            } else if (op === "member-add" || op === "member-remove") {
                // 人（账号）成员：成功 → toast + 重拉成员清单；失败 → toast 云端人话。
                if (ok) {
                    pane.toast(op === "member-add" ? "已添加成员（下次登录生效）" : "成员已移出班级", false)
                    const a = pane.admin()
                    const cc = (pane.cs && pane.cs.classCode) ? pane.cs.classCode : ""
                    if (a && cc !== "") a.members(cc)
                } else {
                    pane.toast((op === "member-add" ? "添加成员失败：" : "移出成员失败：") + message, true)
                }
            }
        }
    }
    FreshCard {
        anchors.fill: parent
        md3: pane.md3

        Column {
            id: bodyCol
            width: parent.width
            spacing: 12
            // ── 标题行 ──
            Row {
                width: parent.width
                spacing: 8
                SvgIcon {
                    // 2026-10-25：原 "settings-2" 在图标集里不存在（image://svgicon 取图
                    // 失败 ⇒ 空白，离屏冒烟实证），改回实存的 "settings"。
                    name: "settings"
                    px: 16
                    tint: md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "班级管理"
                    font.pixelSize: md3.body13
                    font.weight: Font.Medium
                    color: md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Item { width: Math.max(8, parent.width - 140); height: 1 }
                Text {
                    visible: !pane.subbed
                    text: "未订阅"
                    font.pixelSize: md3.bodyS
                    color: md3.frOffDot || "#9CA3AF"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 当前班（只读展示：名字 + 等宽码）──
            Text {
                width: parent.width
                visible: pane.subbed
                text: "当前班：" + (pane.name !== "" ? pane.name : pane.code)
                font.pixelSize: md3.bodyS
                color: md3.frInk2 || "#6B7280"
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !pane.subbed
                text: "先选择左侧班级，才能执行管理操作"
                font.pixelSize: md3.bodyS
                color: md3.frOffDot || "#9CA3AF"
                wrapMode: Text.WordWrap
            }

            // ── 新建班级（不需要先订阅：建班本就不绑设备）──
            FreshBtn {
                md3: pane.md3
                text: pane.inputMode === "create" ? "收起" : "新建班级"
                variant: "tonal"
                width: parent.width
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

                FreshBtn {
                    md3: pane.md3
                    text: pane.inputMode === "rename" ? "收起" : "修改班名"
                    variant: "outlined"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
                    onClicked: {
                        pane.delArmed = false
                        if (pane.inputMode === "rename") pane.inputMode = ""
                        else { pane.inputMode = "rename"; renameRow.open(pane.name) }
                    }
                }
                FreshBtn {
                    md3: pane.md3
                    text: pane.delArmed ? "取消删除" : "删除班级"
                    variant: pane.delArmed ? "danger" : "danger-ghost"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
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

            // ── 班级接入码（2026-10-25 · 收归我方：设备凭它进入这个班）──────
            // 这是**设备入班的唯一入口**（取代原来站点后台发的 XJK 码）：
            // 老师在这里点一下拿到 6 位码，到设备 OOBE 的「输入绑定码」敲进去即入班。
            // 一次性 + 24 小时有效；发错班了点「作废」立刻失效。
            Rectangle {
                width: parent.width
                height: 1
                visible: pane.subbed
                color: md3.frOffDot || "#9CA3AF"
                opacity: 0.3
            }
            FreshBtn {
                md3: pane.md3
                text: pane.issuing ? "生成中…" : "生成班级接入码"
                variant: "tonal"
                width: parent.width
                enabled: pane.subbed && !pane.issuing
                onClicked: {
                    pane.bindCode = ""
                    pane.issuing = true
                    const a = pane.admin()
                    if (a) a.issueBindCode(pane.code)
                    else pane.issuing = false
                }
            }
            Column {
                width: parent.width
                visible: pane.bindCode !== ""
                spacing: 6
                Text {
                    width: parent.width
                    text: "把这串给这台设备（一次性，24 小时内有效）："
                    font.pixelSize: md3.bodyS
                    color: md3.frInk2 || "#6B7280"
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    width: parent.width
                    height: 44
                    radius: 8
                    color: md3.frOffDot || "#9CA3AF"
                    opacity: 0.14
                }
                // 码叠在上面单独一层：父 Rectangle 用低透明度做底，字才不会被一起调淡。
                TextEdit {
                    id: codeText
                    width: parent.width
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: -44 - 6 + 12          // 压回上面那个底框内（Column 里没有叠加布局）
                    readOnly: true
                    selectByMouse: true
                    text: pane.bindCode
                    font.pixelSize: md3.bodyL || 16
                    font.family: "Consolas"
                    font.letterSpacing: 4
                    color: md3.frInk || "#111827"
                    horizontalAlignment: TextEdit.AlignHCenter
                }
                Row {
                    width: parent.width
                    spacing: 8
                    FreshBtn {
                        md3: pane.md3
                        text: "复制"
                        variant: "outlined"
                        width: (parent.width - 8) / 2
                        onClicked: {
                            codeText.selectAll()
                            codeText.copy()
                            pane.toast("接入码已复制", false)
                        }
                    }
                    FreshBtn {
                        md3: pane.md3
                        text: "作废"
                        variant: "danger-ghost"
                        width: (parent.width - 8) / 2
                        onClicked: {
                            const a = pane.admin()
                            if (a) a.revokeBindCode(pane.bindCode)
                        }
                    }
                }
            }
        }
    }
}
