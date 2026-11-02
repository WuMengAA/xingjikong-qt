import QtQuick
import ".."          // 共享 ui 目录（UiRegistry / UiContract 等）
import "../theme"

// 示例界面插件（U2 参考实现）
// ───────────────────────────────────────────────────────────────────────────
// 用意与后端 plugins/samples/echo 一样：给 U3 迁移那 14 个真实面板时当样板，
// 同时让 U2 的「契约 + 宿主」有一个能真跑起来的消费者（不是只有一份没人用的骨架）。
//
// 契约（ui/UiContract.js · ENTRY_API）：
//   · 必须实现 open() / close()
//   · 可选 openWith(args) —— 需要参数的场景（既有「监控」面板要带设备 uid）
//   · 声明 `property var ctx` 收宿主注入的上下文（theme/registry/host/scope/open/close）
//   · 声明 `property var args` 收 open(id, args) 的载荷
//
// ⚠️ 视觉自己说了算：本示例用 Item 铺满宿主；真实面板想用 Popup 也行 ——
//    宿主只认那两个方法名，不管实现形态。
Item {
    id: panel
    anchors.fill: parent
    visible: false

    property var ctx
    property var args

    readonly property var th: (ctx && ctx.theme) ? ctx.theme : Theme

    function open() { visible = true }
    function close() { visible = false }
    function openWith(a) { args = a; visible = true }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.45)        // scrim

        MouseArea { anchors.fill: parent; onClicked: panel.close() }   // 点空白关闭

        Rectangle {
            id: card
            anchors.centerIn: parent
            width: Math.min(420, Math.max(220, parent.width - 48))
            height: col.implicitHeight + 40
            radius: panel.th.rL
            color: panel.th.cSurface
            border.color: panel.th.cOutline
            border.width: 1

            Column {
                id: col
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 10

                Text {
                    width: parent.width
                    text: (panel.args && panel.args.title) ? panel.args.title : "示例界面插件"
                    color: panel.th.cOnSurface
                    font.pixelSize: panel.th.titleM
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: "我是由 UiPanelHost 按声明清单按需实例化的。\n"
                        + "没有一行 if-else 认识我。"
                    color: panel.th.cOnSurfaceVar
                    font.pixelSize: panel.th.bodyM
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: 88
                    height: 34
                    radius: panel.th.rFull
                    color: panel.th.cPrimary
                    Text {
                        anchors.centerIn: parent
                        text: "关闭"
                        color: panel.th.cOnPrimary
                        font.pixelSize: panel.th.labelL
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.close()      // 只关自己；编排归宿主
                    }
                }
            }
        }
    }
}
