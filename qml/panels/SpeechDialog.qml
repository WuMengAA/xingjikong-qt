import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// SpeechDialog（U3 从 Main.qml 抽出 · 语音对讲）
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
    id: speechDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 360
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.cream
        radius: theme.rCard
        border.color: theme.stroke
        border.width: 1
    }

    contentItem: Column {
        padding: 16
        spacing: 12

        Row {
            spacing: 8
            Text { text: "语音对讲"; color: theme.fg; font.pixelSize: 14; font.weight: Font.Medium }
            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: backend.speaking ? "#e74c3c" : theme.fg4
                opacity: backend.speaking ? (speechPulse.running ? 1 : 0.25) : 1
                Timer { id: speechPulse; running: backend.speaking; repeat: true; interval: 500
                        onTriggered: parent.opacity = parent.opacity === 1 ? 0.25 : 1 }
            }
        }
        Text { text: "目标：" + (backend.currentUid !== "" ? backend.currentUid : "未选设备")
               color: theme.fg3; font.pixelSize: 12 }
        // 2026-10-11：Android 录音权限状态 —— 弹框没出现也要看得见「未授权」，
        // 并给一个「去设置」手动开关的入口（Android 13+ 二次请求不再弹框，
        // 只能从系统设置里开；代码里 startSpeaking 会自动请求，但拒绝过就得靠这里）
        Rectangle {
            visible: ctx.window.isPhone && !backend.audioPermGranted
            width: parent.width; height: 34; radius: theme.rCtrl
            color: "#b03a2e"; opacity: 0.92
            Row {
                anchors.left: parent.left; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; text: "⚠ 麦克风权限未开启——按住讲话会弹系统授权框；若已拒绝请点这里去系统设置开启"
                       color: "white"; font.pixelSize: 11; width: parent.width - 92; wrapMode: Text.Wrap }
                Rectangle {
                    width: 76; height: 26; radius: 6; color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                    Text { anchors.centerIn: parent; text: "去设置"; color: "#b03a2e"; font.pixelSize: 12; font.weight: Font.Medium }
                    MouseArea { anchors.fill: parent; onClicked: backend.openAudioSettings() }
                }
            }
        }
        Text { text: backend.speaking
                   ? ("时长：" + Math.max(0, 180 - backend.speakLeftSec) + " 秒（剩余 " + backend.speakLeftSec + " 秒）")
                   : (backend.speakError !== "" ? ("上次开麦失败：" + backend.speakError) : "时长：未通话")
               color: theme.fg3; font.pixelSize: 12 }
        // 自动接收播放班级语音：audio_playback 已内置（B6），无需再开。

        Row {
            spacing: 10
            Rectangle {
                width: 200; height: 48; radius: theme.rCtrl
                color: speechPush.pressed ? "#a93226" : (backend.speaking ? "#e74c3c" : theme.brand)
                Text {
                    anchors.centerIn: parent
                    text: speechPush.pressed ? "松开结束" : (backend.speaking ? "正在讲话…" : "按住讲话")
                    color: "white"; font.pixelSize: 14; font.weight: Font.Medium
                }
                MouseArea {
                    id: speechPush
                    anchors.fill: parent
                    onPressed: {
                        if (!backend.startSpeaking()) ctx.window.toast("开麦失败：" + backend.speakError)
                        else ctx.window.toast("正在讲话…松开结束；班级语音自动播放")
                        ctx.window.refreshIsland()
                    }
                    onReleased: {
                        if (backend.speaking) { backend.stopSpeaking(); ctx.window.toast("语音已停止（静音）") }
                        ctx.window.refreshIsland()
                    }
                }
            }
            Rectangle {
                visible: backend.speaking
                width: 96; height: 48; radius: theme.rCtrl
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