// 诊断与反馈弹窗（2026-10-12）
//
// 为什么有它：管理端 WIN32_EXECUTABLE 改 ON（双击不再弹黑窗），stdout 随之不可见；
// 出错时老师手上只剩界面。这个弹窗把「本机日志尾部 + 诊断摘要」摊出来，并给一键
// 「复制反馈内容」——把 问题描述 + 诊断摘要 + 日志尾部 一次性拷进剪贴板，老师贴给技术即可。
//
// 反馈只靠剪贴板，**不依赖任何服务端接口**（当前没有反馈接口；要接云端再加一节）。
// 入口有两处：托盘菜单「诊断与反馈…」（main.cpp）+ 设置页「关于」卡按钮（Main.qml）。
//
// 数据/动作全部来自 ViewerBackend：viewerLogTail / diagnosticsSummary /
// copyToClipboard / openLogFolder。本文件只负责显示与转发。

import QtQuick
import QtQuick.Controls
import "components"
import "island"          // SvgIcon 的唯一真源在共享岛目录（Stelarith-shared-qml/island/，构建期拷到 qml/island/）——
                      // 2026-10-16 修：A 档把 viewer 自己的 components/SvgIcon.qml 当"重复件"删了，
                      // 但那 4 个同名件里只有它的接口是超集且被**全套面板 + Main.qml**在用；
                      // 另 3 个（IslandClock/MarqueeText/WhaleMascot）确实只被岛用，删对了。

Popup {
    id: dlg
    property var theme: null
    property string logText: ""

    /** 重新读一次本机日志尾部（打开时、点「刷新」时调）。 */
    function refreshLog() {
        dlg.logText = backend ? backend.viewerLogTail(300) : ""
    }

    anchors.centerIn: parent
    width: 820
    height: 560
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: { tabs.currentIndex = 0; dlg.refreshLog() }

    background: Rectangle {
        color: dlg.theme ? (dlg.theme.cream || "#141414") : "#141414"
        radius: dlg.theme ? (dlg.theme.rCard || 12) : 12
        border.color: dlg.theme ? (dlg.theme.stroke || "#242424") : "#242424"
        border.width: 0
    }

    contentItem: Column {
        spacing: 0

        // ── 标题行 ──
        Item {
            width: parent.width
            height: 46

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "诊断与反馈"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 96
                text: "出错时把下面内容复制给技术"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
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
                color: dlg.theme ? (dlg.theme.line || "#161616") : "#161616"
            }
        }

        // ── 分段：日志 / 诊断 ──
        Item {
            width: parent.width
            height: 40
            StTabBar {
                id: tabs
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                theme: dlg.theme
                model: ["日志", "诊断"]
                currentIndex: 0
            }
        }

        // ── 内容（只读，等宽）──
        ScrollView {
            width: parent.width
            height: 336
            clip: true
            TextArea {
                readOnly: true
                wrapMode: TextArea.NoWrap
                text: tabs.currentIndex === 0
                      ? dlg.logText
                      : (backend ? backend.diagnosticsSummary() : "")
                color: dlg.theme ? (dlg.theme.op || "#C8C8C8") : "#C8C8C8"
                font.family: "Consolas"
                font.pixelSize: 11
                padding: 14
                background: Rectangle { color: "transparent" }
            }
        }

        // ── 问题描述（可选）──
        Item {
            width: parent.width
            height: 46
            InputField {
                id: descField
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                th: dlg.theme
                placeholderText: "一句话描述遇到的问题（可选）——会一并放进「复制反馈内容」"
            }
        }

        // ── 底部动作 ──
        Item {
            width: parent.width
            height: 62
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: dlg.theme ? (dlg.theme.line || "#161616") : "#161616"
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 18
                spacing: 8
                Btn {
                    theme: dlg.theme
                    width: 120
                    text: "复制反馈内容"
                    strong: true
                    tip: "把「问题描述 + 诊断信息 + 日志尾部」一起复制到剪贴板"
                    onClicked: {
                        var desc = descField.text.trim()
                        var blob = (desc !== "" ? ("【问题描述】\n" + desc + "\n\n") : "")
                                 + "【诊断信息】\n" + (backend ? backend.diagnosticsSummary() : "") + "\n\n"
                                 + "【日志尾部】\n" + dlg.logText
                        if (backend) backend.copyToClipboard(blob)
                    }
                }
                Btn {
                    theme: dlg.theme
                    width: 76
                    text: "刷新"
                    onClicked: dlg.refreshLog()
                }
                Btn {
                    theme: dlg.theme
                    width: 120
                    text: "打开日志目录"
                    onClicked: if (backend) backend.openLogFolder()
                }
            }
            Btn {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                theme: dlg.theme
                width: 90
                text: "关闭"
                onClicked: dlg.close()
            }
        }
    }
}
