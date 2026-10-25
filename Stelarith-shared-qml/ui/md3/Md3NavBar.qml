// MD3 底部导航：高 80，激活项 pill 指示器 64×32（secondaryContainer）+ badge。
// items: [{icon, label, badge}]；currentIndex 双向。
import QtQuick
import "../theme"

Item {
    id: root
    property var items: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: 360
    implicitHeight: 80

    Rectangle {
        anchors.fill: parent
        color: Md3.cCont
    }
    Row {
        anchors.fill: parent
        Repeater {
            model: root.items
            delegate: Item {
                required property var modelData
                required property int index
                width: root.width / root.items.length
                height: 80
                // 激活指示器 pill
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 12
                    width: 64; height: 32; radius: 16
                    color: root.currentIndex === index ? Md3.cSecondaryCont : "transparent"
                    Behavior on color { ColorAnimation { duration: Md3.durShort } }
                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon
                        font.pixelSize: 18
                    }
                    Rectangle {
                        visible: modelData.badge !== undefined && modelData.badge !== ""
                        anchors.right: parent.right
                        anchors.top: parent.top
                        width: 16; height: 16; radius: 8
                        color: Md3.cError
                        Text {
                            anchors.centerIn: parent
                            text: modelData.badge !== undefined ? modelData.badge : ""
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            color: Md3.cOnError
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 48
                    text: modelData.label
                    font.pixelSize: Md3.labelM
                    font.weight: Font.Medium
                    color: root.currentIndex === index ? Md3.cOnSurface : Md3.cOnSurfaceVar
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { root.currentIndex = index; root.activated(index) }
                }
            }
        }
    }
}
