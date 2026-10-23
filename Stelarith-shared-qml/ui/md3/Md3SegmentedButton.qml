// MD3 SegmentedButton（分段按钮）· 2026-10-23 #461 升为共享组件库第 13 件
//
// 来历：原型是 qml/components/CameraCenter.qml 里一段 56 行的内联实现（模式切换：
// 仅屏幕/双画面/仅摄像头）。班级工作台中栏「设备网格 ↔ 展板」是同一范式 ⇒ 收编到
// 共享库，此后全库只有这一份（两份真源必然漂移，见 AGENTS.md §9）。
//
// 规格（MD3 segmented button，single-select）：
//   · 高 32、圆角 rM(12)、段间距 spXS(4)；
//   · 选中段 secondaryContainer 底 + onSecondaryContainer 字/图标；
//   · 未选透明底，hover 抬 containerHigh（状态层思路：不抢选中色的语义）；
//   · 无边框、无药丸 —— 与 Md3Chip 的区别：Chip 是"可多选的筛选项"（带 ✓），
//     Segmented 是"互斥的视图切换"（一眼看得出当前在哪一段）。
//
// 图标走 SvgIcon（lucide，随 exe 打包）——**不用 emoji**：本机 emoji 渲染成方块（tofu）。
// import "../../island" 的相对路径在三种布局下都成立：
//   · 源码树   ui/md3/ → ../../island = Stelarith-shared-qml/island/ ✓
//   · viewer 构建 components/md3/ → ../../island = qml/Stelarith/island/ ✓
//   · agent  构建 qml/components/md3/ → ../../island = qml/island/ ✓
//   · Android qrc  components/md3/ → ../../island = qrc:/island/ ✓
import QtQuick
import QtQuick.Controls          // ToolTip（段说明）
import "../../island"            // SvgIcon

Item {
    id: root

    property var md3: ({})
    /**
     * 段定义：[{ value, label, icon, tip }]
     *   value —— 任意值（字符串/数字），activated 原样带回；
     *   label —— 文字；空字符串 = 纯图标段（定宽 44，与 CameraCenter 原版一致）；
     *   icon  —— lucide 图标名（SvgIcon）；
     *   tip   —— 悬停说明（可省）。
     */
    property var model: []
    /** 当前选中的 value（与某段 value 严格相等即选中）。 */
    property var current: ""
    /** 点了哪段（value 原样带回；由调用方决定是否改变 current）。 */
    signal activated(var value)

    implicitWidth: segRow.implicitWidth
    implicitHeight: 32

    Row {
        id: segRow
        anchors.centerIn: parent
        spacing: md3.spXS

        Repeater {
            model: root.model
            delegate: Rectangle {
                id: seg
                required property var modelData
                readonly property bool on: root.current === seg.modelData.value

                height: 32
                radius: md3.rM
                // 文字段宽 = 图标16 + 间距6 + 文字 + 左右各 12 内边距；纯图标段定宽 44
                width: (seg.modelData.label !== "")
                       ? (16 + 6 + segTx.implicitWidth + 24)
                       : 44
                color: seg.on ? md3.cSecondaryCont
                              : (segMa.containsMouse ? md3.cContHigh : "transparent")
                Behavior on color { ColorAnimation { duration: md3.durShort } }

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    SvgIcon {
                        name: seg.modelData.icon
                        px: 16
                        tint: seg.on ? md3.cOnSecondaryCont : md3.cOnSurfaceVar
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: segTx
                        visible: seg.modelData.label !== ""
                        text: seg.modelData.label
                        font.pixelSize: md3.labelM
                        color: seg.on ? md3.cOnSecondaryCont : md3.cOnSurfaceVar
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: segMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.activated(seg.modelData.value)
                }
                ToolTip {
                    visible: segMa.containsMouse && (seg.modelData.tip || "") !== ""
                    delay: 400
                    text: seg.modelData.tip || ""
                }
            }
        }
    }
}
