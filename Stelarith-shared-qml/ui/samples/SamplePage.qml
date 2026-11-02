import QtQuick
import ".."          // 共享 ui 目录（UiRegistry / UiContract 等）
import "../theme"

// 示例**整屏页**（U5 参考实现）
// ───────────────────────────────────────────────────────────────────────────
// 与 `SamplePanel.qml` 对照着看：那个演示的是「浮层面板」，这个演示的是「整屏页」。
// 同一个契约、同一个 ctx 形状，差别只在宿主怎么摆它（页是底、面板是浮层）。
//
// 契约（ui/UiContract.js）：
//   · 声明里要有 `surfaces: ["page"]` + `entry`（缺 entry 会被 validate 报错）
//   · 根对象自己 `anchors.fill: parent`
//   · `property var ctx` 收宿主注入的上下文（CTX_FIELDS）
//   · 页**没有** open()/close() —— 页的显隐归宿主（currentIndex 说了算），
//     这点与面板不同：面板是"叫它才出来"，页是"切过去就在"。
Item {
    id: page
    anchors.fill: parent
    visible: false

    property var ctx

    readonly property var th: (ctx && ctx.theme) ? ctx.theme : Theme

    // 本页对应的声明（宿主把"现在是第几页"也放在 ctx.host 上）
    readonly property var decl: (ctx && ctx.host && ctx.host.currentPage) ? ctx.host.currentPage : null

    Rectangle { anchors.fill: parent; color: page.th.cBackground }

    Column {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 80)
        spacing: 14

        Text {
            width: parent.width
            text: page.decl ? page.decl.title : "示例整屏页"
            color: page.th.cOnSurface
            font.pixelSize: page.th.headlineS
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: "我是由 UiPageHost 按声明清单实例化的整屏页。\n"
                + "声明 id = " + (page.decl ? page.decl.id : "?") + "\n"
                + "此刻 ctx.host.currentId = " + (page.ctx && page.ctx.host ? page.ctx.host.currentId : "?") + "\n"
                + "ctx 字段 = " + (page.ctx ? Object.keys(page.ctx).join(" / ") : "（无宿主）")
            color: page.th.cOnSurfaceVar
            font.pixelSize: page.th.bodyM
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.4
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            // 页间跳转：走 ctx.goTo/goToId（宿主给的能力），页自己不认识别的页
            Rectangle {
                width: 120; height: 36; radius: page.th.rFull
                color: page.th.cPrimary
                Text {
                    anchors.centerIn: parent
                    text: "切到示例页 B"
                    color: page.th.cOnPrimary
                    font.pixelSize: page.th.labelL
                    font.weight: Font.Medium
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (page.ctx && page.ctx.goToId) page.ctx.goToId("sample-page-b")
                }
            }

            Rectangle {
                width: 120; height: 36; radius: page.th.rFull
                color: "transparent"
                border.width: 1
                border.color: page.th.cOutline
                Text {
                    anchors.centerIn: parent
                    text: "关掉所有面板"
                    color: page.th.cOnSurface
                    font.pixelSize: page.th.labelL
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (page.ctx && page.ctx.closePanels) page.ctx.closePanels()
                }
            }
        }
    }
}