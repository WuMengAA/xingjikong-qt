// 通知弹窗（2026-10-06 加）：把管理端「通知」按钮做出来。
//
// 为什么是"接线"而不是"新功能"：被控端的大屏通知**早就实现了**
// （control-qt 的 NotifyWindow + executeAction("notify")，乙阶段 2），站点侧也一直在发
// （website 的 broadcast.ts）。**只有管理端这一个发送方漏了** —— 按钮一直是 t:true 置灰。
// 所以这里只做"按已有契约把参数凑齐、发出去"，一个字段都不新造。
//
// 契约来源（照抄，别改）：control-qt `src/main.cpp` 的 notifyFromParams()
//   action = "notify"
//   params = {
//     kind:    "popup" | "island" | "fullscreen"   // 缺省/未知 → 一律 popup（安全默认）
//     title:   string                              // **必填**，空 → 被控端回 failed "notify 缺 title"
//     content: string
//     seconds: int                                 // >0 = 到点自动收；0/缺省 = 按字数自适应
//     flags: {
//       speech:            bool                    // TTS 朗读（缺省不读，避免每个教室都响）
//       severity:          "remind"|"inform"|"urgent"  // **仅 fullscreen 生效**，未知回落黑底
//       emergency_confirm: bool                    // 紧急：不自动关 + 底部「确认」按钮
//     }
//   }
//
// 三件必须在界面上说清楚的事（都是被控端真实行为，不是猜的）：
//   ① 标题超 24 字、正文超 64 字会被**截断加省略号**（TTS 也读同一份截断文本）。
//      不说的话老师会以为整段都发过去了。所以下面直接给字数计数，口径与被控端一致。
//   ② severity 只对全屏生效 —— 弹窗/灵动岛本来就深色半透明，换配色没意义。
//   ③ 紧急锁定 = **不会自动消失**，要学生在屏幕上点「确认」才关。

