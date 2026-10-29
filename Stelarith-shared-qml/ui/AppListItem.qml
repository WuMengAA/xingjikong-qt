// 星集控 · 列表项（AppListItem）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · **无边框、无分隔线**（规范要求分区只靠背景色差）⇒ 用 hover 底色 + 间距区分
// · 圆角 Theme.radius.sm（hover 高亮块）
// · 高 48（8 的倍数）；左右 padding Theme.spacing.md
// · 颜色全部走 Theme.color.*，无硬编码
//
// ── 为什么不用 Md3ListItem ──────────────────────────────────────────────
// Md3ListItem 带一条 1px divider（`divider: true`），属"靠线条分区"，与规范冲突。
// 本件改为 hover 时整块浅底高亮（accent），静态靠行距分隔。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppListItem { icon: "🔒"; headline: "锁屏"; onClicked: ... }
//   AppListItem { icon: "📄"; headline: "文件分发"; supporting: "选择系统文件"; onClicked: ... }

import QtQuick
import QtQuick.Layouts
import "theme"

Item {
    id: root

    /** 前置图标（emoji 占位；lucide 接入后换 SvgIcon）。 */
    property string icon: ""
    /** 主文案。 */
    property string headline: ""
    /** 次要说明（可空）。 */
    property string supporting: ""

    signal clicked()

    implicitHeight: supporting !== "" ? 56 : 48

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radius.sm
        color: ma.containsMouse ? Theme.color.accent : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.md
        anchors.rightMargin: Theme.spacing.md
        spacing: Theme.spacing.md

        Text {
            visible: root.icon !== ""
            text: root.icon
            font.pixelSize: 18
            Layout.alignment: Qt.AlignVCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: root.headline
                font.pixelSize: 14
                color: Theme.color.textPrimary
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.supporting !== ""
                text: root.supporting
                font.pixelSize: 12
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
        }

        // 尾部箭头（弱化）
        Text {
            text: "›"
            font.pixelSize: 18
            color: Theme.color.textSecondary
            Layout.alignment: Qt.AlignVCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
