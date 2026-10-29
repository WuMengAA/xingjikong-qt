// 被控端 · 底部导航（SvgIcon 版）· 高 80（2026-10-28 模块三；2026-10-30 迁 Theme）
//
// ── 为什么不用共享库里的 Md3NavBar.qml ────────────────────────────────
// 与 viewer 的 BottomNav.qml 同一条理由（那边 2026-10-22 已踩过并改掉）：
// 旧件的图标是 `Text { text: modelData.icon }` —— lucide 名当文字画，本机渲染成
// 方块/英文串，不是图标。本件按项目 SvgIcon 语言重写一份。
// （shared-qml/ui/md3/Md3NavBar.qml 仍是旧实现；改它要两端一起验，
//   属另一批，本批只保证被控端这一处正确。）
//
// 接口对齐 AgentShell 既有用法：items=[{icon,label}] + currentIndex(int) +
// activated(int) —— 壳里那几行 map/赋值不用动。
//
// ── 2026-10-30：令牌从 Md3 迁到 Theme，选中态去紫 ────────────────────────
// 用户点单「不要紫色」。原选中态是 `Md3.cSecondaryCont`（dark `#4A4458` —— 带紫的
// 灰紫），现改 `Theme.color.surfaceVariant`（中性）：选中靠「浅一档底」表达，
// 不靠颜色语义。图标/文字同时提到 `textPrimary`，对比度仍然够。
// 同时按规范（去边框、分区只靠背景色差）**删掉顶部那条 1px 发丝线** ——
// 它本质是 border 的替身；导航用 `background` 底、内容区用 `surface` 底，色差已够分。
//
// ⚠️ QML 零逻辑：选中态用 State（when 绑 currentIndex），不写三元配色判断；
//    色值全部来自 Theme 令牌，本文件零 Hex。
// ⚠️ 不用 QtQuick.Layouts（agent-qml-deps 门禁白名单只有 QtQuick/QtQuick.Window）：
//    等宽靠 `root.width / items.length`，与旧 Md3NavBar 同一算法。
import QtQuick
import "../components/theme"   // Theme 令牌（pragma Singleton）
import "../island"            // SvgIcon（lucide 图标名，禁 emoji）

Item {
    id: root

    /** 导航项：[{ icon, label }]，icon = lucide 名（与 AgentPages.js 同一份真源）。 */
    property var items: []
    property int currentIndex: 0

    signal activated(int index)

    implicitHeight: 80

    Rectangle {
        anchors.fill: parent
        color: Theme.color.background
    }

    Row {
        anchors.fill: parent

        Repeater {
            model: root.items
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                width: root.width / root.items.length
                height: root.height

                // 激活指示器：64×32 胶囊（底色由 State 给，默认透明）
                Rectangle {
                    id: pill
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    width: 64
                    height: 32
                    radius: Theme.radius.full
                    color: "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                    SvgIcon {
                        id: glyph
                        anchors.centerIn: parent
                        name: cell.modelData.icon
                        px: 20
                        tint: Theme.color.textSecondary
                    }
                }
                Text {
                    id: caption
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 48
                    text: cell.modelData.label
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: Theme.color.textSecondary
                }

                // 选中态：胶囊底色 / 图标色 / 文字色三件一起变（State 驱动）
                states: State {
                    name: "active"
                    when: root.currentIndex === cell.index
                    PropertyChanges { target: pill; color: Theme.color.surfaceVariant }
                    PropertyChanges { target: glyph; tint: Theme.color.textPrimary }
                    PropertyChanges { target: caption; color: Theme.color.textPrimary }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.currentIndex = cell.index
                        root.activated(cell.index)
                    }
                }
            }
        }
    }
}
