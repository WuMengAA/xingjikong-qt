// 新版外壳 · 状态条（2026-11-01 · 转正后唯一状态条 · QtShadcn 全量）
//
// 转正时从经典外壳搬来：文案由宿主现算（班级 / 台数 / 在线数 / 正在查看谁），本件只画。
// 高度走宿主注入（shell.statusBarH，32）。
// 与退役的 shell/StatusBar.qml 同一语义，只是不再依赖 ColumnLayout 的 Layout 附加属性。
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 顶边分隔线由手画 Rectangle 换成 **ShadcnSeparator**（统一发丝线口径）。
// 本件没有其它控件（纯文字），令牌走 Theme.*。
import QtQuick
import QtShadcn
import "../components/theme"

Rectangle {
    id: root

    property string text: ""
    property int barHeight: 32

    height: barHeight
    color: Theme.color.surfaceVariant

    ShadcnSeparator {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        orientation: Qt.Horizontal
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.pixelSize: 12
        color: Theme.color.textSecondary
        elide: Text.ElideRight
        width: parent.width - Theme.spacing.lg * 2
    }
}
