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
import "../components/md3"     // Md3SegmentedButton
import "../island"             // （SvgIcon 由子件自带，这里不直接用）

Item {
    id: pane

    property var md3: ({})
    property var deviceModel: null
    property bool subscribed: false
    property string selectedUid: ""
    /** 当前段：devices / board（宿主持有，切段不改数据）。 */
    property string tab: "devices"

    signal deviceClicked(string uid)
    signal tabActivated(string tab)

    // 分段定义只写一份（横竖屏共用）
    readonly property var segModel: [
        { value: "devices", label: "设备网格", icon: "layout-grid", tip: "本班设备实时画面" },
        { value: "board",   label: "班级展板", icon: "tv",          tip: "教室大屏常亮视图（规划中）" }
    ]

    Column {
        anchors.fill: parent
        spacing: 12

        Md3SegmentedButton {
            id: seg
            anchors.horizontalCenter: parent.horizontalCenter
            md3: pane.md3
            model: pane.segModel
            current: pane.tab
            onActivated: function (v) { pane.tabActivated(v) }
        }

        StackLayout {
            width: parent.width
            height: parent.height - seg.height - 12
            currentIndex: pane.tab === "board" ? 1 : 0

            ClassDeviceGrid {
                md3: pane.md3
                model: pane.deviceModel
                subscribed: pane.subscribed
                selectedUid: pane.selectedUid
                onDeviceClicked: function (uid) { pane.deviceClicked(uid) }
            }
            ClassBoardPane { md3: pane.md3 }
        }
    }
}
