// 个人中心 · 偏好卡（2026-10-25 从 SettingsPage 迁来）
//
// 为什么偏好归个人中心而不是系统设置：
//   提醒开关、深浅色、开机自启都是**这个人这台机器**的使用习惯，
//   不是运维配置。老师进不去系统设置（system:settings 只给管理员），
//   但这些开关老师必须能改 —— 原来的位置等于把它们锁给了管理员。
import QtQuick
import QtQuick.Layouts

import "../../components"
import "../../components/md3"

SettingsCard {
    id: card

    title: "偏好"
    gap: 14

    property var ctx

    readonly property bool phone: !!(ctx && ctx.window && ctx.window.isPhone)

    // ── 提醒（后端接真事件：机器真做了才提示）──
    Column {
        width: parent.width
        spacing: 4

        Md3SwitchRow {
            width: parent.width
            md3: card.md3
            text: "操作完成时提示"
            supporting: "设备上真的做完了才说一句（云端收下不算）"
            checked: backend.notifyOnDone
            onToggled: function (on) { backend.notifyOnDone = on }
        }
        Md3SwitchRow {
            width: parent.width
            md3: card.md3
            text: "设备离线时提醒"
            supporting: "与云端断开时弹一条（后台也能看见）"
            checked: backend.notifyOnOffline
            onToggled: function (on) { backend.notifyOnOffline = on }
        }
    }

    // ── 外观（三档：浅色 / 深色 / 跟随系统）──
    Column {
        width: parent.width
        spacing: 8

        Text {
            text: "外观"
            color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
            font.pixelSize: 12
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Repeater {
                model: [
                    { k: "浅色", m: 0 },
                    { k: "深色", m: 1 },
                    { k: "跟随系统", m: 2 }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: card.md3 ? card.md3.rCtrl : 10
                    color: (backend.themeMode === modelData.m)
                           ? (card.md3 ? card.md3.cSecondaryCont : "#E8DEF8")
                           : "transparent"
                    border.color: (backend.themeMode === modelData.m)
                                  ? (card.md3 ? card.md3.cPrimary : "#6750A4")
                                  : (card.md3 ? card.md3.cOutlineVar : "#CAC4D0")
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData.k
                        color: (backend.themeMode === modelData.m)
                               ? (card.md3 ? card.md3.cOnSecondaryCont : "#1D192B")
                               : (card.md3 ? card.md3.cOnSurfaceVar : "#49454F")
                        font.pixelSize: 13
                        font.weight: (backend.themeMode === modelData.m) ? Font.Medium : Font.Normal
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.themeMode = modelData.m
                    }
                }
            }
        }
    }

    // ── 启动（Windows 专属，手机隐藏）──
    Column {
        width: parent.width
        spacing: 4
        visible: !card.phone

        Md3SwitchRow {
            width: parent.width
            md3: card.md3
            text: "开机自动启动"
            supporting: "登录 Windows 后自动打开（免管理员）"
            checked: backend.autoStart
            onToggled: function (on) { backend.setAutoStart(on) }
        }
        Md3SwitchRow {
            width: parent.width
            md3: card.md3
            text: "桌面快捷方式"
            supporting: "在桌面放一枚「星集控管理端」图标"
            checked: backend.desktopShortcut
            onToggled: function (on) { backend.setDesktopShortcut(on) }
        }
    }
}
