// Step 2A · 扫码绑定（2026-10-20）
// 二维码由后端给矩阵、本页只画；每 2 秒问一次「绑上没有」（轮询在 C++ 侧，本页只读 bindingState）。
// 绑定成功的自动跳转在**根向导**里统一处理（一处判断，扫码页/输入页都吃同一份状态）。
import QtQuick
import "../../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）
import ".."

Item {
    id: page
    property var wizard: null

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "用手机扫这个二维码"
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.color.textPrimary
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "扫码后在手机上确认，这台设备会自动完成绑定。"
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }

        Item { width: 1; height: 24 }

        // 二维码卡片（纯白底 + 微阴影；白底是扫码识别需要，不跟深色主题走）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 280
            height: 280
            radius: Theme.radius.md
            color: Theme.color.surface

            QrCanvas {
                id: qr
                anchors.fill: parent
                anchors.margins: 8
                matrix: oobe.qrMatrix
                visible: oobe.qrMatrix.length > 0
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: oobe.qrMatrix.length === 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: oobe.qrBusy ? "正在生成二维码…" : "二维码还没出来"
                    font.pixelSize: 14
                    color: Theme.color.textSecondary
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: oobe.qrBusy ? "" : "点下面的「换一张」试试"
                    font.pixelSize: 12
                    color: Theme.color.textSecondary
                }
            }
        }

        Item { width: 1; height: 20 }

        // 状态行：等待 / 已生成
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: oobe.bindingState === page.wizard.stWaiting ? Theme.accentStrong : Theme.color.textSecondary
            }
            Text {
                text: oobe.bindingState === page.wizard.stWaiting ? "等待手机扫码…" : "准备好了就点「换一张」"
                font.pixelSize: 13
                color: Theme.color.textSecondary
            }
        }

        Item { width: 1; height: 24 }

        OobeBigButton {
            width: parent.width
            text: oobe.qrBusy ? "正在生成…" : "换一张"
            primary: false
            active: !oobe.qrBusy
            onClicked: oobe.refreshQrCode()
        }

        Item { width: 1; height: 16 }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 24
            Text {
                text: "改用输入绑定码"
                font.pixelSize: 13
                color: Theme.accentStrong
                opacity: altArea.containsMouse ? 1.0 : 0.85
                MouseArea {
                    id: altArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.wizard.chooseCode()
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
