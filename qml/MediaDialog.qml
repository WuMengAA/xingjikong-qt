// 媒体弹窗：列出教室机媒体库里的文件（media_list），点一行就删（media_delete）。
//
// 为什么把"列"和"删"放一起：老师要处理的就是"这台机器上有哪些教学媒体、哪个用完了要清掉"。
// 分开成两个按钮的话，看一眼列表还得去别处找删除，等于让人记不住。
// 删除走被控端的 media_delete（进回收站），越出媒体库目录的动作会被被控端拒绝 —— 这里如实照显。

import QtQuick
import QtQuick.Controls

Popup {
    id: dlg
    property var theme: null
    property var items: []            // media_list 的 items（name/path/bytes/sizeMB/ext）
    property string mediaDir: ""
    property bool truncated: false

    anchors.centerIn: parent
    width: 640
    height: 420
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
                text: "媒体（" + dlg.items.length + (dlg.truncated ? "，已截断" : "") + "）"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
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

        Item {
            width: parent.width
            height: 28
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "点一行就删（进回收站，可恢复）；删媒体库外的文件会被被控端拒绝"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
        }

        ListView {
            // 必须给个 id：在 ScrollIndicator.vertical 的对象体里写 ListView 会解析成
            // attached 属性对象（QQuickListViewAttached），而不是这个 ListView 实例 ——
            // 于是 `parent: ListView` 报 "Unable to assign QQuickListViewAttached to QQuickItem"。
            id: mediaList
            width: parent.width
            height: 300
            clip: true
            model: dlg.items
            spacing: 2
            ScrollIndicator.vertical: ScrollIndicator {
                parent: mediaList
                anchors.top: mediaList.top
                anchors.bottom: mediaList.bottom
                anchors.right: mediaList.right
            }

            delegate: Rectangle {
                width: ListView.width - 24
                height: 34
                x: 12
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                color: rowHover.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    text: modelData.name
                    color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: (Math.round((modelData.sizeMB || 0) * 100) / 100) + " MB  ·  " + (modelData.ext || "")
                    color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                    font.pixelSize: 11
                }

                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    onClicked: {
                        // 删完立刻重新拉一次列表：让"删掉了"这件事当场看得见，而不是只靠回执区一行字
                        backend.sendAction("media_delete", { "path": modelData.path })
                        dlg.refreshTimer.restart()
                    }
                }
            }

            // 删完后隔一下再刷：QProcess 走完一轮需要点时间，太早拉会看到旧列表（像没删掉）
            Timer {
                id: refreshTimer
                interval: 900
                repeat: false
                onTriggered: backend.sendAction("media_list")
            }
        }

        Item {
            width: parent.width
            height: 34
            Rectangle {
                anchors.fill: parent
                color: "transparent"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                // QML 字符串里反斜杠是转义符，路径一律写双反斜杠，否则解析直接断在这行
                text: dlg.mediaDir ? ("媒体库：" + dlg.mediaDir)
                                   : "媒体库目录由被控端决定（本机默认 %LOCALAPPDATA%\\xingjikong\\media）"
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
                elide: Text.ElideMiddle
                width: parent.width - 36
            }
        }
    }

    // 外部（Main.qml）可以直接调它
    function setItems(arr, dir, trunc) {
        dlg.items = arr || []
        dlg.mediaDir = dir || ""
        dlg.truncated = !!trunc
    }
}
