// 班级工作台 · 右栏上「动作面板」（#461-#472，2026-10-24 清新极简改版）
// 全班（本班在线塞 picked 走批量 id）/ 单台（宿主已 pickDevice 走 runActionById）。
// 语音对讲走 host.startSpeaking（全局广播，班级语音待建设，按钮预留）。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshBtn
import "../../island"              // SvgIcon

Item {
    id: pane
    property var md3: ({})
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
        md3: pane.md3

        Column {
            id: bodyCol
            width: parent.width
            spacing: 14

            FreshBtn {
                md3: pane.md3
                text: "按住说话"
                icon: "mic"
                variant: "primary"
                width: parent.width
                onClicked: {
                    if (pane.host && pane.host.startSpeaking) pane.host.startSpeaking()
                    else pane.toast("语音对讲请到集控页", false)
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Text {
                    text: "全班"
                    font.pixelSize: md3.body13
                    font.weight: Font.Medium
                    color: md3.frInk2 || "#6B7280"
                }
                Row {
                    width: parent.width
                    spacing: 8
                    FreshBtn {
                        md3: pane.md3
                        text: "锁定桌面"
                        variant: "danger"
                        enabled: pane.subbed
                        width: (parent.width - 8) / 2
                        onClicked: pane.runBulk("pick-lock")
                    }
                    FreshBtn {
                        md3: pane.md3
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
                    font.pixelSize: md3.bodyS
                    color: md3.frOffDot || "#9CA3AF"
                }
                // 发布中心：向全班教室黑板推卡片（与锁屏/关机同属「对全班」动作，
                // 只是它推的是内容而不是指令）。不需要选中设备 —— 对象是整班。
                FreshBtn {
                    md3: pane.md3
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
                    font.pixelSize: md3.body13
                    font.weight: Font.Medium
                    color: md3.frInk2 || "#6B7280"
                }
                Text {
                    width: parent.width
                    visible: pane.selectedUid === ""
                    text: "点选一台设备以操作"
                    font.pixelSize: md3.bodyS
                    color: md3.frOffDot || "#9CA3AF"
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
                            tint: md3.frGreen || "#22C55E"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "已选中：" + (pane.selectedName !== "" ? pane.selectedName : pane.selectedUid)
                            font.pixelSize: md3.body13
                            color: md3.frInk || "#111827"
                            elide: Text.ElideRight
                            width: parent.width - 24
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    FreshBtn {
                        md3: pane.md3
                        text: "屏幕截图"
                        variant: "tonal"
                        width: parent.width
                        onClicked: if (pane.host) pane.host.runActionById("screenshot")
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        FreshBtn {
                            md3: pane.md3
                            text: "锁定桌面"
                            variant: "danger-ghost"
                            width: (parent.width - 8) / 2
                            onClicked: if (pane.host) pane.host.runActionById("lock")
                        }
                        FreshBtn {
                            md3: pane.md3
                            text: "远程重启"
                            variant: "danger-ghost"
                            width: (parent.width - 8) / 2
                            onClicked: if (pane.host) pane.host.runActionById("reboot")
                        }
                    }
                    FreshBtn {
                        md3: pane.md3
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
