import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1024
    height: 768
    title: qsTr("星集控 · 课表编辑器")

    property int currentView: 0

    // 顶部工具栏
    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: "#252526"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            Label {
                text: qsTr("星集控 · 课表编辑器")
                color: "#ccc"
                font.pixelSize: 15
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                id: saveStatus
                text: qsTr("示例档案")
                color: "#666"
                font.pixelSize: 11
            }
            Button {
                text: qsTr("保存")
                onClicked: {
                    if (scheduleModel.saveTo(profilePath)) {
                        saveStatus.text = qsTr("已保存 → %1").arg(profilePath)
                        saveStatus.color = "#6ecb63"
                    } else {
                        saveStatus.text = qsTr("保存失败：%1").arg(profilePath)
                        saveStatus.color = "#e06c6c"
                    }
                }
            }
        }
    }

    // 主内容区
    StackLayout {
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: navBar.top

        currentIndex: window.currentView

        Item { ScheduleGrid { anchors.fill: parent } }
        Item { TimeAxis { anchors.fill: parent } }
        Item { SubjectManager { anchors.fill: parent } }
    }

    // 底部导航
    Rectangle {
        id: navBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: "#1e1e1e"

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // 课表
            Rectangle {
                Layout.fillWidth: true
                height: 56
                color: window.currentView === 0 ? "#0078d4" : "transparent"
                MouseArea {
                    anchors.fill: parent
                    onClicked: window.currentView = 0
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("课表")
                    color: window.currentView === 0 ? "#fff" : "#888"
                    font.pixelSize: 14
                }
            }
            // 时间轴
            Rectangle {
                Layout.fillWidth: true
                height: 56
                color: window.currentView === 1 ? "#0078d4" : "transparent"
                MouseArea {
                    anchors.fill: parent
                    onClicked: window.currentView = 1
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("时间轴")
                    color: window.currentView === 1 ? "#fff" : "#888"
                    font.pixelSize: 14
                }
            }
            // 科目
            Rectangle {
                Layout.fillWidth: true
                height: 56
                color: window.currentView === 2 ? "#0078d4" : "transparent"
                MouseArea {
                    anchors.fill: parent
                    onClicked: window.currentView = 2
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("科目")
                    color: window.currentView === 2 ? "#fff" : "#888"
                    font.pixelSize: 14
                }
            }
        }
    }
}
