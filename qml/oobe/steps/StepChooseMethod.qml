// Step 1 · 选择绑定方式（2026-10-20）
// 只让用户做一件事：二选一。两张巨卡，整卡可点。
// 单向流程：这里可以退回欢迎页（不算跳跃），但不能直接飞到后面步骤。
import QtQuick
import ".."

Item {
    id: page
    property var tk: ({})
    property var wizard: null

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.wizard.isBroadcast ? "如何绑定这台广播机？" : "你要如何绑定这台设备？"
            font.pixelSize: 24
            font.weight: Font.Bold
            color: tk.cOnSurface
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.wizard.isBroadcast
                  ? "绑定的是广播站码（与班级码同一套码），绑好后可以在设置里改。"
                  : "两种方式任选一种，绑好后可以在设置里改。"
            font.pixelSize: 13
            color: tk.cOnSurfaceVar
        }

        Item { width: 1; height: 32 }

        OobeCardButton {
            width: parent.width
            tk: page.tk
            glyph: "扫"
            title: "扫码绑定"
            subtitle: page.wizard.isBroadcast
                      ? "用手机扫广播站绑定码（由管理员生成）"
                      : "使用手机小程序或公众号扫码"
            recommended: true
            onClicked: page.wizard.chooseQr()
        }

        Item { width: 1; height: 16 }

        OobeCardButton {
            width: parent.width
            tk: page.tk
            glyph: "码"
            title: "输入绑定码"
            subtitle: page.wizard.isBroadcast
                      ? "输入广播站码或管理员发的接入码"
                      : "输入管理员发给你的绑定码"
            onClicked: page.wizard.chooseCode()
        }

        Item { width: 1; height: 24 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "返回"
            font.pixelSize: 13
            color: tk.cOnSurfaceVar
            opacity: backArea.containsMouse ? 1.0 : 0.75
            MouseArea {
                id: backArea
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.wizard.gotoStep(1)   // 回「用途」而不是回欢迎页
            }
        }
    }
}
