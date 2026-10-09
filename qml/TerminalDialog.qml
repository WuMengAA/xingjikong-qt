// 远程终端：连教室机的命令行（被控端会先弹窗问本机一句「允许吗」，没人点头就开不了）。
// 云端把敲的每条命令都记进事件流水，界面上不做"审计好看"的事。
//
// ⚠️ 这不是真 PTY，是 REPL：一行回车执行一次、`exit code` 回来就能敲下一行。
// 本机（Win11 Build 26200）没有 ConPTY，真伪控制台这条路在这台机器上没法端到端验证，
// 所以没做"盲写一套跑不了的实现"。交互体验上比 UU 远程终端的 PTY 差一截（没有行编辑、
// 上下翻历史、Tab 补全），但"能敲命令、能看到输出"这条主线是通的。

import QtQuick
import QtQuick.Controls
import "components"

Popup {
    id: dlg
    property var theme: null
    property string sid: ""
    // 输出区按行攒：terminal_data 是"一小段"（被控端 50ms 攒一块），不能直接拼，
    // 拼起来中间没有换行就成了一坨。这里统一按 \n 补齐再 append。
    property string pending: ""

    anchors.centerIn: parent
    width: 760
    height: 520
    modal: true
    padding: 0
    // 关掉就是断会话：esc 关、点遮罩关、程序关，三条路都走 doClose()，
    // 免得"关了窗口但机器那边会话还开着"——那条会话的空闲计时还在跑，白占一个口子。
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme ? dlg.theme.cream : "#141414"
        radius: dlg.theme ? dlg.theme.rCard : 12
        border.color: dlg.theme ? dlg.theme.stroke : "#242424"
        border.width: 1
    }

    contentItem: Column {
        spacing: 0

        // ── 标题栏 ──
        Item {
            width: parent.width
            height: 46
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "远程终端"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                // ⚠️ 这里不能写 right: ... —— right 是 anchors 的只读输出属性，
                // 直接赋值会在加载时报 "Invalid property assignment"，整个 Dialog 类型都建不出来。
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: stateText()
                color: stateColor()
                font.pixelSize: 12
            }
        }

        Rectangle { width: parent.width; height: 1; color: dlg.theme ? dlg.theme.stroke : "#242424" }

        // ── 输出区 ──
        Flickable {
            id: flick
            width: parent.width
            height: 380
            clip: true
            contentWidth: width
            contentHeight: out.implicitHeight
            boundsMovement: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            Text {
                id: out
                x: 14
                width: parent.width - 28
                text: dlg.pending
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                // 等宽：cmd 的输出对齐全靠它，换字体一列就歪
                font.family: "Consolas"
                wrapMode: Text.NoWrap   // 终端不折行，横向滚动比折行更像终端
            }
            MouseArea {
                anchors.fill: parent
                // 点一下就跳到底部：输出是往上刷的，低端用户不会自己找滚动条
                onClicked: flick.contentY = out.implicitHeight - flick.height
            }
        }

        Rectangle { width: parent.width; height: 1; color: dlg.theme ? dlg.theme.stroke : "#242424" }

        // ── 输入行 ──
        // ⚠️ 输入行必须用 InputField：原来这里是裸 QQC `TextField`，它自带系统调色板
        // 的浅灰底（实测截图中是 #D0D0D0 那段白条），暗色外观下就成了"白底灰字"，
        // 看着跟 #94 一个病 —— 暗色下敲命令看不见自己在打什么。
        Rectangle {
            width: parent.width
            height: 42
            color: dlg.theme ? dlg.theme.win : "#1A1A1A"
            InputField {
                id: input
                th: dlg.theme
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                enabled: backend.termState === 2          // TermOpen
                placeholderText: enabled ? "" : "先开终端才能敲"
                font.family: "Consolas"
                onAccepted: {
                    const line = text
                    if (line.trim() === "" || !enabled) { text = ""; return }
                    text = ""
                    // 回显先出：命令是"发出去了"还是"机器跑完了"要分得清，
                    // 回显是发出去那一刻立刻有的，机器回的东西随后到，混在一起就分不清了。
                    append("> " + line)
                    backend.termInput(dlg.sid, line)
                }
            }
        }

        // ── 底部：状态 + 操作 ──
        Item {
            width: parent.width
            height: 52
            Text {
                id: tip
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 160
                text: tipText()
                color: dlg.theme ? dlg.theme.op : "#BBBBBB"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Rectangle {
                id: btn
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: 90; height: 32; radius: dlg.theme ? dlg.theme.rCtrl : 8
                color: backend.termState === 1 ? "transparent"
                                               : (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    // Pending 显示「…」：那会儿按钮按了也不会有别的反应（见下面 MouseArea）
                    text: (backend.termState === 0) ? "开终端" : (backend.termState === 1 ? "…" : "关终端")
                    color: (backend.termState === 1) ? (dlg.theme ? dlg.theme.op : "#BBBBBB")
                                                     : (dlg.theme ? dlg.theme.win : "#141414")
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    // 等回话（Pending）时别让人连点 —— 每点一次就多发一条 terminal_close，
                    // 而状态不会变（setTermState 值相同直接 return），看着就是"按了没反应"。
                    // 等不及就等 20 秒：到点 C++ 自己回 Idle，按钮自动变回「开终端」。
                    enabled: backend.termState !== 1
                    onClicked: {
                        if (backend.termState === 0) doOpen()
                        else doClose()
                    }
                }
            }
        }
    }

    // ── 与 C++ 侧的连接 ────────────────────────────────────────────
    Connections {
        target: backend
        function onTerminalOpened(sid, shell, prompt, cwd) {
            dlg.sid = sid
            dlg.pending = (prompt || "") + "\r\n"
            scrollToEnd()
            input.forceActiveFocus()
        }
        function onTerminalData(sid, data) {
            if (sid !== dlg.sid) return
            append(data)
        }
        function onTerminalExit(sid, code, ms) {
            if (sid !== dlg.sid) return
            // 命令跑完留一行空行，下一行回显从干净的地方开始
            append("\r\n[exit " + code + " · " + ms + "ms]\r\n")
            scrollToEnd()
        }
        function onTerminalClosed(sid, reason) {
            if (sid && sid !== dlg.sid) return
            append("\r\n[会话结束：" + (reason || "未知") + "]\r\n")
            input.text = ""
            scrollToEnd()
        }
    }

    onClosed: doClose()

    function append(s) {
        const t = dlg.pending + s
        // 攒太多会拖慢渲染，也让人看不回头顶：留最后 5000 行，从头砍
        const lines = t.split("\n")
        if (lines.length > 5000) dlg.pending = lines.slice(lines.length - 5000).join("\n")
        else dlg.pending = t
        scrollToEnd()
    }

    function scrollToEnd() {
        flick.contentY = Math.max(0, out.implicitHeight - flick.height)
    }

    function stateText() {
        if (backend.termState === 1) return "等本机确认…"
        if (backend.termState === 2) return "已连接"
        return "未连接"
    }

    function stateColor() {
        if (backend.termState === 2) return dlg.theme ? dlg.theme.op : "#BBBBBB"
        if (backend.termState === 1) return dlg.theme ? dlg.theme.fg : "#FAFAFA"
        return dlg.theme ? dlg.theme.op : "#BBBBBB"
    }

    function tipText() {
        // Pending 时直接把 C++ 那句原话顶上去（"等本机点头…" / "正在关…"），
        // 超时复位后那边会换成"…超时（本机没允许…）"，一并让用户看见 ——
        // 否则按钮自己变回「开终端」，人不知道刚才为什么没连上。
        if (backend.termState === 1) return backend.termNote || "机器在问本机「允许吗」，等着就行"
        if (backend.termState === 2)
            return "回车执行一条，30 秒不回自动断；敲 exit 或点关终端"
        // Idle：只有"上次没成"的时候才带原因（刚被拒绝 / 刚超时 / 刚被本机拒绝），
        // 没原因（第一次打开）就给开场的那句。
        return backend.termNote !== "" ? ("上次：" + backend.termNote)
                                       : "开一条命令行（本机要手动允许）"
    }

    function doOpen() {
        dlg.pending = ""
        dlg.sid = ""
        backend.termOpen("cmd")
    }

    function doClose() {
        if (backend.termState !== 0) backend.termClose()
    }
}
