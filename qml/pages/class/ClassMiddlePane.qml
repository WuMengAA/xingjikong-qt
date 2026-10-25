// 班级工作台 · 中栏（设备网格 ↔ 班级展板）· #472 拆件
//
// 从 ClassDashboard 抽出来的原因：横屏三栏和竖屏竖排都要这一块，
// 原来两处各写一遍（分段按钮 model + StackLayout + 两个子件），
// 一处改字段另一处忘记改就是"竖屏和横屏不一样"的经典 bug。
//
// 本件只管中栏的**视图切换**，不持有任何数据：
//   · 设备数据从 deviceModel（ClassDeviceModel）进来；
//   · 选中态、分段态都由宿主传，事件原样抛回（deviceClicked）。
import QtQuick
import QtQuick.Layouts
import "../../components/md3"     // Md3SegmentedButton
import "../../island"             // （SvgIcon 由子件自带，这里不直接用）

Item {
    id: pane

    property var md3: ({})
    property var deviceModel: null
    property bool subscribed: false
    property string selectedUid: ""
    /** 当前段：devices / board / members（宿主持有，切段不改数据）。
     *  members = 成员管理（2026-10-24 方案 B）：设备移出/换班。 */
    property string tab: "devices"
    /** members 段用：换班目标名单（backend.classes）。 */
    property var classes: []
    /** members 段用：当前班码。 */
    property string classCode: ""
    /** members 段用：外壳页级 toast。 */
    property var toastFn: null

    signal deviceClicked(string uid)
    signal tabActivated(string tab)

    // 分段定义只写一份（横竖屏共用）
    readonly property var segModel: [
        { value: "devices", label: "设备网格", icon: "layout-grid", tip: "本班设备实时画面" },
        { value: "board",   label: "班级展板", icon: "tv",          tip: "教室大屏常亮视图（规划中）" },
        { value: "members", label: "成员管理", icon: "users",        tip: "设备移出本班 / 换到别的班" }
    ]

    Column {
        anchors.fill: parent
        spacing: 12

        // 分段条：三段的自然宽 ≈330，窄栏（<330）时原来会溢出到邻栏压住按钮
        // （2026-10-25「按钮按不动」的成因之一）—— 改为横向可滑 + 居中夹紧。
        Flickable {
            width: parent.width
            height: 32
            contentWidth: seg.implicitWidth
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Md3SegmentedButton {
                id: seg
                // Flickable 不按 implicitWidth 布局子项：显式给宽，否则所有段叠在 0 宽处
                width: implicitWidth
                x: Math.max(0, (parent.width - width) / 2)
                md3: pane.md3
                fresh: true
                model: pane.segModel
                current: pane.tab
                onActivated: function (v) { pane.tabActivated(v) }
            }
        }

        StackLayout {
            width: parent.width
            height: parent.height - seg.height - 12
            currentIndex: pane.tab === "board" ? 1 : (pane.tab === "members" ? 2 : 0)

            ClassDeviceGrid {
                md3: pane.md3
                model: pane.deviceModel
                subscribed: pane.subscribed
                selectedUid: pane.selectedUid
                onDeviceClicked: function (uid) { pane.deviceClicked(uid) }
            }
            ClassBoardPane { md3: pane.md3 }
            ClassMemberPane {
                md3: pane.md3
                deviceModel: pane.deviceModel
                classes: pane.classes
                classCode: pane.classCode
                subscribed: pane.subscribed
            }
        }
    }
}
