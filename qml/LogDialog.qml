// 日志弹窗：教室机被控端日志的尾部（装机后没有 stdout，出故障只能靠它）。

import QtQuick
import QtQuick.Controls

Popup {
    id: dlg
    property var theme: null
    property var lines: []
    property string logPath: ""

    anchors.centerIn: parent
    width: 820
    height: 500
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme ? dlg.theme.cream : "#141414"
        radius: dlg.theme ? dlg.theme.rCard : 12
        border.color: dlg.theme ? dlg.theme.stroke : "#242424"
        border.width: 1
    }

    contentItem: Column {
        spacing: 0

        Item {
            width: parent.width
            height: 46

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "日志"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 56
                text: "末 " + dlg.lines.length + " 行"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: "✕"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 13
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -10
                    onClicked: dlg.close()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
        }

        ScrollView {
            width: parent.width
            height: 396
            clip: true

            TextArea {
                id: logArea
                readOnly: true
                wrapMode: TextArea.NoWrap
                text: dlg.lines.join("\n")
                color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                font.family: "Consolas"
                font.pixelSize: 11
                padding: 14
                background: Rectangle { color: "transparent" }

                // 打开时滚到最底（看的就是最新的那几行）
                onTextChanged: if (activeFocus || true) cursorPosition = length
            }
        }

        Item {
            width: parent.width
            height: 58

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: closeBtn.left
                anchors.rightMargin: 12
                text: dlg.logPath
                elide: Text.ElideMiddle
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }

            Rectangle {
                id: closeBtn
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: 90
                height: 34
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                color: "transparent"
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "关闭"
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dlg.close()
                }
            }
        }
    }
}
