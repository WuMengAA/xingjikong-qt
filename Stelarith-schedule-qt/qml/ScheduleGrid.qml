import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#1e1e1e"

    // 顶部工具栏
    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: "#252526"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            Label {
                text: qsTr("课表网格")
                color: "#ccc"
                font.pixelSize: 14
                Layout.fillWidth: true
            }

            ComboBox {
                Layout.preferredWidth: 140
                model: [
                    qsTr("全部周次"),
                    qsTr("第 1 周"),
                    qsTr("第 2 周"),
                    qsTr("双周轮换")
                ]
                currentIndex: 0
            }
        }
    }

    // 表格视图：行=时间点，列=周一~周日
    TableView {
        id: grid
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8

        model: scheduleModel
        clip: true

        // 固定行高/列宽
        rowHeightProvider: (row) => 56
        columnWidthProvider: (col) => grid.width / (scheduleModel.columnCount + 1)

        // delegate：第一列显示时间，其余显示科目
        delegate: Rectangle {
            required property int row
            required property int column

            implicitWidth: 80
            implicitHeight: 56

            color: (column === 0) ? "#252526" : (row % 2 === 0 ? "#1e1e1e" : "#232323")

            Text {
                anchors.centerIn: parent
                text: column === 0
                    ? scheduleModel.rowHeader(row)
                    : scheduleModel.cellText(row, column - 1)
                color: column === 0 ? "#aaa" : "#fff"
                font.pixelSize: 13
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
            }
        }
    }
}
