// 系统设置 · 分类「关于与诊断」（2026-10-25 从 SettingsPage 拆出）
//
// ⚠️ 2026-10-25 修：原实现这里写的是裸 `diagDlg.refreshLog()` ——
//   `diagDlg` 是 Main.qml 里的**组件内 id**，独立 QML 文件根本看不见，
//   点「诊断与反馈」只会抛 ReferenceError（GUI 子系统下连控制台都没有，
//   等于按钮静默失灵）。改走 ctx.window.openDiagnostics()，与课表编辑器同款。
import QtQuick

import "../../components"
import "../../components/theme"   // Md3 令牌（pragma Singleton）

SettingsCard {
    id: card

    title: "关于"
    gap: 8

    property var ctx

    Text {
        text: "星集控 · 管理端"
        color: Md3.cOnSurface
        font.pixelSize: 13
    }

    Item {
        width: parent.width
        height: 18
        Text {
            id: verLabel
            text: "版本"
            color: Md3.cOnSurfaceVar
            font.pixelSize: 12
            width: 70
        }
        Text {
            anchors.left: verLabel.right
            anchors.right: parent.right
            text: backend.version
            color: Md3.cPrimary
            font.pixelSize: 12
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: "被控端安装在教室设备上（星集控被控端），登录同一账号后就会出现在这里。"
              + "要看某台设备，在控制页左边选。"
        color: Md3.cOnSurfaceVar
        font.pixelSize: 12
    }

    // 更新日志 / 帮助 / 诊断：三颗自绘按钮（不用默认 Button，那个蓝会把界面破掉）
    Row {
        spacing: 8

        Rectangle {
            width: 96; height: 30; radius: Md3.rCtrl
            color: h1.containsMouse
                   ? (Md3.cContHigh)
                   : (Md3.cContLow)
            border.color: Md3.cOutlineVar
            border.width: 1
            Text { anchors.centerIn: parent; text: "更新日志"; color: Md3.cOnSurface; font.pixelSize: 13 }
            MouseArea {
                id: h1
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (card.ctx) card.ctx.open("changelog")
            }
        }

        Rectangle {
            width: 80; height: 30; radius: Md3.rCtrl
            color: h2.containsMouse
                   ? (Md3.cContHigh)
                   : (Md3.cContLow)
            border.color: Md3.cOutlineVar
            border.width: 1
            Text { anchors.centerIn: parent; text: "帮助"; color: Md3.cOnSurface; font.pixelSize: 13 }
            MouseArea {
                id: h2
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (card.ctx) card.ctx.open("help")
            }
        }

        Rectangle {
            width: 110; height: 30; radius: Md3.rCtrl
            color: h3.containsMouse
                   ? (Md3.cContHigh)
                   : (Md3.cContLow)
            border.color: Md3.cOutlineVar
            border.width: 1
            Text { anchors.centerIn: parent; text: "诊断与反馈"; color: Md3.cOnSurface; font.pixelSize: 13 }
            MouseArea {
                id: h3
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (card.ctx) card.ctx.window.openDiagnostics()
            }
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: "日志：Stelarith-viewer-qt/viewer.log"
        color: Md3.cOnSurfaceVar
        font.pixelSize: 12
    }
}
