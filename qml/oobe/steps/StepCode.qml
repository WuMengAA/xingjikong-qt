// Step 2B · 输入绑定码（2026-10-20）
// 收云端发的 6 位一次性码（2026-10-25 起旧站点 XJK-XXXX-XXX 已废止）。
// 格式判定一律走 C++ 的 isCodeFormatValid —— 前后端同一份规则，避免 QML 里再抄一遍正则各自漂移。
import QtQuick
import "../../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）
import ".."

Item {
    id: page
    property var wizard: null

    readonly property bool ready: codeField.text.length >= 6 && oobe.isCodeFormatValid(codeField.text)

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "请输入绑定码"
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.color.textPrimary
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "管理员发给你的那串字符，比如 A7X9Q2"
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }

        Item { width: 1; height: 32 }

        CodeField {
            id: codeField
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            onAccepted: if (page.ready) oobe.submitBindingCode(text)
        }

        Item { width: 1; height: 32 }

        OobeBigButton {
            width: parent.width
            text: "确认绑定"
            active: page.ready
            onClicked: oobe.submitBindingCode(codeField.text)
        }

        Item { width: 1; height: 16 }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 24
            Text {
                text: "改用扫码绑定"
                font.pixelSize: 13
                color: Theme.accentStrong
                opacity: altArea.containsMouse ? 1.0 : 0.85
                MouseArea {
                    id: altArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.wizard.chooseQr()
                }
            }
            Text {
                text: "稍后再绑定"
                font.pixelSize: 13
                color: Theme.color.textSecondary
                opacity: skipArea.containsMouse ? 1.0 : 0.75
                MouseArea {
                    id: skipArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.wizard.skipNow()
                }
            }
        }
    }
}
