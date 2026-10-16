import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// ChangelogDialog（U3 从 Main.qml 抽出 · 更新日志）
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
    id: changelogDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 560; height: 460
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: theme.win; radius: theme.rCard; border.color: theme.card; border.width: 1 }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        Text { text: "更新日志"; color: theme.fg; font.pixelSize: 16; font.weight: Font.Medium }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            Column {
                spacing: 18
                Repeater {
                    model: ctx.window.changelogModel
                    delegate: Column {
                        spacing: 6
                        width: changelogDlg.width - 36
                        Row { spacing: 8
                            Text { text: modelData.version; color: theme.inv; font.pixelSize: 13; font.weight: Font.Medium }
                            Rectangle { width: 2; height: 12; color: theme.stroke }
                            Text { text: modelData.date; color: theme.fg3; font.pixelSize: 12 }
                        }
                        Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.title; color: theme.fg; font.pixelSize: 14; font.weight: Font.Medium }
                        Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.highlight; color: theme.fg3; font.pixelSize: 12 }
                        Repeater {
                            model: modelData.items
                            delegate: Row { spacing: 7; width: parent.width
                                Text { text: ({ feat: "新增", fix: "修复", change: "调整" })[modelData.kind] || "·"; color: theme.fg3; font.pixelSize: 12; width: 36 }
                                Text { width: parent.width - 43; wrapMode: Text.Wrap; text: modelData.text; color: theme.op; font.pixelSize: 13 }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 30; radius: theme.rCtrl
            color: theme.hover; border.color: theme.stroke; border.width: 1
            Text { anchors.centerIn: parent; text: "关闭"; color: theme.fg; font.pixelSize: 13 }
            MouseArea { anchors.fill: parent; onClicked: changelogDlg.close() }
        }
    }
}