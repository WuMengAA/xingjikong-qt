// 首次启动引导 · 主按钮（2026-10-20）
// 为什么不用 Md3Btn：MD3 规范的按钮高 40，而触屏/教室机场景要求**主操作至少 48**，
// 且本向导的主按钮是「整行通栏」——Md3Btn 是药丸自适宽，撑不出通栏效果。
// 自绘一个（不用 Qt Button，遵项目纪律），尺寸/状态层按 MD3 取。
import QtQuick

Rectangle {
    id: root
    property var tk: ({})
    property string text: ""
    property bool primary: true
    property bool active: true          // ⚠️ 不能叫 enabled：会盖掉 Item.enabled（项目已踩过的坑）
    signal clicked()

    implicitHeight: 52                    // ≥48：教室机触摸友好
    implicitWidth: 200
    radius: tk.rFull
    color: primary ? (active ? tk.cPrimary : tk.cContHighest)
                   : (active ? tk.cContHigh : tk.cCont)
    border.width: primary ? 0 : 1
    border.color: tk.cOutlineVar
    opacity: active ? 1 : 0.6

    Text {
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 16
        font.weight: Font.Medium
        color: root.primary ? tk.cOnPrimary : tk.cOnSurface
    }

    // MD3 状态层：hover 8% / press 10%
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: tk.cOnSurface
        opacity: mouse.pressed ? tk.stPress : (mouse.containsMouse ? tk.stHover : 0)
        visible: opacity > 0
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.active
        cursorShape: root.active ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
