// 文件弹窗：把本机一个文件推给这台教室机（file_push → file_chunk → file_done）。
//
// 为什么单独一个弹窗、不塞进媒体弹窗：方向是反的 —— 媒体是"教室机上的东西删掉"，
// 这里是"从我这台灌进教室机"。混在一个列表里，老师会分不清哪个是自己的文件。
//
// 分块推送的节奏在 backend（C++）里：一片一片等回执再发下一片。这里只负责
// 选文件 / 显示进度 / 把失败原因照原样念出来，不自己攒状态。

import QtQuick
import QtQuick.Controls

Popup {
    id: dlg
    property var theme: null
    property string pendingUrl: ""        // 选完还没点"开始推送"的文件（file:// URL）
    // 只把文件名挑出来显示：file:///D:/.../a.txt 整条塞进去，界面上只剩中点省略
    readonly property string pendingName: {
        if (!dlg.pendingUrl) return "";
        var s = String(dlg.pendingUrl);
        var i = s.lastIndexOf("/");
        return i >= 0 ? s.slice(i + 1) : s;
    }

    anchors.centerIn: parent
    width: 560
    height: 300
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
                text: "文件"
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
            height: 30
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 36
                text: "选一个本机文件推给这台教室机。被控端会落进它自己的收件目录，"
                      + "文件名不许带路径、必须有扩展名 —— 这两条它自己会拒，这里照它说的报"
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }

        Item {
            width: parent.width
            height: 62

            // 选文件
            Rectangle {
                id: pickBtn
                x: 18; y: 12
                width: 92; height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                color: pickArea.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "选文件"
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                }
                // 选文件走 backend.pickFile()（C++ 原生 QFileDialog）。
                // 不 import QtQuick.Dialogs：那套 QML 插件没随 exe 部署，真机上会直接把
                // Main.qml 整个拖挂（QML 里一个 import 不到，不是"少个按钮"，是起不来）。
                MouseArea { id: pickArea; anchors.fill: parent; onClicked: dlg.pendingUrl = backend.pickFile() }
            }

            // 选完的文件名 + 大小
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 124
                width: parent.width - 142
                text: dlg.pendingUrl ? ("已选：" + dlg.pendingName) : "（还没选文件）"
                color: dlg.pendingUrl ? (dlg.theme ? dlg.theme.op : "#C8C8C8")
                                      : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }

        Item {
            width: parent.width
            height: 34
            // 进度条：backend.fileState 是 sending 才动，其它状态留着当信息位
            Rectangle {
                x: 18; y: 6
                width: parent.width - 108
                height: 6
                radius: 3
                color: dlg.theme ? dlg.theme.line : "#161616"
                Rectangle {
                    // 子项里 parent 就是这条底槽，直接按它的宽度算，别往上摸两层（折叠一次就断）
                    width: parent.width * (backend.filePercent / 100)
                    height: 6
                    radius: 3
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: backend.filePercent + "%"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
        }

        Item {
            width: parent.width
            height: 40

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 190
                // 状态/原因一律由 backend 出：这里不自己拼一句"可能失败了"这种没信息的话
                text: {
                    if (backend.fileState === "sending")
                        return "正在推：" + backend.fileBytes + " / " + backend.fileTotal + " 字节";
                    if (backend.fileState === "failed")
                        return "✕ 推失败：" + backend.fileError;
                    if (backend.fileState === "done")
                        return "✅ 已推到：" + backend.fileTarget;
                    if (backend.fileError === "已取消")
                        return "已取消";
                    return "选好文件后点「开始推送」";
                }
                // 失败态不再用红色（#C0392B）：黑白稿集里"强调"只有反白+描边一种手段，
                // 这里改成反白字加粗、前缀 ✕，深色下是白字压深底，浅色下是黑字压白底。
                color: (backend.fileState === "failed") ? (dlg.theme ? dlg.theme.fg : "#FAFAFA")
                                                        : (dlg.theme ? dlg.theme.fg3 : "#5A5A5A")
                font.pixelSize: 12
                font.bold: backend.fileState === "failed"
                elide: Text.ElideMiddle
            }

            Rectangle {
                id: goBtn
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: 76; height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                color: goArea.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "开始推送"
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                }
                MouseArea {
                    id: goArea
                    anchors.fill: parent
                    // 推的过程中按钮锁住：再点只会多开一个会话，两个会话互相抢 seq
                    enabled: dlg.pendingUrl !== "" && backend.fileState !== "sending"
                             && backend.fileState !== "pushing"
                    // 推的过程中**不关**这个弹窗：进度条和"已推到：x"就是推送的唯一凭据，
                    // 一关就只剩日志里那两行，老师看见的是"点了没反应"
                    onClicked: backend.pushFile(dlg.pendingUrl)
                }
            }

            Rectangle {
                id: cancelBtn
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: goBtn.left
                anchors.rightMargin: 8
                width: 56; height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 9
                color: cancelArea.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent"
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                    font.pixelSize: 12
                }
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    onClicked: { backend.cancelPush(); dlg.close() }
                }
            }
        }
    }

}
