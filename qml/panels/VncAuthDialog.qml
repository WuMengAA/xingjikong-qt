// 被控端访问密码弹窗（2026-10-26）
//
// 为什么有它：接管一台「启用了访问密码」的设备前，云端会在 remote_control_start /
// vnc-data 上行两处回 VNC_AUTH_REQUIRED 拒绝。backend 因此发 vncAuthRequired(uid)
// 信号 → Main.qml 用 backend.devices 解析出设备名后调用本弹窗 showFor(uid, name)。
//
// 用户填密码 → backend.submitVncPassword(uid, pw)。云端回：
//   · vnc-auth-ok  → backend 发 vncAuthOk(uid) → 本弹窗关闭，接管继续；
//   · vnc-auth-fail → backend 发 vncAuthFailed(uid, reason) → 框内提示错误、保留输入框重试。
//
// 本弹窗只负责显示与转发，鉴权逻辑全在 ViewerBackend。
import QtQuick
import QtQuick.Controls
import "../components"
import "../island"          // SvgIcon 唯一真源在共享岛目录（构建期拷到 qml/island/）

Popup {
    id: dlg
    property var theme: null
    property string uid: ""
    property string deviceName: ""

    function showFor(uid, name) {
        dlg.uid = uid
        dlg.deviceName = name || uid
        pwField.text = ""
        errText.text = ""
        dlg.open()
    }
    function showError(reason) {
        errText.text = reason || "密码错误"
        pwField.forceActiveFocus()
    }
    function doSubmit() {
        if (pwField.text.length === 0) { errText.text = "请输入密码"; return }
        errText.text = ""
        backend.submitVncPassword(dlg.uid, pwField.text)
    }

    anchors.centerIn: parent
    width: 420
    height: 224
    modal: true
    padding: 0
    // 必须手动（取消/确认/右上角 X）关闭，别因误点外面把鉴权流打断。
    closePolicy: Popup.CloseOnEscape
    onOpened: pwField.forceActiveFocus()

    background: Rectangle {
        color: dlg.theme ? (dlg.theme.cream || "#141414") : "#141414"
        radius: dlg.theme ? (dlg.theme.rCard || 12) : 12
        border.color: dlg.theme ? (dlg.theme.stroke || "#242424") : "#242424"
        border.width: 1
    }

    contentItem: Column {
        spacing: 0

        // ── 标题行 ──
        Item {
            width: parent.width
            height: 46
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "设备访问密码"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                name: "x"
                tint: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                width: 14; height: 14
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -10
                    onClicked: dlg.close()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? (dlg.theme.line || "#161616") : "#161616"
            }
        }

        // ── 说明 ──
        Item {
            width: parent.width
            height: 34
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "接管「" + dlg.deviceName + "」需要访问密码"
                color: dlg.theme ? dlg.theme.fg3 : "#9A9A9A"
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width - 36
            }
        }

        // ── 密码输入 ──
        Item {
            width: parent.width
            height: 38
            InputField {
                id: pwField
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                th: dlg.theme
                echoMode: TextField.Password
                placeholderText: "请输入设备访问密码"
                maximumLength: 64
                onAccepted: dlg.doSubmit()
            }
        }

        // ── 错误提示 ──
        Item {
            width: parent.width
            height: 22
            Text {
                id: errText
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: ""
                color: "#E0604F"
                font.pixelSize: 11
            }
        }

        // ── 底部动作 ──
        Item {
            width: parent.width
            height: 56
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: dlg.theme ? (dlg.theme.line || "#161616") : "#161616"
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                spacing: 8
                Btn {
                    theme: dlg.theme
                    width: 76
                    text: "取消"
                    onClicked: dlg.close()
                }
                Btn {
                    theme: dlg.theme
                    width: 120
                    text: "确认接管"
                    strong: true
                    onClicked: dlg.doSubmit()
                }
            }
        }
    }
}
