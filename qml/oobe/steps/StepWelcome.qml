// Step 0 · 欢迎页（2026-10-20）
// 只做一件事：告诉用户这是什么，并给一个明确的主操作。
// 底部另有一个**不显眼**的文字链接做兜底（稍后配置）——取消路径必须有，否则断网就卡死。
import QtQuick
import ".."          // 同向导目录的自绘组件（OobeBigButton 等）

Item {
    id: page
    property var tk: ({})
    property var wizard: null

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)     // 边缘至少留 32 的安全距离
        spacing: 0

        // Logo（字形占位：教室机不装字体图标，用汉字最稳）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96
            height: 96
            radius: 28
            color: tk.cPrimaryCont
            Text {
                anchors.centerIn: parent
                text: "星"
                font.pixelSize: 45
                font.weight: Font.Medium
                color: tk.cOnPrimaryCont
            }
        }

        Item { width: 1; height: 28 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "星集控"
            font.pixelSize: 32
            font.weight: Font.Bold
            color: tk.cOnSurface
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "让教室管理更简单"
            font.pixelSize: 16
            color: tk.cOnSurfaceVar
        }

        Item { width: 1; height: 56 }

        // 主操作：通栏 ≥48 高
        OobeBigButton {
            width: parent.width
            tk: page.tk
            text: "开始绑定"
            onClicked: page.wizard.gotoStep(1)
        }

        Item { width: 1; height: 12 }

        // 兜底：不显眼的文字链接（跳过后进「未绑定/离线」主界面，不再反复弹）
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "暂时跳过，稍后配置"
            font.pixelSize: 13
            color: tk.cOnSurfaceVar
            opacity: area.containsMouse ? 1.0 : 0.75
            MouseArea {
                id: area
                anchors.fill: parent
                anchors.margins: -8            // 小字也要好点中
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.wizard.skipNow()
            }
        }
    }
}
