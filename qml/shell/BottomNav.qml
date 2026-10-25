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
import "../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: root

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
        color: Md3.cCont
    }
    // 顶部发丝线（与 TopBar 底部那条同口径，弱分隔不抢画）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Md3.cOutlineVar
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
                    color: on ? (Md3.cSecondaryCont) : "transparent"
                    Behavior on color { ColorAnimation { duration: Md3.durShort} }
                    SvgIcon {
                        anchors.centerIn: parent
                        name: modelData.icon
                        px: 20
                        tint: on ? (Md3.cOnSecondaryCont)
                                : (Md3.cOnSurfaceVar)
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 48
                    text: modelData.label
                    font.pixelSize: Md3.labelM
                    font.weight: Font.Medium
                    color: on ? (Md3.cOnSurface)
                            : (Md3.cOnSurfaceVar)
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
