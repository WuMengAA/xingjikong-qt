import QtQuick 2.15

// 动作按钮（对齐 island.cpp 607-626 的 actions 按钮）
//
// 被控端岛的动作来自 showWithActions(title, desc, icon, form, actions, onAction, ms)
// 典型：["确认"] + 若干回复短语。QML 里 role 分主次：
//   primary   = 实心（如「确认」）
//   secondary = 描边（如回复短语）
Item {
    id: btn

    property var theme
    property string label: ""
    property string role: "secondary"     // primary | secondary
    property bool light: theme ? theme.light : false

    signal clicked()

    implicitWidth: txt.implicitWidth + 22
    implicitHeight: 28

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: {
            if (btn.role === "primary")
                return btn.light ? "#161616" : "#FAFAFA"
            return btn.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10)
        }
        border.width: btn.role === "primary" ? 0 : 1
        border.color: btn.light ? "#D0D0D0" : "#3C3C3C"

        Behavior on color { ColorAnimation { duration: 120 } }

        Text {
            id: txt
            anchors.centerIn: parent
            text: btn.label
            font.pixelSize: 13
            font.weight: Font.Medium
            color: {
                if (btn.role === "primary")
                    return btn.light ? "#FAFAFA" : "#161616"
                return btn.light ? "#161616" : "#FAFAFA"
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: btn.clicked()
    }

    scale: ma.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 90 } }

    MouseArea { id: ma; anchors.fill: parent; visible: false }
}
