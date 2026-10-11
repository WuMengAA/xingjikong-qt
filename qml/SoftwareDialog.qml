// 软件弹窗：列出教室机上"有主窗口"的进程；选中一行可结束它。
// 受保护的（系统关键 / 安全软件 / 集控自身）由被控端拒绝，这里先置灰标出来。

import QtQuick
import QtQuick.Controls
import "components"       // 2026-10-10：SvgIcon 等组件统一显式 import（Qt6.12 严格解析）

Popup {
    id: dlg
    property var theme: null
    property var items: []          // 正在运行（来自 process_list）
    property var candidates: []     // 可打开（来自 list_shortcut_candidates：有窗口的程序 + 桌面条目）
    property bool truncated: false

    // 弹窗内的短提示（2026-10-11）：一键全关这种批量动作必须给人一个回音，
    // 否则点了不知道是没匹配上还是没发出去。这里不跨页调 root.toast()——
    // 本组件不认识 root，自己显示一行更可靠。
    property string tipText: ""
    function hint(msg) { dlg.tipText = msg; tipTimer.restart() }
    Timer { id: tipTimer; interval: 2600; repeat: false; onTriggered: dlg.tipText = "" }

    anchors.centerIn: parent
    width: 720
    // 2026-10-11：加了下面那条「黑名单」区（原 480 已排满，加高才放得下）
    height: 566
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
            SvgIcon {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                name: "x"
                tint: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                width: 14
                height: 14
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
                        // 进程图标（2026-10-11 需求#6）：被控端从窗口句柄取图标、
                        // 转 16×16 PNG 的 base64 带回来。取不到（服务进程 / 无窗口）
                        // 就留一个同名首字的方块占位 —— 至少每行左边是齐的，
                        // 不会"有的有图标有的没有"看起来像渲染坏了。
                        Item {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            Rectangle {
                                anchors.fill: parent
                                radius: 3
                                color: dlg.theme ? dlg.theme.line : "#161616"
                                visible: !(modelData.icon || "")
                                Text {
                                    anchors.centerIn: parent
                                    text: (modelData.name || "?").charAt(0).toUpperCase()
                                    color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                                    font.pixelSize: 9
                                }
                            }
                            Image {
                                anchors.fill: parent
                                visible: !!(modelData.icon || "")
                                source: (modelData.icon || "")
                                         ? ("data:image/png;base64," + modelData.icon) : ""
                                fillMode: Image.PreserveAspectFit
                                mipmap: true
                            }
                        }
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

        // ── 黑名单（2026-10-11 需求#6：「保存 → 指定软件一键关闭」）──
        // 老师真正重复做的事不是"关这一个进程"，而是"每节课都要关掉那几个"。
        // 所以把"上课不许开的软件"存成一份名单（落在管理端本机，换机器也跟着走），
        // 以后点一次就全部关掉。名单只对**当前正在运行**的进程生效：没开的不用管。
        Item {
            width: parent.width
            height: 86

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }

            Text {
                y: 10
                x: 18
                text: "黑名单（" + backend.procBlacklist.length + "）· 存下来，以后一键全关"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }

            // 名单横条：空的时候给一句人话，不要留一片空白让人以为没这个功能
            Flickable {
                y: 30
                x: 18
                width: parent.width - 210
                height: 24
                contentWidth: blRow.implicitWidth
                clip: true
                Row {
                    id: blRow
                    spacing: 4
                    Repeater {
                        model: backend.procBlacklist
                        delegate: Rectangle {
                            height: 22
                            width: blLbl.implicitWidth + 26
                            radius: 6
                            color: dlg.theme ? dlg.theme.line : "#161616"
                            Text {
                                id: blLbl
                                anchors.centerIn: parent
                                x: 8
                                text: modelData
                                color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                                font.pixelSize: 11
                            }
                            // 右侧一个小 × 用来移除（不设 icon 字体，画两笔斜线最省事）
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                width: 12; height: 12
                                color: "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "×"
                                    color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    onClicked: backend.removeProcFromBlacklist(modelData)
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: backend.procBlacklist.length === 0
                    text: "（空）—— 在「正在运行」里选一行，点右边「加入黑名单」"
                    color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                    font.pixelSize: 11
                }
            }

            // 加入黑名单（拿当前选中的那一行）
            Rectangle {
                y: 30
                anchors.right: parent.right
                anchors.rightMargin: 100
                width: 92; height: 24
                radius: 6
                color: "transparent"
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "加入黑名单"
                    color: listView.picked >= 0 ? (dlg.theme ? dlg.theme.fg : "#FAFAFA")
                                                : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                    font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: listView.picked >= 0
                    onClicked: {
                        var it = dlg.items[listView.picked]
                        if (it) backend.addProcToBlacklist(it.name)
                    }
                }
            }

            Text {
                y: 58
                x: 18
                width: parent.width - 36
                text: dlg.tipText
                visible: dlg.tipText !== ""
                color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            // 一键关闭：命中名单且此刻正在运行的，逐个发 process_stop
            Rectangle {
                y: 30
                anchors.right: parent.right
                anchors.rightMargin: 8
                width: 84; height: 24
                radius: 6
                color: backend.procBlacklist.length > 0 ? (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                                                        : (dlg.theme ? dlg.theme.line : "#161616")
                Text {
                    anchors.centerIn: parent
                    text: "一键全关"
                    color: backend.procBlacklist.length > 0 ? (dlg.theme ? dlg.theme.win : "#0A0A0A")
                                                            : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: backend.procBlacklist.length > 0
                    onClicked: {
                        var n = backend.killBlacklisted(dlg.items)
                        if (n < 0) dlg.hint("还没选设备，不知道要关哪台机器上的")
                        else if (n === 0) dlg.hint("名单里的软件这台机器现在都没开着")
                        else dlg.hint("已下发 " + n + " 个关闭指令")
                    }
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
