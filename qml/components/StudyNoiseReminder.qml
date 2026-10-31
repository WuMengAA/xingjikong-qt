// 自习页 · 语音提醒按钮（2026-10-31 · 管理端联动）
// 一键向当前选中设备下发噪音提醒通知（含 TTS 朗读）。
// 契约与 NotifyDialog 完全同源：NotifyParams.buildNotifyParams + backend.sendAction("notify")
// （viewer → 云端 /api/instructions → 被控端 notifyFromParams → 岛通知 + speakText）。
import QtQuick
import QtQuick.Layouts

import "../components"                  // Btn
import "../NotifyParams.js" as NotifyParams   // 通知参数契约唯一真源

Item {
    id: reminder
    property var cc                       // CameraController（tSpS/tFg4/tLabelM 等下略）
    readonly property bool can: backend && !!backend.currentUid

    width: parent ? parent.width : 200
    implicitHeight: 40

    function announceQuiet() {
        if (!reminder.can) return
        const q = NotifyParams.buildNotifyParams({
            kind: "tips",
            title: "语音提醒",
            content: "同学们请注意，教室噪音超标，请保持安静",
            speech: true,
            seconds: 0
        })
        if (q.ok) backend.sendAction("notify", q.params)
    }

    Row {
        anchors.fill: parent
        spacing: reminder.cc ? reminder.cc.tSpS : 8
        Btn {
            width: 128
            height: 30
            text: "语音提醒安静"
            tip: "向当前选中设备播报「教室噪音超标，请保持安静」（通知 + 语音朗读）"
            disabled: !reminder.can
            onClicked: reminder.announceQuiet()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: reminder.can ? ("将发给 " + backend.currentUid) : "先选一台设备"
            color: reminder.cc ? reminder.cc.tFg4 : "#888888"
            font.pixelSize: reminder.cc ? reminder.cc.tLabelM : 11
        }
    }
}