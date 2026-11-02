// 星集控 · 分段按钮（AppSegmentedButton）—— 2026-10-29 QtShadcn 规范
//
// ── 定位 ────────────────────────────────────────────────────────────────
// 语义：互斥的**视图切换**（当前在哪一段要一眼看出）—— 与 AppChip（可多选筛选项）分工不同。
//
// 1. **去边框 / 去描边**：旧件未选段透明、选中段 secondaryContainer 浅底；规范下去边框后
//    浅底（surfaceVariant #F4F4F5）与页面底（#FAFAFA）几乎分不开 ⇒ 选中段改 **primary 实底**
//    （与 AppChip 同一口径：靠实底色差，不靠线条）。
// 2. **颜色走 Theme 令牌**，本件零硬编码 Hex。
//    App 系只有 Theme 一套语义，保留它等于两套真源（AGENTS.md §9）。
//    ⇒ 三处消费方若传了 fresh，删掉即可（选中态本就是 primary 实底）。
// 4. **保留两个必须兼容的行为**：
//    · `label === ""` = 纯图标段，定宽 44（ClassMiddlePane 窄中栏依赖它算出 188 < 240）；
//    · ToolTip（段说明）保留，Flickable 里 `width: implicitWidth` 的写法照旧。
//
// ── 图标 ────────────────────────────────────────────────────────────────
// 走 SvgIcon（lucide 实体在 resources/icons/），**不用 emoji**（本机 emoji 渲染成 tofu）。
// `../island` 的相对路径在三种布局下都成立：
//   · 源码树   ui/            → ../island = Stelarith-shared-qml/island ✓
//   · viewer 构建 components/ → ../island = qml/Stelarith/island ✓
//   · agent  构建 components/ → ../island = qml/island ✓
//
// 用法：
//   AppSegmentedButton {
//       model: [ { value: "grid", label: "设备网格", icon: "layout-grid", tip: "…" }, … ]
//       current: pane.mode
//       onActivated: function (v) { pane.mode = v }
//   }

import QtQuick
import QtQuick.Controls          // ToolTip（段说明）
import "../island"               // SvgIcon
import "theme"

Item {
    id: root

    /** 段定义：[{ value, label, icon, tip }]；label 空串 = 纯图标段（定宽 44）。 */
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
        spacing: Theme.spacing.xs

        Repeater {
            model: root.model
            delegate: Rectangle {
                id: seg
                required property var modelData
                readonly property bool on: root.current === seg.modelData.value
                readonly property bool pure: (seg.modelData.label === undefined)
                                             || seg.modelData.label === ""

                height: 32
                radius: Theme.radius.md
                // 文字段宽 = 图标16 + 间距6 + 文字 + 左右各 12 内边距；纯图标段定宽 44
                width: seg.pure ? 44 : (16 + 6 + segTx.implicitWidth + 24)
                color: seg.on ? Theme.accentStrong
                              : (segMa.containsMouse ? Theme.color.accent : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    SvgIcon {
                        name: seg.modelData.icon || ""
                        px: 16
                        tint: seg.on ? Theme.accentStrongForeground : Theme.color.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: segTx
                        visible: !seg.pure
                        text: seg.pure ? "" : seg.modelData.label
                        font.pixelSize: 13
                        font.weight: seg.on ? Font.Medium : Font.Normal
                        color: seg.on ? Theme.accentStrongForeground : Theme.color.textSecondary
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
