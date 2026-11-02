import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// HelpDialog（U3 从 Main.qml 抽出 · 帮助）
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
    id: helpDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 480; height: 420
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: theme.win; radius: theme.rCard; border.color: theme.card; border.width: 0 }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        Text { text: "帮助"; color: theme.fg; font.pixelSize: 16; font.weight: Font.Medium }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            Column {
                spacing: 12
                width: helpDlg.width - 36
                Text { width: parent.width; wrapMode: Text.Wrap; text: "管理端统一管设备。常用操作："; color: theme.fg3; font.pixelSize: 13 }
                Repeater {
                    model: [
                        { t: "看某台设备画面", d: "控制页左边选设备。" },
                        { t: "广播通知 / 屏幕广播", d: "在集控页广播通知，或将教师端画面同步至全部设备。" },
                        { t: "按班级筛选设备", d: "设备列表上方点「全部班级」切换。" },
                        { t: "锁屏 / 重启 / 关机", d: "控制页右侧「电源」，需管理员。" },
                        { t: "查看更新历史", d: "设置页「更新日志」。" },
                        { t: "跳页与触发动作", d: "顶部命令入口或快捷键。" }
                    ]
                    delegate: Column {
                        spacing: 3
                        width: parent.width
                        Text { text: modelData.t; color: theme.fg; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.d; color: theme.fg3; font.pixelSize: 12 }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 30; radius: theme.rCtrl
            color: theme.hover; border.color: theme.stroke; border.width: 0
            Text { anchors.centerIn: parent; text: "关闭"; color: theme.fg; font.pixelSize: 13 }
            MouseArea { anchors.fill: parent; onClicked: helpDlg.close() }
        }
    }
}