// 班级工作台 · 新建班级弹窗（2026-10-25 用户点单「班级创建繁琐，没有引导」）
//
// 为什么做成页级弹窗：原来「新建班级」只藏在右栏「班级管理」里 —— 未选班时
// 右栏还要先滚过别的面板才看得到，左栏空态更是只写了句「登录后由云端下发」，
// 新手根本不知道要自己建班。现在左栏空态的「＋ 新建班级」和右栏「上手三步」
// 都能一键直接开这个弹窗：输入名字 → 创建 → 立即重拉名单。
//
// 复用：输入行沿用 ClassNameInputRow（与右栏新建/改名同一件，不另写一套）；
// 开合/遮罩/动画照抄 ClassPublishDialog（50% scrim + 居中卡 + 缩放淡入）。
// 铁律：失败必 toast 且保留输入（改完能重试），成功才关闭；建班不绑设备，
//       所以**不需要先订阅**（云端 createClass 只收班名）。
import QtQuick
import "../../components/fresh"     // FreshCard
import "../../island"               // SvgIcon
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: dlg

    property var toastFn: null        // (msg, isErr) 外壳页级 toast
    property bool opened: false
    /** 已发创建请求、等回执（防连点）。 */
    property bool sending: false

    signal closed()

    /** 动效时长：令牌优先，({}) 兜底 200（与 ClassPublishDialog 同口径）。 */
    readonly property int animDur: (Theme.motion && Theme.motion.fast !== undefined) ? Theme.motion.fast : 200

    visible: opened
    anchors.fill: parent
    onOpenedChanged: if (dlg.opened) openAnim.restart()

    function toast(msg, isErr) { if (dlg.toastFn) dlg.toastFn(msg, isErr === true) }

    /** 班级管理调用层（backend.classAdmin，C++ ClassAdmin）。 */
    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    function open() { dlg.sending = false; nameRow.open(""); dlg.opened = true }
    function close() { dlg.opened = false; dlg.closed() }

    function doCreate() {
        const a = dlg.admin()
        const t = nameRow.text.trim()
        if (!a || dlg.sending || t === "") return
        dlg.sending = true
        a.createClass(t)
    }

    // 创建回执：只认 op="create"（其余写操作归 ClassAdminPane，两处不混）。
    Connections {
        target: dlg.admin()
        ignoreUnknownSignals: true
        function onOpFinished(op, ok, message) {
            if (op !== "create") return
            dlg.sending = false
            if (ok) {
                dlg.toast("已创建班级「" + message + "」", false)
                if (typeof backend !== "undefined" && backend && backend.requestClasses)
                    backend.requestClasses()
                dlg.opened = false
                dlg.closed()
            } else {
                dlg.toast("新建班级失败：" + message, true)   // 输入保留，改完可重试
            }
        }
    }

    ParallelAnimation {
        id: openAnim
        NumberAnimation { target: scrim; property: "opacity"; from: 0; to: 1; duration: dlg.animDur; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "scale"; from: 0.85; to: 1.0; duration: dlg.animDur; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: dlg.animDur; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }
    }

    // ── 卡面（显式高，不绑 FreshCard.implicitHeight —— 会成环）──
    FreshCard {
        id: card
        anchors.centerIn: parent
        width: Math.min(dlg.width - 48, 440)
        height: col.implicitHeight + 32

        Column {
            id: col
            anchors.fill: parent
            spacing: 12

            Row {
                width: parent.width
                spacing: 8

                SvgIcon {
                    name: "plus"
                    px: 16
                    tint: Theme.color.textSecondary || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "新建班级"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 引导：说清"建班只是第一步，设备要靠接入码进来"
            Text {
                width: parent.width
                text: "给班级起个名字即可。建好后在右栏「班级管理」点「生成班级接入码」，"
                      + "把这串 6 位码给设备 —— 设备首启时输入即自动入班。"
                font.pixelSize: 12
                color: Theme.color.textSecondary || "#6B7280"
                wrapMode: Text.WordWrap
                lineHeight: 1.4
            }

            ClassNameInputRow {
                id: nameRow
                width: parent.width
                placeholder: "班级名称，如：高二(3)班"
                confirmText: dlg.sending ? "创建中…" : "创建"
                confirmEnabled: !dlg.sending && nameRow.text.trim() !== ""
                onConfirmed: function (t) { dlg.doCreate() }
                onCancelled: dlg.close()
            }
        }
    }
}
