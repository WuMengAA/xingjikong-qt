// 星集控 · 命令面板（Ctrl + K，2026-10-06 补）
//
// 设计依据《星集控 — 全量设计文档（整理合并版）》：
//   3.5.1 命令面板：模糊搜索所有操作（类 VS Code），老师不用记菜单位置；
//   4.7    快捷键表：Ctrl+K 命令面板、F5 刷新、Ctrl+1/2/3 切视图、Ctrl+F 聚焦搜索。
//
// 本组件只做三件事：**收命令、搜、显示**。命令本身由主界面塞进来（pal.cmds =
// [{ name, keys, tip, run }]）—— 命令要调 runAction / openOnly / page，那些都在主界面手里，
// 组件不去猜。主界面给的是 ctx（root），exec 时传给 run，命令里就能 root.xxx。
//
// 视觉沿用黑白稿：唯一强调手段＝反白（选中行反白），不引入彩色。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: pal

    property var theme: ({ })          // 主题令牌，由主界面给
    property var ctx: null          // 主窗口（runAction / openOnly / page 都在它身上）
    property var cmds: ([])         // 全部命令：[{ name, keys, tip, run }]
    property var hits: ([])         // 当前搜索结果
    property int cur: 0             // 光标所在行
    property real overlayW: Overlay.overlay ? Overlay.overlay.width : 1180

    // ⚠️ 弹窗不是"越大越好"：教室机常见 1366×768，面板超过 420 宽就压住画面区了。
    width: 420
    height: input.height + 1 + Math.min(hits.length, 9) * 30 + 26 + 18
    x: Math.max(8, (pal.overlayW - pal.width) / 2)
    y: 52                            // 顶条 44 + 8，浮在顶条下面（不挡账户菜单）
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: pal.theme.panel || "#101010"
        radius: pal.theme.rCard || 12
        border.color: pal.theme.stroke || "#242424"
        border.width: 1
    }

    // ── 打开即清空输入、回到第一条 ──────────────────────────────────
    onOpened: {
        input.text = ""
        pal.refilter("")
        cur = 0
        input.forceActiveFocus()
    }

    function refilter(q) {
        const s = (q || "").trim().toLowerCase()
        if (s === "") { pal.hits = pal.cmds.slice(); return }
        // 三档匹配：名称前缀 > 名称含 > 别名含（别名列在后面，拼音首字母才找得到）
        const hitName = [], hitAlt = []
        for (let i = 0; i < pal.cmds.length; ++i) {
            const c = pal.cmds[i]
            const nm = (c.name || "").toLowerCase()
            let score = -1
            if (nm.indexOf(s) === 0) score = 0
            else if (nm.indexOf(s) > 0) score = 1
            else {
                const ks = c.keys || []
                for (let k = 0; k < ks.length; ++k) {
                    if (ks[k].toLowerCase().indexOf(s) >= 0) { score = 2; break }
                }
            }
            if (score >= 0) (score === 2 ? hitAlt : hitName).push(c)
        }
        pal.hits = hitName.concat(hitAlt)
    }

    function exec(idx) {
        const c = pal.hits[idx]
        if (!c) return
        pal.close()               // 先收面板再执行：命令里可能开别的弹窗，叠着就串味了
        if (c.run) c.run(pal.ctx)
    }

    contentItem: Column {
        spacing: 0

        Row {
            width: parent.width
            height: 34
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                text: "命令"
                color: pal.theme.fg3 || "#8A8A8A"
                font.pixelSize: 12
            }
            Rectangle { width: 1; height: 16; color: pal.theme.sepline; anchors.verticalCenter: parent.verticalCenter }
            // ⚠️ 输入框不要画底色：面板本身已经是 panel 底，再深一层就是"两层灰叠一起"
            InputField {
                th: pal.theme
                id: input
                height: 34
                width: parent.width - 96
                leftPadding: 10
                rightPadding: 8
                verticalAlignment: Text.AlignVCenter
                placeholderText: "搜命令（打首字母也行：gj → 关机教室机）"
                font.pixelSize: 13

                onTextChanged: { pal.refilter(text); pal.cur = 0 }

                Keys.onPressed: function (ev) {
                    if (ev.key === Qt.Key_Up) {
                        pal.cur = pal.cur > 0 ? pal.cur - 1 : pal.hits.length - 1
                        list.currentIndex = pal.cur
                        ev.accepted = true
                    } else if (ev.key === Qt.Key_Down) {
                        pal.cur = pal.cur < pal.hits.length - 1 ? pal.cur + 1 : 0
                        list.currentIndex = pal.cur
                        ev.accepted = true
                    } else if (ev.key === Qt.Key_Return || ev.key === Qt.Key_Enter) {
                        pal.exec(pal.cur)
                        ev.accepted = true
                    } else if (ev.key === Qt.Key_Escape) {
                        pal.close()
                        ev.accepted = true
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: pal.theme.sepline }

        ListView {
            id: list
            width: parent.width
            height: Math.min(pal.hits.length, 9) * 30
            clip: true
            model: pal.hits
            currentIndex: pal.cur
            highlightMoveDuration: 0

            delegate: Item {
                width: list.width
                height: 30

                Rectangle {
                    anchors.fill: parent
                    color: (index === pal.cur) ? (pal.theme.inv || "#F0F0F0") : "transparent"
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10
                    Rectangle {
                        width: 12; height: 12
                        radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: (index === pal.cur) ? (pal.theme.win || "#0A0A0A") : (pal.theme.fg4 || "#707070")
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: (index === pal.cur) ? (pal.theme.win || "#0A0A0A") : (pal.theme.fg || "#FAFAFA")
                        font.pixelSize: 13
                    }
                    Item { width: 8 }
                    // 别名摆在右边：既能提示"还能这么搜"，又不抢名字的位置
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        text: (modelData.keys && modelData.keys.length) ? modelData.keys[0] : ""
                        color: (index === pal.cur) ? (pal.theme.win || "#0A0A0A") : (pal.theme.fg4 || "#707070")
                        font.pixelSize: 12
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: { pal.cur = index; pal.exec(index) }
                    onEntered: pal.cur = index
                }
            }
        }

        // 底部一行：光标落点 + 空结果时的说法（不留白）
        Row {
            width: parent.width
            height: 26
            anchors.leftMargin: 12
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pal.hits.length === 0 ? "没有匹配的命令" : (pal.hits[pal.cur] ? (pal.hits[pal.cur].tip || "") : "")
                color: pal.theme.fg3 || "#8A8A8A"
                font.pixelSize: 12
                width: parent.width - 24
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }
        }
    }
}
