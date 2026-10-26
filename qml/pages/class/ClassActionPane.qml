// 班级工作台 · 右栏上「动作面板」（#461-#472，2026-10-24 清新极简改版）
// 全班（本班在线塞 picked 走批量 id）/ 单台（宿主已 pickDevice 走 runActionById）。
// 语音对讲 = ClassTalkButton（真开麦，按住说话 / 松开结束；2026-10-26 修掉假按钮）。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshBtn
import "../../island"              // SvgIcon
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane
    property var host: null
    property var cs: null
    property var devices: []
    property string selectedUid: ""
    property string selectedName: ""
    property var toastFn: null
    property var openControlFn: null

    /** 发布中心（P1）：请求打开发布弹窗。弹窗本体在页级（ClassDashboard）——
     *  模态要盖整页，塞进右栏 320 宽会被裁（竖屏那只还在 Flickable 里）。 */
    signal publishRequested()

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""
    implicitHeight: bodyCol.implicitHeight + 32

    function toast(msg, isErr) { if (pane.toastFn) pane.toastFn(msg, isErr === true) }
    function classOnlineUids() {
        const cc = pane.code
        if (cc === "") return []
        const ds = (pane.devices && pane.devices.length) ? pane.devices : []
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            const d = ds[i]
            if (!d) continue
            if ((d.classCode || "") !== cc) continue
            if (d.online !== false) out.push(d.uid)
        }
        return out
    }
    function runBulk(actionId) {
        if (!pane.host) return
        const u = pane.classOnlineUids()
        if (u.length === 0) { pane.toast("当前班没有在线设备，无法批量下发", true); return }
        pane.host.picked = u
        pane.host.runActionById(actionId)
    }

    FreshCard {
        anchors.fill: parent

        Column {
            id: bodyCol
            width: parent.width
            spacing: 14

            // 语音对讲：真·按住说话（2026-10-26 换掉假按钮）
            // 旧写法 `pane.host.startSpeaking` 在外壳上不存在 ⇒ 点了只弹"请到集控页"。
            // 现在直连 backend.startSpeaking/stopSpeaking，按下开麦、松开停。
            ClassTalkButton {
                width: parent.width
                toastFn: pane.toastFn
            }

            Column {
                width: parent.width
                spacing: 8

                Text {
                    text: "全班"
                    font.pixelSize: Md3.body13
                    font.weight: Font.Medium
                    color: Md3.frInk2 || "#6B7280"
                }
                Row {
                    width: parent.width
                    spacing: 8
                    FreshBtn {
                        text: "锁定桌面"
                        variant: "danger"
                        enabled: pane.subbed
                        width: (parent.width - 8) / 2
                        onClicked: pane.runBulk("pick-lock")
                    }
                    FreshBtn {
                        text: "远程关机"
                        variant: "danger"
                        enabled: pane.subbed
                        width: (parent.width - 8) / 2
                        onClicked: pane.runBulk("pick-shutdown")
                    }
                }
                Text {
                    width: parent.width
                    text: "本班 " + pane.classOnlineUids().length + " 台在线"
                    font.pixelSize: Md3.bodyS
                    color: Md3.frOffDot || "#9CA3AF"
                }
                // 发布中心：向全班教室黑板推卡片（与锁屏/关机同属「对全班」动作，
                // 只是它推的是内容而不是指令）。不需要选中设备 —— 对象是整班。
                FreshBtn {
                    text: "发布中心"
                    icon: "monitor"
                    variant: "tonal"
                    enabled: pane.subbed
                    width: parent.width
                    onClicked: pane.publishRequested()
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Text {
                    text: "单台"
                    font.pixelSize: Md3.body13
                    font.weight: Font.Medium
                    color: Md3.frInk2 || "#6B7280"
                }
                Text {
                    width: parent.width
                    visible: pane.selectedUid === ""
                    text: "点选一台设备以操作"
                    font.pixelSize: Md3.bodyS
                    color: Md3.frOffDot || "#9CA3AF"
                }
                Column {
                    width: parent.width
                    spacing: 8
                    visible: pane.selectedUid !== ""

                    Row {
                        width: parent.width
                        spacing: 8
                        SvgIcon {
                            name: "circle-check"
                            px: 16
                            tint: Md3.frGreen || "#22C55E"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "已选中：" + (pane.selectedName !== "" ? pane.selectedName : pane.selectedUid)
                            font.pixelSize: Md3.body13
                            color: Md3.frInk || "#111827"
                            elide: Text.ElideRight
                            width: parent.width - 24
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    FreshBtn {
                        text: "屏幕截图"
                        variant: "tonal"
                        width: parent.width
                        onClicked: if (pane.host) pane.host.runActionById("screenshot")
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        FreshBtn {
                            text: "锁定桌面"
                            variant: "danger-ghost"
                            width: (parent.width - 8) / 2
                            onClicked: if (pane.host) pane.host.runActionById("lock")
                        }
                        FreshBtn {
                            text: "远程重启"
                            variant: "danger-ghost"
                            width: (parent.width - 8) / 2
                            onClicked: if (pane.host) pane.host.runActionById("reboot")
                        }
                    }
                    FreshBtn {
                        text: "在集控页打开"
                        variant: "outlined"
                        width: parent.width
                        onClicked: {
                            if (pane.openControlFn) pane.openControlFn()
                            else if (pane.host) pane.host.goPage(1)
                        }
                    }
                }
            }
        }
    }
}
