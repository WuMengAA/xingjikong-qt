// 新版外壳 · 状态条（2026-11-01 · 转正后唯一状态条）
//
// 转正时从经典外壳搬来：文案由宿主现算（班级 / 台数 / 在线数 / 正在查看谁），
// 本件只画。高度走宿主注入（shell.statusBarH，32）。
// 与退役的 shell/StatusBar.qml 同一语义，只是不再依赖 ColumnLayout 的 Layout 附加属性。
import QtQuick
import "../components/theme"

Rectangle {
    id: root

    property string text: ""
    property int barHeight: 32

    height: barHeight
    color: Theme.color.surfaceVariant

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
