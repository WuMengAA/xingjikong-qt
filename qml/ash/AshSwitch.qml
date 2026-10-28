import QtQuick

// 开关（shadcn Switch）—— 纯 QtQuick，不用 QtQuick.Controls
//
// 照 ShadcnSwitch.qml：track 32×18、圆角 h/2、thumb 14（= h-4）、
// 位移 = w - thumb - 2 = 16；勾选时 track 用 primary 色（浅色主题下是近黑，不是蓝）。
// 聚焦环：3px、ring 色 30% 透明（ShadcnSwitch.qml:52-53）。
Item {
    id: root

    property var th
    property bool checked: false

    signal toggled(bool value)

    implicitWidth: th ? th.switchW : 32
    implicitHeight: th ? th.switchH : 18

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.th ? root.th.input : "#e4e4e7"
        Behavior on color { ColorAnimation { duration: root.th ? root.th.dur : 150 } }
    }

    // 聚焦环（键盘/点击聚焦时出现）
    Rectangle {
        anchors.fill: track
        anchors.margins: -3
        radius: (track.height + 6) / 2
        color: "transparent"
        border.width: 3
        border.color: root.th ? Qt.rgba(root.th.ring.r, root.th.ring.g, root.th.ring.b, 0.30)
                              : Qt.rgba(0.63, 0.63, 0.67, 0.30)
        visible: ma.activeFocus
    }

    Rectangle {
        id: thumb
        width: root.th ? root.th.switchThumb : 14
        height: width
        radius: height / 2
        anchors.verticalCenter: parent.verticalCenter
        x: 2
        color: root.th ? root.th.background : "#ffffff"

        // 勾选：thumb 右移 + 提到 primary 反色（浅色主题下是白底黑点不变，靠 track 变黑区分）
        states: State {
            name: "on"
            when: root.checked
            PropertyChanges {
                target: thumb
                x: track.width - thumb.width - 2
            }
        }
        transitions: Transition {
            NumberAnimation {
                target: thumb
                property: "x"
                duration: root.th ? root.th.dur : 150
                easing.type: Easing.OutCubic
            }
        }
    }

    // track 勾选态：用 State 而不是三元
    states: State {
        name: "on"
        when: root.checked
        PropertyChanges {
            target: track
            color: root.th ? root.th.primary : "#18181b"
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        anchors.margins: -6      // 命中区放大到 44 高，符合点击目标下限
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
