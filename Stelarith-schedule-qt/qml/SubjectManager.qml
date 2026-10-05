import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 科目管理页：列表 + 添加/删除/改名
Rectangle {
    id: root
    color: "#1e1e1e"

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
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            Label {
                text: qsTr("科目管理（%1 个）").arg(subjectModel.count())
                color: "#ccc"
                font.pixelSize: 14
                Layout.fillWidth: true
            }

            TextField {
                id: newNameInput
                Layout.preferredWidth: 160
                placeholderText: qsTr("新科目名")
                color: "#fff"
                selectByMouse: true
                onAccepted: addBtn.clicked()
            }

            Button {
                id: addBtn
                text: qsTr("添加")
                onClicked: {
                    const name = newNameInput.text.trim()
                    if (name === "") return
                    subjectModel.addSubjectQml(name)
                    newNameInput.text = ""
                }
            }
        }
    }

    // 科目列表
    ListView {
        id: list
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8

        model: subjectModel
        clip: true
        spacing: 4

        delegate: Rectangle {
            required property string id
            required property string name
            required property string simplifiedName
            required property string teacher

            width: list.width
            height: 44
            radius: 4
            color: "#252526"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                // 简称徽标
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 16
                    color: "#0078d4"
                    Text {
                        anchors.centerIn: parent
                        text: model.simplifiedName !== "" ? model.simplifiedName : "?"
                        color: "#fff"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }

                Label {
                    text: model.name
                    color: "#fff"
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label {
                    text: model.teacher !== "" ? model.teacher : "—"
                    color: "#888"
                    font.pixelSize: 12
                }

                // 改名
                Button {
                    text: qsTr("改名")
                    onClicked: renameDialog.openFor(index)
                }

                // 删除
                Button {
                    text: qsTr("删除")
                    onClicked: {
                        if (subjectModel.removeAt(index)) {
                            // 成功
                        } else {
                            statusMsg.text = qsTr("「%1」被课表引用，无法删除").arg(model.name)
                            statusTimer.restart()
                        }
                    }
                }
            }
        }

        // 空态
        Item {
            anchors.fill: parent
            visible: list.count === 0
            Text {
                anchors.centerIn: parent
                text: qsTr("还没有科目，在顶部输入名称添加")
                color: "#666"
                font.pixelSize: 14
            }
        }
    }

    // 状态提示
    Text {
        id: statusMsg
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        color: "#e0a03a"
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        visible: text !== ""
    }
    Timer {
        id: statusTimer
        interval: 3000
        onTriggered: statusMsg.text = ""
    }

    // 改名对话框
    Dialog {
        id: renameDialog
        title: qsTr("科目改名")
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        standardButtons: Dialog.Ok | Dialog.Cancel

        property int row: -1

        function openFor(r) {
            row = r
            renameInput.text = subjectModel.nameAt(r)
            open()
        }

        onAccepted: {
            if (subjectModel.renameAt(row, renameInput.text)) {
                // ok
            } else {
                statusMsg.text = qsTr("改名失败：名称不能为空")
                statusTimer.restart()
            }
        }

        contentItem: TextField {
            id: renameInput
            placeholderText: qsTr("科目名")
        }
    }
}