// ⚠️ 必须有这一行：下面 Tick / Seg / ToggleRow 是**内联组件**（component X: ...），
// Qt 6.5 起内联组件默认是 Unbound —— 它不捕获本文件的上下文，于是在组件内部引用外层
// 的 `dlg.theme` 会解析不到（qmllint 的原话就是"Set pragma ComponentBehavior: Bound in
// order to use IDs from outer components in nested components"）。
// 加上 Bound 之后外层 id 才可用；代价是这些组件不能再被外文件复用，正好也不需要。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Popup {
    id: dlg
    property var theme: null

    // 与被控端 NotifyWindow 的 kTitleCap / kContentCap 保持一致。
    // 改这里必须同步改那边，否则计数显示的和实际截断的对不上。
    readonly property int capTitle: 24
    readonly property int capContent: 64

    // 当前选择
    property string kind: "popup"        // popup | island | fullscreen
    property string severity: "remind"   // remind | inform | urgent
    property bool speech: false
    property bool emergency: false
    property string error: ""

    readonly property bool canSend: backend.currentUid !== ""
                                    && titleInput.text.trim() !== ""
    readonly property bool titleOver: titleInput.text.length > capTitle
    readonly property bool contentOver: bodyInput.text.length > capContent

    anchors.centerIn: parent
    width: 560
    // 形态选了「全屏」才多出「配色」那一行，所以高度跟着走，不留空白也不挤
    height: dlg.kind === "fullscreen" ? 400 : 362
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme ? dlg.theme.cream : "#141414"
        radius: dlg.theme ? dlg.theme.rCard : 12
        border.color: dlg.theme ? dlg.theme.stroke : "#242424"
        border.width: 1
    }

    // 手写小组件：勾选框。界面上没有现成的 CheckBox（另外四个弹窗也都是手写的）。
    // 名字用 checked 而不是 on —— `on: ...` 这种属性名容易被当成信号处理器语法，别赌。
    component Tick: Rectangle {
        id: tick
        property bool checked: false
        width: 16
        height: 16
        radius: 4
        color: tick.checked ? (dlg.theme ? dlg.theme.inv : "#F0F0F0") : "transparent"
        border.color: tick.checked ? (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                                   : (dlg.theme ? dlg.theme.stroke : "#242424")
        border.width: 1
        Text {
            anchors.centerIn: parent
            visible: tick.checked
            text: "✓"
            color: dlg.theme ? dlg.theme.win : "#0A0A0A"
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    // 手写小组件：分段选择里的一段
    component Seg: Rectangle {
        id: seg
        property string label: ""
        property bool active: false
        signal tapped()
        width: 78
        height: 28
        radius: dlg.theme ? dlg.theme.rCtrl : 8
        color: seg.active ? (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                          : (segHover.containsMouse ? (dlg.theme ? dlg.theme.hover : "#1E1E1E") : "transparent")
        border.color: seg.active ? "transparent" : (dlg.theme ? dlg.theme.stroke : "#242424")
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: seg.label
            color: seg.active ? (dlg.theme ? dlg.theme.win : "#0A0A0A")
                              : (dlg.theme ? dlg.theme.op : "#C8C8C8")
            font.pixelSize: 12
            font.weight: seg.active ? Font.Medium : Font.Normal
        }
        MouseArea {
            id: segHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: seg.tapped()
        }
    }

    // 手写小组件：一个「勾选框 + 文案」的可点整行。
    // ⚠️ 不能用 Row 装 MouseArea 再 anchors.fill —— Row 会把每个子项横向排队，
    //    MouseArea 会被当成第三个格子而不是铺满整行。
    component ToggleRow: Item {
        id: row
        property string label: ""
        property bool checked: false
        signal tapped()
        width: 168
        height: 32
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Tick { checked: row.checked }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: row.label
                color: dlg.theme ? dlg.theme.op : "#C8C8C8"
                font.pixelSize: 12
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: row.tapped()
        }
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
                text: "通知"
                color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                text: backend.currentUid === "" ? "（先选一台设备）" : ("发给 " + backend.currentUid)
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

        // ── 形态 ──
        Item {
            width: parent.width
            height: 38
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "形态"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                spacing: 6
                Seg { label: "弹窗"; active: dlg.kind === "popup"; onTapped: dlg.kind = "popup" }
                Seg { label: "灵动岛"; active: dlg.kind === "island"; onTapped: dlg.kind = "island" }
                Seg { label: "全屏"; active: dlg.kind === "fullscreen"; onTapped: dlg.kind = "fullscreen" }
            }
        }

        // ── 标题（必填）──
        Item {
            width: parent.width
            height: 38
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "标题"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                width: parent.width - 74 - 78
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                border.color: dlg.titleOver ? "#BA7517" : (dlg.theme ? dlg.theme.stroke : "#242424")
                border.width: 1
                TextInput {
                    id: titleInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    maximumLength: 120
                    onTextChanged: dlg.error = ""
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: titleInput.text.length + "/" + dlg.capTitle
                color: dlg.titleOver ? "#BA7517" : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                font.pixelSize: 11
            }
        }

        // ── 正文 ──
        Item {
            width: parent.width
            height: 38
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "正文"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                width: parent.width - 74 - 78
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                border.color: dlg.contentOver ? "#BA7517" : (dlg.theme ? dlg.theme.stroke : "#242424")
                border.width: 1
                TextInput {
                    id: bodyInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    maximumLength: 300
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                text: bodyInput.text.length + "/" + dlg.capContent
                color: dlg.contentOver ? "#BA7517" : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                font.pixelSize: 11
            }
        }

        // ── 显示时长 ──
        Item {
            width: parent.width
            height: 38
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "显示时长"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                width: 92
                height: 30
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                border.color: dlg.theme ? dlg.theme.stroke : "#242424"
                border.width: 1
                TextInput {
                    id: secInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: dlg.theme ? dlg.theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                    validator: IntValidator { bottom: 0; top: 3600 }
                    text: "0"
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 176
                text: "秒 · 0 = 按字数自适应（约 5 秒起）"
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
            }
        }

        // ── 全屏配色（只对全屏生效，别的形态整行不显示）──
        Item {
            width: parent.width
            height: 38
            visible: dlg.kind === "fullscreen"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: "配色"
                color: dlg.theme ? dlg.theme.fg3 : "#5A5A5A"
                font.pixelSize: 11
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: 74
                spacing: 6
                Seg { label: "提醒"; active: dlg.severity === "remind"; onTapped: dlg.severity = "remind" }
                Seg { label: "提示"; active: dlg.severity === "inform"; onTapped: dlg.severity = "inform" }
                Seg { label: "紧急"; active: dlg.severity === "urgent"; onTapped: dlg.severity = "urgent" }
            }
        }

        // ── 两个开关 ──
        Item {
            width: parent.width
            height: 32
            ToggleRow {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                label: "语音播报"
                checked: dlg.speech
                onTapped: dlg.speech = !dlg.speech
            }
            ToggleRow {
                anchors.verticalCenter: parent.verticalCenter
                x: 200
                label: "紧急锁定"
                checked: dlg.emergency
                onTapped: dlg.emergency = !dlg.emergency
            }
        }

        // ── 发送 / 取消 ──
        Item {
            width: parent.width
            height: 52
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: 130
                height: 32
                radius: dlg.theme ? dlg.theme.rCtrl : 8
                color: dlg.canSend ? (dlg.theme ? dlg.theme.inv : "#F0F0F0")
                                   : (dlg.theme ? dlg.theme.hover2 : "#111111")
                Text {
                    anchors.centerIn: parent
                    text: "发送"
                    color: dlg.canSend ? (dlg.theme ? dlg.theme.win : "#0A0A0A")
                                       : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dlg.send()
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                text: dlg.error !== "" ? dlg.error
                      : (backend.currentUid === "" ? "没选设备，发不出去"
                         : (titleInput.text.trim() === "" ? "标题必填 —— 被控端会拒收" : ""))
                color: dlg.error !== "" ? "#E24B4A" : (dlg.theme ? dlg.theme.fg4 : "#3A3A3A")
                font.pixelSize: 11
            }
        }

        // ── 一起说清楚 ──
        Item {
            width: parent.width
            height: 50
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: dlg.theme ? dlg.theme.line : "#161616"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 18
                width: parent.width - 36
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                color: dlg.theme ? dlg.theme.fg4 : "#3A3A3A"
                font.pixelSize: 11
                text: "标题超 " + dlg.capTitle + " 字、正文超 " + dlg.capContent
                      + " 字会被截断（语音也只读截断后的）。紧急锁定＝不自动消失，要学生在屏幕上点「确认」。"
            }
        }
    }

    function send() {
        if (backend.currentUid === "") { dlg.error = "没选设备"; return; }
        const t = titleInput.text.trim();
        if (t === "") { dlg.error = "标题必填"; return; }

        // 只带被控端**真的会读**的字段。notice_id 是站点侧对账用的（它有 notice_kinds 表），
        // 管理端没有这张表，带了也没人认，所以不发。
        let flags = { "speech": dlg.speech };
        if (dlg.kind === "fullscreen") flags["severity"] = dlg.severity;
        if (dlg.emergency) flags["emergency_confirm"] = true;

        let params = {
            "kind": dlg.kind,
            "title": t,
            "content": bodyInput.text,
            "flags": flags
        };
        const sec = parseInt(secInput.text, 10);
        if (sec > 0) params["seconds"] = sec;

        backend.sendAction("notify", params);
        dlg.error = "";
        dlg.close();
    }

    onOpened: dlg.error = ""
}
