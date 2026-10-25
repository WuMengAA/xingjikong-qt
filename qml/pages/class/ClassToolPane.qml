// 班级工作台 · 右栏「课堂工具」（2026-10-25 · 教室小工具第一刀）
// 定位：老师上课时**对全班一键**的常用工具。落点全部是被控端已有指令
// （set_volume / study_mode_* / noise_monitor_*），管理端此前只有单机音量滑块
// （集控页 volumeDlg），班级场景缺"一键对全班"——这里补的就是这一块。
// 被控端零改动、零新协议。
//
// ⚠️ 三条设计口径（都是踩过或想明白的）：
// ① 音量给**档位**不给滑块：批量场景没法对 40 台逐台确认拖到哪，老师真正会用的是
//    「静音 / 标准 / 大声」三个数。单台微调仍去集控页的音量滑块（那条路没动）。
// ② 专注/噪音的参数一律不传：被控端已有调好的默认值（thresholdDb -45 /
//    streakNeed 3 / recordSec 30 / intervalMs 2000），管理端再暴露一份必然漂移。
// ③ 学生机**本地关了噪音监测**时，这两类指令会回 skipped（不是 failed）。
//    那是真实的本地状态，界面如实显示，不假装成功——所以这里不放"已开启"的常亮态。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshBtn
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane
    property var host: null
    property var cs: null
    property var devices: []
    property var toastFn: null

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""
    implicitHeight: bodyCol.implicitHeight + 32

    function toast(msg, isErr) { if (pane.toastFn) pane.toastFn(msg, isErr === true) }

    /** 本班在线设备（与 ClassActionPane.classOnlineUids 同口径：只认本班 + 在线）。
     *  两处各写一份是**有意的**：这是右栏面板的自洽实现，不跨面板借函数
     *  （借了就会在对方改筛选条件时静默变形）。 */
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

    /** 批量下发：本班在线设备塞进宿主 picked，再走唯一执行入口 runActionById。
     *  ⚠️ 与 ClassActionPane.runBulk 同一形状（host.picked = u →
     *  host.runActionById），不抽公共函数：两处的失败提示文案不同（那边是"无法批量
     *  下发"，这边要说清是哪个工具），且右栏面板各自保持可独立读懂。 */
    function runBulk(actionId) {
        if (!pane.host) return
        const u = pane.classOnlineUids()
        if (u.length === 0) { pane.toast("当前班没有在线设备，无法下发课堂工具", true); return }
        pane.host.picked = u
        pane.host.runActionById(actionId)
    }

    FreshCard {
        anchors.fill: parent

        Column {
            id: bodyCol
            width: parent.width
            spacing: 10

            Row {
                width: parent.width
                spacing: 6
                Text {
                    text: "课堂工具"
                    font.pixelSize: Md3.body13
                    font.weight: Font.Medium
                    color: Md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "对全班在线设备生效"
                    font.pixelSize: Md3.rXS || 11
                    color: Md3.frOffDot || "#9CA3AF"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 音量：三档。静音是danger（打断课堂秩序时的紧急动作），两档常规 ──
            Row {
                width: parent.width
                spacing: 8
                FreshBtn {
                    text: "静音"
                    icon: "ban"
                    variant: "danger"
                    enabled: pane.subbed
                    width: (parent.width - 16) / 3
                    onClicked: pane.runBulk("pick-volume-mute")
                }
                FreshBtn {
                    text: "30%"
                    variant: "tonal"
                    enabled: pane.subbed
                    width: (parent.width - 16) / 3
                    onClicked: pane.runBulk("pick-volume-low")
                }
                FreshBtn {
                    text: "80%"
                    variant: "tonal"
                    enabled: pane.subbed
                    width: (parent.width - 16) / 3
                    onClicked: pane.runBulk("pick-volume-high")
                }
            }

            // ── 专注模式（自习纪律）：开/关成对，不假装知道当前状态 ──
            Row {
                width: parent.width
                spacing: 8
                FreshBtn {
                    text: "开启专注"
                    icon: "clock"
                    variant: "tonal"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
                    onClicked: pane.runBulk("pick-study-on")
                }
                FreshBtn {
                    text: "结束专注"
                    variant: "outlined"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
                    onClicked: pane.runBulk("pick-study-off")
                }
            }

            // ── 噪音监测（超阈值留证）：同样开/关成对 ──
            Row {
                width: parent.width
                spacing: 8
                FreshBtn {
                    text: "开启监测"
                    variant: "tonal"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
                    onClicked: pane.runBulk("pick-noise-on")
                }
                FreshBtn {
                    text: "结束监测"
                    variant: "outlined"
                    enabled: pane.subbed
                    width: (parent.width - 8) / 2
                    onClicked: pane.runBulk("pick-noise-off")
                }
            }

            Text {
                width: parent.width
                text: "学生在设备本地关闭噪音监测后，相关指令会显示未执行"
                font.pixelSize: Md3.rXS || 11
                color: Md3.frOffDot || "#9CA3AF"
                wrapMode: Text.WordWrap
            }
        }
    }
}
