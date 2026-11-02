import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// ControlDialog（U3 从 Main.qml 抽出 · 管控（广播/考试/语音/屏幕广播））
// ───────────────────────────────────────────────────────────────────────────
// 契约（见 Stelarith-shared-qml/ui/UiContract.js）：
//   · Popup 自带 open()/close() ⇒ entry 契约天然满足
//   · `property var ctx` 收宿主注入的上下文：ctx.window（原 `root.`）、ctx.open/ctx.closePanels
//   · `property var theme` 给本文件自己的绘制用（token 对象）
//
// ⚠️ 内容与抽出前逐字相同，只做了四类机械替换（语义不变）：
//      `root.X` → `ctx.window.X`
//      裸 `th.foo`（属性读取）→ `theme.foo`
//      裸 `th`（作为取值，如 `Btn { theme: th }`）→ `ctx.window.th`
//        ⚠️ 不能用本地 `theme`：那会变成 `theme: theme` 自引用绑定
//      跨面板 `controlDlg.close()` → `ctx.closePanels()`
Popup {
    id: controlDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 560
    height: 560
    modal: true
    padding: 0
    background: Rectangle {
        color: theme.panel
        radius: theme.rCard
        border.color: theme.card
        border.width: 0
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentHeight: ctlCol.height
        clip: true
        Column {
            id: ctlCol
            width: parent.width
            spacing: 14

            // ── 广播（对所有在线设备）──
            Rectangle {
                width: parent.width
                height: bcastCol.height + 32
                radius: theme.rCard
                color: theme.panel
                border.color: theme.card
                border.width: 0
                Column {
                    id: bcastCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    spacing: 10
                    Text { text: "广播（发给所有在线设备）"; color: theme.fg3; font.pixelSize: 12 }
                    InputField {
                        th: ctx.window.th
                        id: bcastText
                        width: parent.width
                        placeholderText: "广播内容"
                    }
                    Btn {
                        theme: ctx.window.th
                        width: parent.width
                        text: "广播"
                        // 真正的事（权限、空内容、逐台下发）都交给 ctx.window.broadcastAll()：
                        // 命令面板上同一件事也得做一遍，抽出来才不会两边漂移
                        onClicked: ctx.window.broadcastAll(bcastText.text)
                    }
                }
            }

            // ── 考试模式（2026-10-06，设计文档 3.7 第一版）──
            // 2026-10-09 改**黑名单**：语义从"名单之外全杀"反转为"只杀名单里的"。
            //   起因是学校机房反复重启 —— 白名单制下没列全的系统进程会被杀，
            //   杀到 critical 的 svchost 就是 CRITICAL_PROCESS_DIED(0xEF) 蓝屏。
            //   黑名单的好处是"配错了最多没杀干净，绝不把机器弄崩"。
            // ⚠️ 别为了"更严格"把这里改回白名单，也别给默认黑名单塞进程名。
            Rectangle {
                width: parent.width
                height: examCol.height + 32
                radius: theme.rCard
                color: theme.panel
                border.color: theme.card
                border.width: 0
                Column {
                    id: examCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    spacing: 10
                    Text { text: "考试模式（全屏拦截 + 黑名单进程）"; color: theme.fg3; font.pixelSize: 12 }
                    Row {
                        spacing: 8
                        InputField {
                            th: ctx.window.th
                            id: examMinutes
                            width: 90
                            placeholderText: "时长(分钟)"
                            validator: IntValidator { bottom: 0; top: 300 }
                        }
                        InputField {
                            th: ctx.window.th
                            id: examBlacklist
                            width: 200
                            placeholderText: "黑名单(逗号分隔，如 chrome,steam)"
                        }
                    }
                    Row {
                        spacing: 8
                        Btn {
                            theme: ctx.window.th
                            width: 90
                            text: "开始考试"
                            onClicked: {
                                const uid = backend.currentUid
                                if (!uid) { ctx.window.toast("先在控制页选中一台设备"); return }
                                var blacklist = []
                                examBlacklist.text.split(",").forEach(function(s) {
                                    var t = s.trim()
                                    if (t) blacklist.push(t)
                                })
                                backend.sendAction("exam_mode", {
                                    "minutes": parseInt(examMinutes.text, 10) || 0,
                                    "blacklist": blacklist
                                })
                                ctx.window.toast("考试模式已下发 → " + uid)
                            }
                        }
                        Btn {
                            theme: ctx.window.th
                            width: 90
                            text: "结束考试"
                            onClicked: {
                                const uid = backend.currentUid
                                if (!uid) { ctx.window.toast("先在控制页选中一台设备"); return }
                                backend.sendAction("exam_mode_stop", {})
                                ctx.window.toast("已请求结束考试 → " + uid)
                            }
                        }
                    }
                }
            }

            // ── 语音对讲（2026-10-07 设计 3.3 第一版，2026-10-11 改对话框式）──
            // 老师→全班单向广播：开麦采集 PCM → 云端 fan-out → 学生端播放。
            // 按住讲话：按下开麦、松开停（push-to-talk）；回传的班级语音自动播放
            //（audio_playback，B6 已接——"自动接收播放班级语音"）。
            Rectangle {
                width: parent.width
                height: speechCol.height + 32
                radius: theme.rCard
                color: theme.panel
                border.color: theme.card
                border.width: 0
                Column {
                    id: speechCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    spacing: 10
                    Row {
                        width: parent.width
                        Text { text: "语音对讲"; color: theme.fg; font.pixelSize: 13; font.weight: Font.Medium }
                        Rectangle {
                            width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter
                            color: backend.speaking ? "#e74c3c" : theme.fg4; anchors.left: parent.right; anchors.leftMargin: 8
                            Timer {
                                running: backend.speaking; repeat: true; interval: 500
                                onTriggered: parent.opacity = parent.opacity === 1 ? 0.25 : 1
                            }
                        }
                    }
                    Text { text: "目标：" + (backend.currentUid !== "" ? backend.currentUid : "未选设备")
                           color: theme.fg3; font.pixelSize: 12 }
                    // 时长：已讲秒数 = 180（kDefaultSpeakMaxMs 3 分钟）− 剩余；末尾同显剩余。
                    Text { text: backend.speaking
                               ? ("时长：" + Math.max(0, 180 - backend.speakLeftSec) + " 秒（剩余 " + backend.speakLeftSec + " 秒）")
                               : (backend.speakError !== "" ? ("上次开麦失败：" + backend.speakError) : "时长：未通话")
                           color: theme.fg3; font.pixelSize: 12 }
                    Row {
                        spacing: 10
                        // 按住讲话（push-to-talk）：按下说话、松开停；自动接收播放班级语音已内置。
                        Rectangle {
                            width: 170; height: 46; radius: theme.rCtrl
                            color: pushArea.pressed ? "#a93226" : (backend.speaking ? "#e74c3c" : theme.brand)
                            Text {
                                anchors.centerIn: parent
                                text: pushArea.pressed ? "松开结束" : (backend.speaking ? "正在讲话…" : "按住讲话")
                                color: "white"; font.pixelSize: 14; font.weight: Font.Medium
                            }
                            MouseArea {
                                id: pushArea
                                anchors.fill: parent
                                onPressed: {
                                    if (!backend.startSpeaking())
                                        ctx.window.toast("开麦失败：" + backend.speakError)
                                    else
                                        ctx.window.toast("正在讲话…松开结束；班级语音自动播放")
                                    ctx.window.refreshIsland()
                                }
                                onReleased: {
                                    if (backend.speaking) { backend.stopSpeaking(); ctx.window.toast("语音已停止（静音）") }
                                    ctx.window.refreshIsland()
                                }
                            }
                        }
                        // 手动停止（保险：按住的话忘记松开也能停；也符合原来点按钮停的习惯）
                        Rectangle {
                            visible: backend.speaking
                            width: 96; height: 46; radius: theme.rCtrl
                            color: theme.inv
                            Text { anchors.centerIn: parent; text: "停止"; color: theme.win; font.pixelSize: 14; font.weight: Font.Medium }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { backend.stopSpeaking(); ctx.window.toast("语音已停止"); ctx.window.refreshIsland() }
                            }
                        }
                    }
                }
            }

            // ── 屏幕广播（2026-10-07，设计文档《屏幕广播-第一版设计》）──
            // 老师屏幕 → 全部在线设备全屏显示（3fps JPEG，60 台可承受）。
            Rectangle {
                width: parent.width
                height: bcastCol2.height + 32
                radius: theme.rCard
                color: theme.panel
                border.color: theme.card
                border.width: 0
                visible: !ctx.window.isPhone   // 屏幕广播 = 老师桌面推流，手机管理端不出现
                Column {
                    id: bcastCol2
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    spacing: 10
                    Text { text: "屏幕广播（老师屏幕 → 全部在线设备）"; color: theme.fg3; font.pixelSize: 12 }
                    Text { text: "到点自动停，按钮显示剩余秒"; color: theme.fg4; font.pixelSize: 12 }
                    Row {
                        spacing: 8
                        Rectangle {
                            width: backend.broadcasting ? 170 : 130
                            height: 34; radius: theme.rCtrl
                            color: backend.broadcasting ? "#b03a2e" : theme.inv
                            Text { anchors.centerIn: parent; color: "white"; font.pixelSize: 13; font.weight: Font.Medium
                                   text: backend.broadcasting ? ("停止广播（剩 " + backend.bcastLeftSec + " 秒）") : "开始广播" }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (backend.broadcasting) {
                                        backend.stopBroadcast()
                                        ctx.window.toast("屏幕广播已停止")
                                    } else {
                                        if (backend.startBroadcast()) {
                                            ctx.window.toast("正在屏幕广播；再次点击停止")
                                        } else {
                                            ctx.window.toast("广播启动失败")
                                        }
                                    }
                                    ctx.window.refreshIsland()   // 灵动岛广播胶囊跟手开/关
                                }
                            }
                        }
                    }
                }
            }

        }
    }
}