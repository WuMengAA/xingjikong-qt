// PPT 面板 · 标题条（2026-10-27 · I 批）
// 拆出来的理由有两条：① PptDialog 撞 QML 单文件 200 行红线；② 标题条 + 关闭按钮
// 在几个面板里逐字重复过一遍（眼下手写，将来可收敛成通用件）。
import QtQuick
import "../island"   // SvgIcon 唯一真源在共享岛目录（构建期拷到 qml/island/）

Item {
    id: bar

    property var theme: null
    property string title: ""

    signal closed()

    implicitHeight: 46

    Text {
        anchors.verticalCenter: parent.verticalCenter
        x: 18
        text: bar.title
        color: bar.theme ? bar.theme.fg : "#E6E1E5"
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    SvgIcon {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 18
        name: "x"
        tint: bar.theme ? bar.theme.fg3 : "#CAC4D0"
        width: 14
        height: 14
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: bar.closed()
        }
    }
}
