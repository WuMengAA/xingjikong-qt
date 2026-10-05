// 软件弹窗：列出教室机上"有主窗口"的进程；选中一行可结束它。
// 受保护的（系统关键 / 安全软件 / 集控自身）由被控端拒绝，这里先置灰标出来。

import QtQuick
import QtQuick.Controls

Popup {
    id: dlg
    property var theme: null
    property var items: []          // 正在运行（来自 process_list）
    property var candidates: []     // 可打开（来自 list_shortcut_candidates：有窗口的程序 + 桌面条目）
    property bool truncated: false

    anchors.centerIn: parent
    width: 720
    height: 480
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
                text: "软件"
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

        // ── 可打开：**选自这台机器**（被控端返回的候选：有窗口的程序 + 桌面上的文件与文件夹）──
        // 按用户口径：快捷方式来自被控端，不是预置名单。
        Item {
            width: parent.width
            height: 30
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "可打开 · 选自这台机器（" + dlg.candidates.length + "）"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
        }

        ListView {
            id: candList
            width: parent.width
            height: 158
            clip: true
            model: dlg.candidates
            spacing: 2
            topMargin: 4

            delegate: Rectangle {
                width: candList.width - 20
                x: 10
                height: 30
                radius: 8
                color: candHover.containsMouse ? (dlg.theme ? dlg.theme.cream : "#141414") : "transparent"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30
                        text: modelData.kind === "app" ? "程序" : (modelData.kind === "dir" ? "目录" : "文件")
                        color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                        font.pixelSize: 11
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: candList.width - 96
                        text: modelData.title || modelData.target
                        color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: candHover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: backend.sendAction("launch_app", { "target": modelData.target })
                }
            }

            Text {
                anchors.centerIn: parent
                visible: dlg.candidates.length === 0
                text: "还没拿到候选（点「软件」时会向这台机器要）"
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
            }
        }

        Item {
            width: parent.width
            height: 30
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "正在运行（" + dlg.items.length + (dlg.truncated ? " · 已截断" : "") + "）"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
        }

        ListView {
            id: listView
            width: parent.width
            height: 158
            clip: true
            focus: false          // 别自动拿焦点：不然弹窗一开就有一行被"选中"，看着像已经选了它
            model: dlg.items
            spacing: 3
            topMargin: 8
            bottomMargin: 8
            currentIndex: -1
            // 用户**真点过哪一行**才算选中（-1 = 还没点）。
            // 不拿 currentIndex 当选中依据：ListView 拿到焦点会自动把第一行设成 current，
            // 那样弹窗一开「结束选中进程」就是可点的 —— 关别人机器上的程序不能这么随便。
            property int picked: -1

            delegate: Rectangle {
                width: listView.width - 20
                x: 10
                height: 48
                radius: 9
                property bool prot: modelData.protected !== undefined && modelData.protected !== null
                property bool sel: listView.picked === index
                // 底色恒定透明：选中**不再靠"整块底色 + 边框"**表达。
                // 原因：`color: sel ? 底色 : "transparent"` 这种写法，绑定一旦异常，
                // Rectangle 会静默回落到默认白底 —— 看着像"每一行都被选中"，
                // 用户就不知道「结束选中进程」会关哪一个。改成竖条标记，不受绑定异常影响。
                color: "transparent"
                opacity: prot ? 0.4 : 1.0

                Rectangle {
                    visible: sel
                    x: 3
                    width: 2
                    height: 22
                    radius: 1
                    color: dlg.theme ? dlg.theme.inv : "#F0F0F0"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    spacing: 3

                    Row {
                        spacing: 8
                        Text {
                            text: modelData.name
                            color: sel ? (dlg.theme ? dlg.theme.fg : "#FAFAFA")
                                       : (dlg.theme ? dlg.theme.op : "#C8C8C8")
                            font.pixelSize: 13
                            font.weight: sel ? Font.Medium : Font.Normal
                        }
                        Text {
                            text: modelData.pid + " · " + modelData.mem_mb + " MB"
                            color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                            font.pixelSize: 11
                        }
                        Rectangle {
                            visible: prot
                            width: 62
                            height: 16
                            radius: 5
                            color: "transparent"
                            border.color: "#2A2A2A"
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                // 必须兜底：process_list 回来的进程不一定带 protected 字段，
                                // 而外层 Rectangle 只是"不可见"（visible: prot），绑定照样求值 ——
                                // 直接赋 undefined 会刷屏 "Unable to assign [undefined] to QString"。
                                // （2026-10-03 自检抓图时在日志里抓到 10 条这个告警）
                                text: modelData.protected || ""
                                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                                font.pixelSize: 11
                            }
                        }
                    }

                    Text {
                        text: modelData.title || ""
                        visible: text !== ""
                        color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                        font.pixelSize: 11
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: listView.picked = index
                }
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

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: 220
                height: 34
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                color: listView.picked >= 0 ? (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                                            : (dlg.theme ? dlg.theme.cream : "#141414")

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    // 把要关的进程名写进按钮 —— 用户不必回头去对是哪一行
                    text: listView.picked >= 0
                          ? ("结束 " + (dlg.items[listView.picked] ? dlg.items[listView.picked].name : ""))
                          : "先选一行再结束"
                    color: listView.picked >= 0 ? (dlg.theme ? dlg.theme.win : "#0A0A0A")
                                                : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                    font.pixelSize: 13
                    font.weight: listView.picked >= 0 ? Font.Medium : Font.Normal
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: listView.picked >= 0
                    onClicked: {
                        var it = dlg.items[listView.picked]
                        backend.sendAction("process_stop", { "pid": it.pid })
                        dlg.close()
                    }
                }
            }

            Rectangle {
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
                    color: dlg.theme ? dlg.theme.op : "#C8C8C8"
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
