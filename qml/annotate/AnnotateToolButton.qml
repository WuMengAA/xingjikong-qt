// 批注底部工具条的单枚按钮（2026-10-27 用户实测点单后补）
//
// 为什么用**中文标签**而不是图标：
//   本窗口由 QQuickView 单独建引擎加载，app 主引擎里那两样东西都不在这儿 ——
//   ① `image://svgicon/*` 图片 provider（那是在 agent 的 main 里注册的）；
//   ② qrc 里的图标资源。
//   踩过一次：岛上用 SvgIcon 正常，批注层里就只剩空白方块，
//   日志刷 "Failed to get image from provider"。而老师也不认识抽象图标：
//   写「橡皮」比画一块橡皮快，还不会被误认成面包。
import QtQuick

Rectangle {
    id: btn

    property string label: ""
    property bool active: false
    property int minWidth: 62          // 收起按钮（▾）这类窄按钮用它压窄
    signal clicked()

    implicitWidth: Math.max(btn.minWidth, lbl.implicitWidth + 26)
    height: 36
    radius: 18
    color: btn.active ? "#FF5A5F"
                      : (ma.containsMouse ? "#2BFFFFFF" : "transparent")
    Behavior on color { ColorAnimation { duration: 110 } }

    Text {
        id: lbl
        anchors.centerIn: parent
        text: btn.label
        color: btn.active ? "#FFFFFF" : "#EDE7F6"
        font.pixelSize: 13
        font.weight: btn.active ? Font.DemiBold : Font.Medium
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // 只吃左键：右键在画布上是"撤销一笔"，在工具条上不该误触（手滑清空很致命）
        acceptedButtons: Qt.LeftButton
        onClicked: btn.clicked()
    }
}
