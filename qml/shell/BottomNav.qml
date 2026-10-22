// MD3 底部导航（BottomNav）· 高 80 · 竖屏布局专用（2026-10-22）
//
// 定位（用户点单「界面自适应布局，竖屏横屏两种布局模式」）：
//   横屏 = 左栏 Sidebar(240) + 顶栏 + 内容区；竖屏 = 顶栏 + 内容区 + 底栏(80)。
//   **不是两套导航**：items 与 Sidebar 共用同一份 navItems（{key, icon, label}，
//   icon = lucide 名），激活态视觉语言也保持一致（secondaryContainer pill +
//   onSecondaryContainer 图标文字）——只是同一份菜单的横排形态。
//
// ── 为什么不用共享库里的 Md3NavBar.qml ─────────────────────────────────
// 那个件的图标是 `Text { text: modelData.icon }`（emoji 字形），本机渲染成方块
// （项目 QML 纪律：符号字形 tofu ⇒ SvgIcon 或汉字），且它的 items 形状是
//   {icon, label, badge} 与 Sidebar 的 {key, icon, label} 不同源 ⇒ 拒绝复用，
//   按本项目 SvgIcon 语言重写一份（ui-terms 门禁会盯着新文件）。
import QtQuick
import QtQuick.Layouts
import "../island"          // SvgIcon（lucide 图标名，禁 emoji）

Item {
    id: root

    property var md3: ({})
    /** 菜单项：[{ key, icon, label }]，与 Sidebar 同一份（宿主 shell.navItems）。 */
    property var items: []
    /** 当前选中的 key。 */
    property string current: ""

    signal activated(string key)

    Layout.fillWidth: true
    implicitHeight: 80

    // 底色与横屏侧栏同源（surfaceContainer 一档），保证两种模式"同一块板"
    Rectangle {
        anchors.fill: parent
        color: root.md3 ? root.md3.cCont : "#F3EDF7"
    }
    // 顶部发丝线（与 TopBar 底部那条同口径，弱分隔不抢画）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.md3 ? root.md3.cOutlineVar : "#CAC4D0"
        opacity: 0.5
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.items
            delegate: Item {
                required property var modelData
                readonly property bool on: root.current === modelData.key
                Layout.fillWidth: true
                Layout.fillHeight: true

                // MD3 激活指示器：64×32 胶囊（active indicator）
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    width: 64
                    height: 32
                    radius: 16
                    color: on ? (root.md3 ? root.md3.cSecondaryCont : "#E8DEF8") : "transparent"
                    Behavior on color { ColorAnimation { duration: root.md3 ? root.md3.durShort : 150 } }
                    SvgIcon {
                        anchors.centerIn: parent
                        name: modelData.icon
                        px: 20
                        tint: on ? (root.md3 ? root.md3.cOnSecondaryCont : "#1D192B")
                                : (root.md3 ? root.md3.cOnSurfaceVar : "#49454F")
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 48
                    text: modelData.label
                    font.pixelSize: root.md3 ? root.md3.labelM : 12
                    font.weight: Font.Medium
                    color: on ? (root.md3 ? root.md3.cOnSurface : "#1D1B20")
                            : (root.md3 ? root.md3.cOnSurfaceVar : "#49454F")
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.activated(modelData.key)
                }
            }
        }
    }
}
