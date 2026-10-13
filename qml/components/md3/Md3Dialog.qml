// MD3 对话框：scrim（32% 黑）+ 居中卡片（圆角 28）。
// actions: [{text, role}]，role = cancel（text 按钮）/ confirm（filled）/ danger（filled error）。
import QtQuick
import "."          // 同目录的 Md3Btn（QML 同目录类型需显式导入才稳）

Item {
    id: root
    property var md3: ({})
    property bool opened: false
    property string title: ""
    property string text: ""
    property var actions: []
    signal actionClicked(int index, string role)

    visible: opened
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.32)
        MouseArea { anchors.fill: parent }   // 挡背后点击（MD3 模态）
    }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 312)
        height: dialogCol.implicitHeight + 48
        radius: md3.rXL
        color: md3.cContHigh
        Column {
            id: dialogCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 16
            Text {
                text: root.title
                font.pixelSize: md3.headlineS
                color: md3.cOnSurface
                width: parent.width
            }
            Text {
                text: root.text
                font.pixelSize: md3.bodyM
                color: md3.cOnSurfaceVar
                width: parent.width
                wrapMode: Text.Wrap
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            spacing: 8
            Repeater {
                model: root.actions
                delegate: Md3Btn {
                    required property var modelData
                    required property int index
                    md3: root.md3
                    text: modelData.text
                    variant: modelData.role === "cancel" ? "text"
                           : (modelData.role === "danger" ? "error" : "filled")
                    onClicked: root.actionClicked(index, modelData.role)
                }
            }
        }
    }
}
