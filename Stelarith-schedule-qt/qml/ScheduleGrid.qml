import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 课表网格：行=时间点，列=周一~周日；点击科目格可调课（选科目/清空）
Rectangle {
    id: root
    color: "#1e1e1e"

    // 当前操作的格子坐标
    property int editRow: -1
    property int editCol: -1   // 0=周一

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
                text: qsTr("课表网格 · 点击科目可调课")
                color: "#ccc"
                font.pixelSize: 14
                Layout.fillWidth: true
            }

            ComboBox {
                Layout.preferredWidth: 140
                model: [
                    qsTr("第 1 周"),
                    qsTr("第 2 周"),
                    qsTr("第 3 周"),
                    qsTr("第 4 周")
                ]
                currentIndex: Math.max(0, scheduleModel.currentWeek - 1)
                onActivated: (i) => scheduleModel.setCurrentWeek(i + 1)
            }
        }
    }

    // 表格视图
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

        // delegate
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
                onClicked: {
                    if (column === 0) return  // 时间列不可调
                    root.editRow = row
                    root.editCol = column - 1
                    // 构建科目菜单
                    subjectMenu.reset()
                    for (var i = 0; i < subjectModel.count(); ++i) {
                        subjectMenu.addItem(subjectModel.nameAt(i), subjectModel.idAt(i))
                    }
                    subjectMenu.openAt(mouseX, mouseY, parent)
                }
            }
        }
    }

    // 调课菜单（动态生成）
    Item {
        id: subjectMenu
        property var entries: []

        function reset() { entries = [] }
        function addItem(name, id) { entries.push({ name: name, id: id }) }

        function openAt(mx, my, parentItem) {
            // 用 Popup 展示
            menuPopup.x = parentItem.mapToItem(root, mx, my).x
            menuPopup.y = parentItem.mapToItem(root, mx, my).y
            menuPopup.entries = entries
            menuPopup.open()
        }
    }

    Popup {
        id: menuPopup
        property var entries: []
        modal: true
        anchors.centerIn: parent
        width: 220
        padding: 0

        Column {
            width: parent.width
            spacing: 0

            // 清空项
            Rectangle {
                width: parent.width
                height: 36
                color: mouse.hovered ? "#2d2d2d" : "#252526"
                Text { text: qsTr("（清空这节课）"); anchors.centerIn: parent; color: "#aaa"; font.pixelSize: 13 }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        scheduleModel.setCellSubject(root.editRow, root.editCol, "")
                        menuPopup.close()
                    }
                }
            }

            // 科目项
            Repeater {
                model: menuPopup.entries
                delegate: Rectangle {
                    width: parent.width
                    height: 36
                    color: hover.hovered ? "#2d2d2d" : "#252526"
                    Text {
                        text: modelData.name
                        anchors.centerIn: parent
                        color: "#fff"
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: hover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            scheduleModel.setCellSubject(root.editRow, root.editCol, modelData.id)
                            menuPopup.close()
                        }
                    }
                }
            }
        }
    }
}
