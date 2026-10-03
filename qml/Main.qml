// 星集控 · 管理端界面（Qt Quick / QML）
//
// 依据《星集控-电脑端界面稿集-黑白-2026-10-03.html》：
//   · 黑白、默认黑、无彩色；唯一强调手段＝反白（#F0F0F0 底 + 黑字）
//   · 一屏一主：控制页的主体是「教室画面」，左右两栏是配角
//   · 文案只写"要做什么 / 成了没"，不写说明、不写术语
//   · 未实现的能力置灰（不假装能用）
//
// 逻辑全在 ViewerBackend（C++）：本文件只负责显示与转发用户操作。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1180
    height: 760
    minimumWidth: 940
    minimumHeight: 620
    visible: true
    title: "星集控"
    color: th.win

    // ── 主题令牌（黑白默认黑；浅色为备选）──
    // 页面里一律从 th 取色，不写死 —— 否则换主题必花。
    property bool darkMode: true

    readonly property var darkTh: ({
        win: "#0A0A0A", body: "#080808", panel: "#101010",
        cream: "#141414", hover: "#1E1E1E", seg: "#121212",
        hover2: "#111111", line: "#161616", canvas: "#1A1A1A",
        stroke: "#242424", stroke2: "#1E1E1E", todo: "#191919",
        fg: "#FAFAFA", op: "#C8C8C8", fg3: "#5A5A5A",
        fg4: "#3A3A3A", inv: "#F0F0F0", ph: "#4E4E4E",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var lightTh: ({
        win: "#F6F6F6", body: "#FFFFFF", panel: "#FFFFFF",
        cream: "#EDEDED", hover: "#E6E6E6", seg: "#E9E9E9",
        hover2: "#EFEFEF", line: "#E3E3E3", canvas: "#DCDCDC",
        stroke: "#C4C4C4", stroke2: "#BCBCBC", todo: "#CCCCCC",
        fg: "#161616", op: "#3C3C3C", fg3: "#787878",
        fg4: "#A6A6A6", inv: "#1A1A1A", ph: "#9A9A9A",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var th: darkMode ? darkTh : lightTh

    // 界面自身的状态（只跟显示有关的东西）
    property int page: 1                 // 0 概览 / 1 控制 / 2 设置
    property int volumeValue: 30
    property bool lastHas: false
    property bool lastOk: false
    property string lastText: ""
    property var results: []          // 概览页的「最近回执」流水（最多 20 条，只收真回执）
    property var candidates: []       // 软件弹窗的「可打开」候选（来自被控端 list_shortcut_candidates）

    // 云端回来的东西：一切以它为准
    readonly property var devices: backend.devices
    readonly property string currentUid: backend.currentUid

    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, error, detail, data) {
            if (state !== "executed")
                return
            root.lastHas = true
            root.lastOk = (result === "done")
            root.lastText = action + " · " + (result === "done" ? "完成" : "失败")

            // 概览页的「最近回执」流水：只收真回执（executed），时间倒序，最多 20 条
            var row = {
                t: Qt.formatTime(new Date(), "hh:mm:ss"),
                uid: uid,
                action: action,
                ok: (result === "done"),
                err: error || ""
            }
            var arr = root.results.slice()
            arr.unshift(row)
            root.results = arr.slice(0, 20)

            if (action === "list_shortcut_candidates" && data) {
                root.candidates = (data.apps || []).concat(data.desktop || [])
            }
            if (action === "process_list" && data && data.items) {
                softwareDlg.items = data.items
                softwareDlg.truncated = (data.truncated === true)
                openOnly("software")
            } else if (action === "log_tail" && data && data.lines) {
                logDlg.lines = data.lines
                logDlg.logPath = data.path || ""
                openOnly("log")
            } else if (action === "media_list" && data) {
                // 删完自动重拉时也会走到这儿（弹窗已开着）→ 先灌数据再开，开的时候顺手关掉别的
                mediaDlg.setItems(data.items || [], data.dir || "", data.truncated === true)
                openOnly("media")
            } else if (action === "list_schedules" && data) {
                schedDlg.setItems(data.items || [], data.file || "")
                openOnly("sched")
            }
        }
    }

    // ══════════════ 主结构 ══════════════
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 顶部条 ──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 48

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 16

                Text {
                    text: "星集控"
                    color: th.fg
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }

                // 角色切换（被控端是另一台机器的形态，这里先只做控制端）
                Rectangle {
                    implicitWidth: 116
                    implicitHeight: 26
                    radius: 999
                    color: th.seg

                    Row {
                        anchors.centerIn: parent
                        Rectangle {
                            width: 58; height: 22; radius: 999
                            color: th.inv
                            Text {
                                anchors.centerIn: parent
                                text: "控制"
                                color: th.win
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                        }
                        Rectangle {
                            width: 58; height: 22; radius: 999
                            color: "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "被控"
                                color: th.fg3
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 5; height: 5; radius: 3
                        color: backend.connected ? th.inv : th.fg4
                    }
                    Text {
                        text: backend.connected
                              ? (devices.length + " 台在线")
                              : (backend.authed ? "已连接" : "未连接")
                        color: th.fg3
                        font.pixelSize: 11
                    }
                }
            }
        }

        // ── 主体：三页，按底部标签切换（一屏一主）──
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.page

            // ══ 0 概览 ══（只放真实有的数据；没有数据源的明说未接入，不编）
            Item {
                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 20

                    Row {
                        spacing: 44
                        Column {
                            spacing: 6
                            Text { text: "在线设备"; color: th.fg3; font.pixelSize: 11 }
                            Text {
                                text: root.devices.length
                                color: th.fg; font.pixelSize: 26; font.weight: Font.Medium
                            }
                        }
                        Column {
                            spacing: 6
                            Text { text: "已收帧"; color: th.fg3; font.pixelSize: 11 }
                            Text {
                                text: backend.frameCount
                                color: th.fg; font.pixelSize: 26; font.weight: Font.Medium
                            }
                        }
                        Column {
                            spacing: 6
                            Text { text: "云端"; color: th.fg3; font.pixelSize: 11 }
                            Text {
                                text: backend.connected ? "已连接" : "未连接"
                                color: th.fg; font.pixelSize: 26; font.weight: Font.Medium
                            }
                        }
                    }

                    Text { text: "在线设备"; color: th.fg3; font.pixelSize: 11 }

                    Column {
                        width: parent.width
                        spacing: 2

                        Repeater {
                            model: root.devices
                            delegate: Rectangle {
                                width: parent.width
                                height: 38
                                radius: 9
                                color: ovHover.containsMouse ? th.cream : "transparent"

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 12
                                    spacing: 10
                                    Rectangle {
                                        width: 5; height: 5; radius: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: th.inv
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.uid
                                        color: th.fg; font.pixelSize: 13
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: (modelData.framesIn || 0) + " 帧"
                                        color: th.fg3; font.pixelSize: 11
                                    }
                                }

                                MouseArea {
                                    id: ovHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        backend.currentUid = modelData.uid
                                        root.page = 1   // 点一台就跳到控制页看它
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.devices.length === 0
                        text: "暂无设备"
                        color: th.fg3
                        font.pixelSize: 12
                    }

                    // ── 最近回执（真数据：来自 backend 的 resultReceived）──
                    Text {
                        visible: root.results.length > 0
                        text: "最近回执"
                        color: th.fg3
                        font.pixelSize: 11
                    }

                    Column {
                        width: parent.width
                        spacing: 6
                        visible: root.results.length > 0

                        Repeater {
                            model: root.results
                            delegate: Row {
                                spacing: 14
                                Text {
                                    text: modelData.t
                                    color: th.fg4
                                    font.pixelSize: 11
                                    width: 58
                                }
                                Text {
                                    text: modelData.ok ? "✓" : "✕"
                                    color: modelData.ok ? th.inv : th.fg3
                                    font.pixelSize: 12
                                }
                                Text {
                                    text: modelData.uid + " · " + modelData.action
                                          + (modelData.ok ? ""
                                                          : (modelData.err ? "（" + modelData.err + "）" : ""))
                                    color: modelData.ok ? th.op : th.fg3
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }

                    // 诚实标注：稿集里这两块没有数据源，就不假装有
                    Text {
                        text: "课表 · 待办：尚未接入（无数据源）"
                        color: th.fg4
                        font.pixelSize: 11
                    }
                }
            }

            // ══ 1 控制 ══
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 0
            Layout.bottomMargin: 14
            spacing: 14

            // ───── 左：设备（配角）─────
            Column {
                Layout.preferredWidth: 168
                Layout.fillHeight: true
                spacing: 2

                Text {
                    text: "设备"
                    color: th.fg3
                    font.pixelSize: 11
                    leftPadding: 10
                    bottomPadding: 8
                }

                Repeater {
                    model: root.devices
                    delegate: Rectangle {
                        width: 168
                        height: 36
                        radius: 9
                        property bool cur: (modelData.uid === root.currentUid)
                        color: cur ? th.cream : (devHover.containsMouse ? th.hover2 : "transparent")

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 9
                            Rectangle {
                                width: 5; height: 5; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: th.inv
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.uid
                                color: cur ? th.fg : th.op
                                font.pixelSize: 13
                                font.weight: cur ? Font.Medium : Font.Normal
                            }
                        }

                        MouseArea {
                            id: devHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: backend.currentUid = modelData.uid
                        }
                    }
                }

                Item { width: 1; height: 14 }
                Rectangle { width: 148; height: 1; color: th.line; x: 10 }
                Text {
                    text: "全校"
                    color: th.fg3
                    font.pixelSize: 11
                    leftPadding: 10
                    topPadding: 12
                }
            }

            // ───── 中：画面（主体）─────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: root.currentUid === "" ? "未选择设备" : root.currentUid
                        color: th.fg
                        font.pixelSize: 15
                        font.weight: Font.Medium
                    }
                    Text {
                        visible: root.currentUid !== ""
                        text: Math.round(backend.fps * 10) / 10 + " fps · "
                              + Math.round(backend.lastFrameBytes / 1024) + " KB"
                        color: th.fg3
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: root.currentUid !== ""
                        text: "已收 " + backend.frameCount + " 帧"
                        color: th.fg3
                        font.pixelSize: 11
                    }
                }

                // 画面本体：按住拖动＝直接操作那台机器
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: th.rCard
                    // 画面容器**刻意不随主题变**：画面内容以深色为主，浅色底会把画面"框"成一块黑，
                    // 又刺眼又显廉价 —— 视频类界面用中性深底是通行做法。这是本页唯一的主题例外。
                    color: "#0E0E0E"
                    border.color: "#1F1F1F"
                    border.width: 1
                    clip: true

                    Image {
                        id: screenImg
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        property int tick: 0
                        source: backend.frameCount > 0 ? ("image://frames/f?" + tick) : ""

                        Connections {
                            target: backend
                            function onFrameChanged() { screenImg.tick++ }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: backend.frameCount === 0
                        text: root.currentUid === "" ? "在左侧选择一台设备" : "等待画面…"
                        color: "#6A6A6A"   // 固定灰：这块底色不随主题变
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: screenArea
                        anchors.fill: parent
                        enabled: backend.frameCount > 0
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.CrossCursor

                        onPressed: function (mouse) {
                            backend.sendPointer("down", mouse.x / width, mouse.y / height)
                        }
                        // 只有**按住左键**才算拖动：悬停绝不能往教室机灌鼠标移动
                        // （曾经用 hoverEnabled + 自记 dragging，结果悬停就发指令、把日志刷爆）
                        onPositionChanged: function (mouse) {
                            if (mouse.buttons & Qt.LeftButton)
                                backend.sendPointer("move", mouse.x / width, mouse.y / height)
                        }
                        onReleased: function (mouse) {
                            backend.sendPointer("up", mouse.x / width, mouse.y / height)
                        }
                    }
                }
            }

            // ───── 右：动作（配角）─────
            ColumnLayout {
                Layout.preferredWidth: 196
                Layout.fillHeight: true
                spacing: 7

                Grid {
                    columns: 2
                    spacing: 7

                    Repeater {
                        model: [
                            { l: "锁屏",   a: "lock",                   t: false },
                            { l: "重启",   a: "reboot",                 t: false },
                            { l: "关机",   a: "shutdown",               t: false },
                            { l: "截图",   a: "screenshot",             t: false },
                            { l: "音量",   a: "",                       t: false },
                            { l: "软件",   a: "process_list",           t: false },
                            { l: "日志",   a: "log_tail",               t: false },
                            { l: "探活",   a: "",                       t: false },
                            // ── 批次 2：全量补齐（点了有回执、失败有原因，不是按钮摆设）──
                            { l: "定时",   a: "",                       t: false },
                            { l: "计划",   a: "list_schedules",         t: false },
                            { l: "摄像头", a: "camera_list",            t: false },
                            { l: "拍一张", a: "camera_snapshot",        t: false },
                            { l: "开录",   a: "camera_record_start",    t: false },
                            { l: "停录",   a: "camera_record_stop",     t: false },
                            { l: "媒体",   a: "media_list",             t: false },
                            { l: "播放",   a: "media_session_start",    t: false },
                            { l: "停播",   a: "media_session_stop",     t: false },
                            { l: "远控开", a: "remote_control_start",   t: false },
                            { l: "远控关", a: "remote_control_stop",    t: false }
                        ]

                        delegate: Rectangle {
                            width: 94
                            height: 30
                            radius: th.rCtrl
                            color: (btnHover.containsMouse && !modelData.t) ? th.hover : "transparent"
                            border.color: modelData.t ? th.todo : th.stroke
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData.l
                                color: modelData.t ? th.fg4 : th.op
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: btnHover
                                anchors.fill: parent
                                hoverEnabled: !modelData.t
                                enabled: !modelData.t
                                onClicked: {
                                    if (modelData.l === "音量") {
                                        volumeDlg.open()
                                    } else if (modelData.l === "探活") {
                                        backend.sendPing()
                                    } else if (modelData.l === "软件") {
                                        // 软件弹窗要两块数据：可打开（选自这台机器）+ 正在运行
                                        backend.sendAction("list_shortcut_candidates")
                                        backend.sendAction("process_list")
                                    } else if (modelData.l === "定时" || modelData.l === "计划") {
                                        // 先拉一次，回执到了（onResultReceived）再开弹窗 —— 免得开出来是空的
                                        backend.sendAction("list_schedules")
                                        openOnly("sched")
                                    } else if (modelData.l === "远控开") {
                                        backend.sendAction("remote_control_start", { "fps": 20 })
                                    } else if (modelData.a !== "") {
                                        backend.sendAction(modelData.a, {})
                                    }
                                }
                            }
                        }
                    }

                    // 音量已在网格里（第 4 格），这里不再单独放按钮

                    Rectangle {
                        width: 94; height: 30; radius: th.rCtrl
                        color: todo1.containsMouse ? "transparent" : "transparent"
                        border.color: th.todo; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "通知"
                            color: th.fg4
                            font.pixelSize: 12
                        }
                        MouseArea { id: todo1; anchors.fill: parent; enabled: false }
                    }

                    // 「文件」原来是个 enabled:false 的灰占位（从界面稿搬来的），看着像按钮、点了没反应。
                    // 现在真接上：选本机文件 → file_push/file_chunk/file_done 推给教室机（2026-10-03）
                    Rectangle {
                        width: 94; height: 30; radius: th.rCtrl
                        color: fileBtn.containsMouse ? th.hover : "transparent"
                        border.color: fileBtn.containsMouse ? th.inv : th.stroke
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "文件"
                            color: th.fg
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: fileBtn
                            anchors.fill: parent
                            onClicked: openOnly("file")
                        }
                    }
                }

                // 给被控端打字
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: th.rCtrl
                    color: "transparent"
                    border.color: th.stroke
                    border.width: 1

                    TextInput {
                        id: typeInput
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        color: th.fg
                        font.pixelSize: 12
                        selectByMouse: true
                        clip: true
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: typeInput.text === ""
                            text: "打字发给教室机，回车"
                            color: th.fg4
                            font.pixelSize: 12
                        }
                        Keys.onReturnPressed: {
                            if (text !== "") {
                                backend.sendType(text)
                                text = ""
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // 最近一次结果
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: 9
                    color: "transparent"
                    border.color: root.lastHas ? th.stroke : "transparent"
                    border.width: 1

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10
                        spacing: 9
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.lastHas
                            text: root.lastOk ? "✓" : "✕"
                            color: root.lastOk ? th.inv : th.fg3
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.lastHas ? root.lastText : "还没有操作"
                            color: root.lastHas ? th.op : th.fg4
                            font.pixelSize: 12
                        }
                    }
                }
            }
            }

            // ══ 2 设置 ══
            Item {
                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 22

                    Column {
                        width: parent.width - 36
                        spacing: 12
                        Text { text: "连接"; color: th.fg3; font.pixelSize: 11 }
                        Rectangle { width: parent.width; height: 1; color: th.line }
                        Row {
                            spacing: 14
                            Text { text: "云端"; color: th.fg3; font.pixelSize: 12; width: 56 }
                            Text { text: backend.cloudUrl; color: th.op; font.pixelSize: 12 }
                        }
                        Row {
                            spacing: 14
                            Text { text: "状态"; color: th.fg3; font.pixelSize: 12; width: 56 }
                            Text {
                                text: backend.connected ? "已连接" : "未连接"
                                color: th.op; font.pixelSize: 12
                            }
                        }
                    }

                    Column {
                        width: parent.width - 36
                        spacing: 12
                        Text { text: "外观"; color: th.fg3; font.pixelSize: 11 }
                        Row {
                            spacing: 10

                            Rectangle {
                                width: 118; height: 34; radius: th.rCtrl
                                color: "transparent"
                                border.color: root.darkMode ? th.inv : th.stroke
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "黑白"
                                    color: root.darkMode ? th.fg : th.fg3
                                    font.pixelSize: 12
                                    font.weight: root.darkMode ? Font.Medium : Font.Normal
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.darkMode = true
                                }
                            }

                            Rectangle {
                                width: 118; height: 34; radius: th.rCtrl
                                color: "transparent"
                                border.color: root.darkMode ? th.stroke : th.inv
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "浅色"
                                    color: root.darkMode ? th.fg3 : th.fg
                                    font.pixelSize: 12
                                    font.weight: root.darkMode ? Font.Normal : Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.darkMode = false
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width - 36
                        spacing: 10
                        Text { text: "关于"; color: th.fg3; font.pixelSize: 11 }
                        Text { text: "Qt 6.8.1 · Qt Quick / QML"; color: th.op; font.pixelSize: 12 }
                        Text { text: "版本号未注入（构建时未写入）"; color: th.fg3; font.pixelSize: 11 }
                        Text { text: "日志：stelarith-viewer-qt/viewer.log"; color: th.fg3; font.pixelSize: 11 }
                    }

                    Text {
                        text: "账户：尚未接入（桌面端目前用长期令牌，没有「登录用户」概念）"
                        color: th.fg4; font.pixelSize: 11
                    }
                }
            }
        }

        // ── 底部标签（与手机端同一套心智）──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: th.win

            Rectangle { width: parent.width; height: 1; color: th.line }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: [ "概览", "控制", "设置" ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: (index === root.page) ? th.fg : th.fg3
                            font.pixelSize: 12
                            font.weight: (index === root.page) ? Font.Medium : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.page = index
                        }
                    }
                }
            }
        }
    }

    // ── 音量弹层 ──
    Popup {
        id: volumeDlg
        anchors.centerIn: Overlay.overlay
        width: 300
        modal: true
        padding: 0

        background: Rectangle {
            color: th.cream
            radius: th.rCard
            border.color: th.stroke
            border.width: 1
        }

        contentItem: Column {
            spacing: 14
            padding: 16

            Text {
                text: "音量"
                color: th.fg
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            Row {
                spacing: 10
                width: parent.width - 32
                Slider {
                    id: volSlider
                    width: parent.width - 52
                    from: 0
                    to: 100
                    value: root.volumeValue
                    onMoved: root.volumeValue = Math.round(value)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.volumeValue + "%"
                    color: th.fg
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
            }

            Row {
                spacing: 10
                Rectangle {
                    width: 120; height: 34; radius: th.rCtrl
                    color: th.inv
                    Text {
                        anchors.centerIn: parent
                        text: "应用"
                        color: th.win
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            backend.sendAction("set_volume", { "value": root.volumeValue })
                            volumeDlg.close()
                        }
                    }
                }
                Rectangle {
                    width: 120; height: 34; radius: th.rCtrl
                    color: "transparent"
                    border.color: th.stroke
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: th.op
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: volumeDlg.close()
                    }
                }
            }
        }
    }

    // ── 打开程序（快捷方式来源＝这台机器上的程序，不预置名单）──
    Popup {
        id: launchDlg
        anchors.centerIn: parent
        width: 320
        modal: true
        padding: 0

        background: Rectangle {
            color: th.cream
            radius: th.rCard
            border.color: th.stroke
            border.width: 1
        }

        contentItem: Column {
            spacing: 14
            padding: 16

            Text {
                text: "打开程序"
                color: th.fg
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                text: "填程序名或路径（不在安全名单里的会被拒绝）"
                color: th.fg3
                font.pixelSize: 11
            }

            Rectangle {
                width: parent.width - 32
                height: 34
                radius: th.rCtrl
                color: "transparent"
                border.color: th.stroke
                border.width: 1
                TextInput {
                    id: launchInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: th.fg
                    font.pixelSize: 13
                    selectByMouse: true
                }
            }

            Row {
                spacing: 10
                Rectangle {
                    width: 130; height: 34; radius: th.rCtrl
                    color: th.inv
                    Text {
                        anchors.centerIn: parent
                        text: "打开"
                        color: th.win
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (launchInput.text !== "") {
                                backend.sendAction("launch_app", { "target": launchInput.text })
                                launchInput.text = ""
                                launchDlg.close()
                            }
                        }
                    }
                }
                Rectangle {
                    width: 130; height: 34; radius: th.rCtrl
                    color: "transparent"
                    border.color: th.stroke
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: th.op
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: launchDlg.close()
                    }
                }
            }
        }
    }

    SoftwareDialog {
        id: softwareDlg
        theme: root.th
        candidates: root.candidates
    }
    LogDialog { id: logDlg; theme: root.th }

    MediaDialog { id: mediaDlg; theme: root.th }
    ScheduleDialog { id: schedDlg; theme: root.th }
    // 注意类名是 FilePushDialog 不是 FileDialog：同个文件里要 import QtQuick.Dialogs 拿原生
    // 选文件对话框，两个 FileDialog 撞名 qml 编译期就冲突了，所以弹窗这侧改名（2026-10-03）
    FilePushDialog { id: fileDlg; theme: root.th }

    // ══════════════ 弹窗互斥 ══════════════
    // 软件/日志/媒体/定时/文件 都是同一块居中 modal 弹窗，谁后开谁压在谁上面。
    // 原来的写法只防"重复 open"（if (!mediaDlg.opened) ...），结果点了媒体再点定时 →
    // 三层叠在一起，下面的软件弹窗被盖死（2026-10-03 自检抓图 qml-medialog/qml-schedialog
    // 里肉眼看见三层叠着才发现 —— 只看文件大小/只看"图存了"是发现不了的）。
    // 所以统一走 openOnly()：开一个之前把其它全关掉，同一时刻屏幕上只可能有一个。
    // 音量滑块（volumeDlg）是贴在画面上的小浮层，不在这四个里，照旧可共存。
    function openOnly(which) {
        softwareDlg.close()
        logDlg.close()
        mediaDlg.close()
        schedDlg.close()
        fileDlg.close()
        if      (which === "software") softwareDlg.open()
        else if (which === "log")      logDlg.open()
        else if (which === "media")    mediaDlg.open()
        else if (which === "sched")    schedDlg.open()
        else if (which === "file")     fileDlg.open()
    }
}
