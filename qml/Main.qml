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
import "schedule"          // 课表编辑器模块（2026-10-06 并入，schedule/ 子目录）

ApplicationWindow {
    id: root
    width: 1180
    height: 760
    minimumWidth: 940
    // 2026-10-06：620 → 680。右栏动作区是**固定高度**的一列（ColumnLayout，子项都是固定高），
    // 空间不够时不会被压缩，只会溢出到窗口外压住底部标签栏。
    // 加了「通知」整行（+36px）后，右栏内容约 543px，而 620 高时可用只有约 520px —— 会溢出。
    // ⚠️ 右栏高度预算已经很紧：**下次再往右栏加按钮，应该先把它改成可滚动（Flickable）
    //    而不是继续抬最小高度**。见 docs/管理端功能一览.md 的动作栏说明。
    minimumHeight: 680
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
        card: "#181818", sepline: "#141414",
        fg: "#FAFAFA", op: "#C8C8C8", fg3: "#5A5A5A",
        fg4: "#3A3A3A", inv: "#F0F0F0", ph: "#4E4E4E",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var lightTh: ({
        win: "#F6F6F6", body: "#FFFFFF", panel: "#FFFFFF",
        cream: "#EDEDED", hover: "#E6E6E6", seg: "#E9E9E9",
        hover2: "#EFEFEF", line: "#E3E3E3", canvas: "#DCDCDC",
        stroke: "#C4C4C4", stroke2: "#BCBCBC", todo: "#CCCCCC",
        card: "#DCDCDC", sepline: "#DEDEDE",
        fg: "#161616", op: "#3C3C3C", fg3: "#787878",
        fg4: "#A6A6A6", inv: "#1A1A1A", ph: "#9A9A9A",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var th: darkMode ? darkTh : lightTh

    // 界面自身的状态（只跟显示有关的东西）
    property int page: 1                 // 0 概览 / 1 控制 / 2 设置
    property int volumeValue: 30
    // 远端确认过的音量（set_volume 回执里的 data.volume）。-1 = 还没拿到，就别往界面上写。
    // 刻意不用滑块那个值：root.volumeValue 只是本机拖动出来的数字，被控端根本没确认过，
    // 拿它冒充"教室机现在音量多少"就是假装成功了。
    property int remoteVolume: -1
    property var results: []          // 概览页的「最近回执」流水（最多 20 条，只收真回执）
    property var candidates: []       // 软件弹窗的「可打开」候选（来自被控端 list_shortcut_candidates）

    // 云端回来的东西：一切以它为准
    readonly property var devices: backend.devices
    readonly property string currentUid: backend.currentUid
    // 「正在接通」：选了某台机器、画面还没出第一帧，且 RTC 链路确实在建
    // （rtcState 由 backend 维护：idle → waiting → track）。
    // 只写"还没出帧"会把"被控端根本没推流"也误报成在忙，让用户空等。
    readonly property bool linkBusy: backend.currentUid !== ""
                                     && backend.frameCount === 0
                                     && backend.rtcState === "waiting"

    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, error, detail, data) {
            if (state !== "executed")
                return
            // 画面右下角那个音量，只认回执；滑块那个数字不写回界面（见 remoteVolume 注释）
            if (action === "set_volume" && data && data.volume !== undefined)
                root.remoteVolume = data.volume

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
        // 令牌表：顶条 44px / 底标签 42px，两边各留 18。以前是 48，看着没差别，
        // 但和稿并排一比就出来了。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            // 稿 .top 有条 border-bottom:#141414 —— 之前整条没画，顶条和主体糊在一起
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: th.sepline
            }

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

            // ══ 0 概览 ══
            // 稿里这屏的主角是「今日课表」，但课表现在没有数据源（站点那套课表没接进来）。
            // 规则 6「零编造」压过版式：宁可不摆那张卡，也不填一排自己编出来的课名。
            // 所以主角换成确实有的东西：在线设备 + 最近回执，两张卡按稿 .box 做。
            Item {
                Rectangle { anchors.fill: parent; color: th.body }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    // 稿上是「在线 / 待办 / 离线」三个数。待办和离线现在算不出来
                    // （没有台账，离线设备云端也不下发），所以只留两个真有的；
                    // 「云端」不再在这儿写第二遍 —— 顶条右侧已经写着连接状态（规则 4）。
                    Row {
                        spacing: 32
                        Repeater {
                            model: [
                                { k: "在线",   v: root.devices.length },
                                { k: "已收帧", v: backend.frameCount }
                            ]
                            delegate: Row {
                                spacing: 8
                                Text { text: modelData.k; color: th.fg3; font.pixelSize: 11 }
                                Text {
                                    text: modelData.v
                                    color: th.fg; font.pixelSize: 20; font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14

                        // 卡片＝稿 .box：panel 底 + card 边 + 12 圆角 + 16 内距
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                Row {
                                    width: parent.width
                                    Text {
                                        text: "在线设备"
                                        color: th.fg; font.pixelSize: 12; font.weight: Font.Medium
                                    }
                                    Text {
                                        anchors.right: parent.right
                                        text: root.devices.length + " 台"
                                        color: th.fg3; font.pixelSize: 11
                                    }
                                }
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
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                backend.currentUid = modelData.uid
                                                root.page = 1   // 点一台就跳到控制页看它
                                            }
                                        }
                                    }
                                }

                                // 空态照稿 .empty：标题 12/操作色 + 说明 11/辅助色，两行居中。
                                // 之前只有孤零零一句"暂无设备"，看着像坏了不像没设备。
                                Item {
                                    width: parent.width
                                    height: 96
                                    visible: root.devices.length === 0
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "暂无设备"
                                            color: th.op; font.pixelSize: 12
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "屏幕上的机器会自动出现"
                                            color: th.fg3; font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                Text {
                                    text: "最近回执"
                                    color: th.fg; font.pixelSize: 12; font.weight: Font.Medium
                                }

                                // 稿 .feed：时间 42px/11/辅助色，✓ ✕ 固定宽 12px，
                                // 成功失败靠符号分不靠颜色（规则 3）—— 所以两个符号同色。
                                Repeater {
                                    model: root.results
                                    delegate: Row {
                                        spacing: 11
                                        width: parent.width
                                        Text {
                                            text: modelData.t
                                            color: th.fg3
                                            font.pixelSize: 11
                                            width: 42
                                        }
                                        Text {
                                            text: modelData.ok ? "✓" : "✕"
                                            color: th.fg
                                            font.pixelSize: 12
                                            width: 12
                                        }
                                        Text {
                                            width: parent.width - 76
                                            text: modelData.uid + " · " + modelData.action
                                                  + (modelData.ok ? ""
                                                        : (modelData.err ? "（" + modelData.err + "）" : ""))
                                            color: modelData.ok ? th.op : th.fg3
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                Item {
                                    width: parent.width
                                    height: 96
                                    visible: root.results.length === 0
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "还没有操作"
                                            color: th.op; font.pixelSize: 12
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "点了右边的动作，结果记在这儿"
                                            color: th.fg3; font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ══ 1 控制 ══
            // 稿 .body 的底色是 #080808，比窗口 #0A0A0A 深一档 —— 差两个十六进制数位，
            // 肉眼几乎分不出，但主体区域"沉下去"那点层次就是靠它。
            Item {
                Rectangle { anchors.fill: parent; color: th.body }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.topMargin: 0
                    anchors.bottomMargin: 14
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

                    // 链路徽标：让用户一眼看出"现在看的是实时流还是轮询截图"。
                    // 设计稿的规矩是黑白里唯一的强调手段＝反白，所以：
                    //   实时（RTC）＝反白（最亮，因为它是我们要的状态）
                    //   轮询（JPEG）＝描边灰（弱化，说明它在走退化的路）
                    //   建流中     ＝todo 底 + 呼吸圆点（说清"正在办"，不是"没反应"）
                    Rectangle {
                        visible: root.currentUid !== ""
                        radius: 4
                        height: 20
                        // 文案宽度不固定，交给 trailing Text 自己撑
                        width: linkLabel.implicitWidth + (root.linkBusy ? 24 : 14)
                        color: backend.frameSource === "rtc" ? th.inv
                             : root.linkBusy ? th.todo : "transparent"
                        border.color: backend.frameSource === "rtc" ? th.inv
                                    : root.linkBusy ? th.stroke : th.stroke
                        border.width: 1

                        Rectangle {
                            visible: root.linkBusy
                            anchors.left: parent.left
                            anchors.leftMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            width: 5
                            height: 5
                            radius: 3
                            color: th.op
                            // 呼吸用 QML 自带的循环动画，不用 Math.sin + 高频 Timer：
                            // 后者为了让一个 5px 的点动起来，每秒要把整个绑定重算 20~30 次
                            opacity: 0.25
                            SequentialAnimation on opacity {
                                running: root.linkBusy
                                loops: Animation.Infinite
                                NumberAnimation { from: 0.25; to: 1.0; duration: 620 }
                                NumberAnimation { from: 1.0; to: 0.25; duration: 620 }
                            }
                        }

                        Text {
                            id: linkLabel
                            anchors.left: parent.left
                            anchors.leftMargin: root.linkBusy ? 18 : 7
                            anchors.verticalCenter: parent.verticalCenter
                            text: backend.frameSource === "rtc" ? "实时"
                                : root.linkBusy ? "正在接通…"
                                : backend.frameSource === "jpeg" ? "轮询" : "等待画面"
                            color: backend.frameSource === "rtc" ? (root.darkMode ? "#0A0A0A" : "#FFFFFF")
                                 : th.fg3
                            // 稿上没有 10px 这一档（最小是"注 11"），徽标字太小会把
                            // "实时/轮询"变成需要凑近看的装饰 —— 统一提到 11。
                            font.pixelSize: 11
                            font.weight: backend.frameSource === "rtc" ? Font.Medium : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 400
                            ToolTip.text: backend.frameSource === "rtc"
                                          ? "WebRTC 实时流：被控端推的 video 轨，端到端延迟在百毫秒级"
                                          : root.linkBusy
                                            ? "正在和被控端建立 WebRTC 连接，成功后会自动切成实时"
                                            : "JPEG 轮询：被控端定时截图推送，比实时慢一些"
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // 画面本体：按住拖动＝直接操作那台机器
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: th.rCard
                    // 稿 .canvas＝panel 底 + #1A1A1A 边。以前这里写死 #0E0E0E/#1F1F1F，
                    // 两个都不在令牌里 —— 换主题时这块永远是暗的，是个没人记得住的例外。
                    color: th.panel
                    border.color: th.canvas
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

                    // 空态照稿 .empty：标题 12/操作色 + 说明 11/辅助色，两行。
                    // 分三种情形（没选机器 / 正在接通 / 选了但没帧）：以前一句话包打天下，
                    // 用户看不出是"正在办"还是"没人理"。
                    Column {
                        anchors.centerIn: parent
                        visible: backend.frameCount === 0
                        spacing: 6

                        BusyIndicator {
                            visible: root.linkBusy
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 28
                            height: 28
                            running: root.linkBusy
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.currentUid === "" ? "还没选机器"
                                : root.linkBusy ? "正在接通"
                                : "等画面"
                            color: th.op
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.currentUid === "" ? "在左边选一台"
                                : root.linkBusy ? "连上就有画面"
                                : "这台机器还没发来第一帧"
                            color: th.fg3
                            font.pixelSize: 11
                        }
                    }

                    // 静态区角标：被控端画面没在动时（老师看的是静止的投影/待机界面），
                    // 我们就不白烧重绘 —— 但必须说清楚"是没动，不是卡了"，否则用户以为掉线。
                    Text {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 9
                        visible: backend.screenStatic && backend.frameCount > 0
                        text: "画面未变化 · 已暂停刷新"
                        color: "#8A8A8A"   // 固定灰：不随主题变
                        font.pixelSize: 11
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

                // 稿 .viewfoot：左边「已收 N 帧 · 连接正常」，右边音量。
                // 帧数原来挤在标题行里跟设备名抢位置，挪下来之后也顺手满足了
                // 规则 4（同一个数一屏只说一遍）。
                // 「连接正常」是术语换日常词的产物：云端 WS 通着就说这句，不说协议状态。
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text {
                        visible: root.currentUid !== ""
                        text: "已收 " + backend.frameCount + " 帧"
                        color: th.fg3; font.pixelSize: 11
                    }
                    Text {
                        visible: root.currentUid !== ""
                        text: backend.connected ? "连接正常" : "连不上云端"
                        color: th.fg3; font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // 只显示被控端确认过的音量，没确认过就空着（见 remoteVolume 注释）
                        visible: root.remoteVolume >= 0
                        text: root.remoteVolume + "%"
                        color: th.fg3; font.pixelSize: 11
                    }
                }
            }

            // ───── 右：动作（配角）─────
            // 以前 20 个按钮平铺在一个网格里，找"关机"要把整栏扫一遍，
            // 而它旁边就坐着"截图"——误点代价和它完全不在一个量级。
            // 现在按「点了会怎样」分三组：**电源**（不可逆，独占整行、视觉更重）
            // / **看看**（只读，安全）/ **让它做事**（会改远端状态，成对的一左一右）。
            ColumnLayout {
                Layout.preferredWidth: 196
                Layout.fillHeight: true
                spacing: 10

                // 按钮外观只有这一份：以前"文件"是描边加粗、"音量"没特殊处理、
                // 其余是普通描边，三套样式早就漂了，改个圆角得改三处。
                //
                // 注意 Loader 的坑：Loader **不会**把自身尺寸让给被加载项，被加载项的
                // width/height 必须显式绑到 loader 上。否则 Rectangle 用的是自己那句
                // `width: ...` 算出 0（或者更糟：算出一个比容器大得多的值被裁掉），
                // 表现就是"按钮框还在、字没了"——而且 QML 一句报错都不给。
                Component {
                    id: actBtn

                    Rectangle {
                        id: btnBody
                        // d = { l 文案, a action, t 未实现 }；wide/heavy 由使用处（Btn）注入
                        property var d: ({ l: "", a: "", t: false })
                        property bool wide: false
                        property bool heavy: false
                        property bool pressed2: false

                        // 宽度往上问，别自己拍。见 Btn 里那段注释：写死具体值会让
                        // "跟着容器变"这件事失效，而 Loader 那条路又是单向绑不回来的。
                        width: parent ? parent.width : 94
                        height: 30
                        radius: th.rCtrl
                        // 未实现的：留白 + 极淡描边，看得出"还没做"，也不引诱人去点
                        color: d.t ? "transparent"
                             : pressed2 ? th.hover2
                             : ma.containsMouse ? th.hover : "transparent"
                        border.color: d.t ? th.line : heavy ? th.op : th.stroke
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: d.l
                            color: d.t ? th.fg4 : heavy ? th.fg : th.op
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: ma
                            anchors.fill: parent
                            // t = 这功能压根没做；capOk = 这台被控端上报的能力清单里认不认这条指令。
                            // 两者都置灰：没做的留白、不支持的照样看得见（灰着），但都不引诱人去点。
                            enabled: !d.t && capOkAll(d)
                            hoverEnabled: !d.t
                            // 以前鼠标移上去还是箭头，看不出这东西能点
                            cursorShape: d.t ? Qt.ArrowCursor
                                        : capOkAll(d) ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                            onPressed: pressed2 = true
                            onReleased: pressed2 = false
                            onCanceled: pressed2 = false
                            onClicked: root.runAction(d)
                        }

                        ToolTip {
                            // 不支持的也显示 tooltip（不然灰按钮 Why 都不问，像坏了），
                            // 文案分四种情况：不可逆 / 没选机器 / 这台机器不支持 / 正常
                            visible: ma.containsMouse && (!d.t || !capOkAll(d))
                            delay: 500
                            text: heavy ? "点了立刻执行，没法撤销"
                                : root.currentUid === "" ? "先在左边选一台机器"
                                : !capOkAll(d)
                                  ? ("这台被控端不支持：它上报的能力里没有 " + capNeedOf(d).filter(function (x) { return !capOk(x) }).join(" / "))
                                : ""
                        }
                    }
                }

                // 每个按钮外面都套一层 Loader 会重复四行样版代码（尺寸 + 两个开关 + 模型），
                // 所以统一走这个内联组件：传 d/wide/heavy，它负责把尺寸同步给 Loader。
                //
                // ⚠️ 两个坑一起踩过，这里都绕开了：
                //   1) Loader 不会把自身尺寸让给被加载项 → 必须显式 width/height，
                //      否则矩形算出 0 尺寸，表现是"框还在、字没了"，QML 一句报错都没有；
                //   2) Binding 是**单向、不回传**的 → 直接把 `width: it.width` 绑到被加载项上，
                //      "被加载项想跟着容器变宽"这件事永远传不回来（表达式把它覆盖掉了）。
                //      所以宽度用 `it.parent ? it.parent.width : ...` 让被加载项**自己往上问**。
                component Btn: Loader {
                    id: ld
                    property var d: ({ l: "", a: "", t: false })
                    property bool wide: false
                    property bool heavy: false
                    sourceComponent: actBtn
                    width: wide ? 196 : (196 - 7) / 2
                    height: 30
                    onLoaded: {
                        // 用 item 而不是 it：Loader 只暴露 item 这个属性，
                        // `it` 不是绑定名，写它会得到 "ReferenceError: it is not defined"
                        // （而且是在 onLoaded 里抛，按钮就那么空着，很难往这上面想）
                        item.d = Qt.binding(function () { return ld.d })
                        item.wide = Qt.binding(function () { return ld.wide })
                        item.heavy = Qt.binding(function () { return ld.heavy })
                    }
                }

                // ── 电源：这三个点下去就不可逆，所以独占整行、描边比别人重 ──
                Column {
                    spacing: 6
                    Text { text: "电源"; color: th.fg4; font.pixelSize: 11 }
                    Repeater {
                        model: [
                            { l: "锁屏", a: "lock",     t: false },
                            { l: "重启", a: "reboot",   t: false },
                            { l: "关机", a: "shutdown", t: false }
                        ]
                        delegate: Btn { d: modelData; wide: true; heavy: true }
                    }
                }

                // ── 看看：只读，不会动那台机器的状态 ──
                Column {
                    spacing: 6
                    Text { text: "看看"; color: th.fg4; font.pixelSize: 11 }
                    Grid {
                        columns: 2
                        columnSpacing: 7
                        rowSpacing: 7
                        Repeater {
                            model: [
                                { l: "截图",   a: "screenshot",       t: false },
                                { l: "拍一张", a: "camera_snapshot",  t: false },
                                { l: "软件",   a: "process_list",     t: false },
                                { l: "日志",   a: "log_tail",         t: false },
                                { l: "探活",   a: "",                 t: false },
                                { l: "摄像头", a: "camera_list",      t: false },
                                { l: "音量",   a: "",                 t: false },
                                { l: "文件",   a: "",                 t: false }
                            ]
                            delegate: Btn { d: modelData }
                        }
                    }
                }

                // ── 让它做事：会改远端状态；"开/停"成对的刻意排在同一行的左右两格，
                //    这样"这是一组互逆操作"一眼能看出来，而不是在两个位置各找一个 ──
                Column {
                    spacing: 6
                    Text { text: "让它做事"; color: th.fg4; font.pixelSize: 11 }
                    Grid {
                        columns: 2
                        columnSpacing: 7
                        rowSpacing: 7
                        Repeater {
                            model: [
                                { l: "定时",   a: "list_schedules",       t: false },
                                { l: "媒体",   a: "media_list",           t: false },
                                { l: "开录",   a: "camera_record_start",  t: false },
                                { l: "停录",   a: "camera_record_stop",   t: false },
                                { l: "播放",   a: "media_session_start",  t: false },
                                { l: "停播",   a: "media_session_stop",   t: false },
                                { l: "远控开", a: "remote_control_start", t: false },
                                { l: "远控关", a: "remote_control_stop",  t: false }
                            ]
                            delegate: Btn { d: modelData }
                        }
                    }
                    // 通知：和上面那两列按钮不同，它要**凑内容**（形态/标题/正文/时长/播报/紧急），
                    // 所以给整行宽度单独摆一行，而不是挤进 2 列网格当第 9 个。
                    Btn { d: ({ l: "通知", a: "", t: false }); wide: true }
                }

                // （这里原先有个 t:true 的置灰「通知」——"没做出来的能力单独躺着，标明没做"。
                //   2026-10-06 它做出来了，已并进上面的「让它做事」分组，不再单独躺着。）

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

                // 稿 .logbox：贴右栏底部、上边一条分隔线、里面放最近几条回执。
                // 原来这儿是个"最近一次结果"的框：一是缺那条分隔线（右栏动作和结果糊成一片），
                // 二是概览页明明已经在攒 results 流水，这边却只留一条 —— 同样的数据存了两处。
                Column {
                    Layout.fillWidth: true
                    spacing: 9
                    topPadding: 12

                    Rectangle { width: parent.width; height: 1; color: th.sepline }

                    Repeater {
                        model: root.results.slice(0, 3)
                        delegate: Row {
                            spacing: 9
                            width: parent.width
                            Text {
                                text: modelData.ok ? "✓" : "✕"
                                color: modelData.ok ? th.inv : th.op
                                font.pixelSize: 11
                            }
                            Text {
                                width: parent.width - 30
                                text: modelData.uid + " · " + modelData.action
                                color: modelData.ok ? th.op : th.fg3
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        visible: root.results.length === 0
                        text: "还没有操作"
                        color: th.fg4
                        font.pixelSize: 11
                    }
                }   // 回执流水（logbox）
            }       // 右栏动作（ColumnLayout）
            }       // 三栏 RowLayout
            }       // 控制页 Item

            // ══ 2 设置 ══
            // 稿 03：pad + 两列卡片（账户 / 连接 / 提醒 / 外观）+ 通栏「关于」。
            // 现在只有「连接」「外观」两张是真的：前者读运行时状态，后者深浅切换确实做了。
            // 账户、提醒没有数据源 —— 规则要求"不假装能用"，所以卡片位置留着、内容留白，
            // 而不是像以前那样写一句「尚未接入（无数据源）」的开发说明（规则 1）。
            Item {
                Rectangle { anchors.fill: parent; color: th.body }

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 14

                    // ── 账户（没源；不编）──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 14
                            Text { text: "账户"; color: th.fg3; font.pixelSize: 11 }
                            Item {
                                width: parent.width
                                height: 64
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "还没有账户"
                                        color: th.op; font.pixelSize: 12
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "登录后显示"
                                        color: th.fg3; font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }

                    // ── 连接（真数据：运行时读，不写硬可能存在编造的值）──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 14
                            Text { text: "连接"; color: th.fg3; font.pixelSize: 11 }
                            // 稿 .kv：键 70px 辅助色，值 12px 操作色
                            Column {
                                width: parent.width
                                spacing: 6
                                Row {
                                    Text { text: "云端"; color: th.fg3; font.pixelSize: 12; width: 70 }
                                    Text { text: backend.cloudUrl; color: th.op; font.pixelSize: 12 }
                                }
                                Row {
                                    Text { text: "状态"; color: th.fg3; font.pixelSize: 12; width: 70 }
                                    Text {
                                        // 术语换日常词：不说"已连接/未连接"以外的协议词（规则 2）
                                        text: backend.connected ? "连接正常" : "连不上云端"
                                        color: th.op; font.pixelSize: 12
                                    }
                                }
                                // 网站账号（2026-10-06，OAuth 一户通）：账号过期/没登录时要能看见原因，
                                // 并且能当场点一下重新登录 —— 不能让用户去翻日志才知道"票过期了"。
                                Row {
                                    Text { text: "账号"; color: th.fg3; font.pixelSize: 12; width: 70 }
                                    Text {
                                        id: acctText
                                        text: backend.accountText
                                        color: backend.accountBusy ? th.fg3 : th.op
                                        font.pixelSize: 12
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                }
                                // 只在"没登录 / 失败了"这个可点的时候才出现，已登录时界面不该多一个按钮
                                MouseArea {
                                    visible: !backend.accountBusy && backend.accountText.indexOf("未登录") === 0
                                    height: visible ? 26 : 0
                                    width: parent.width
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.loginWithSite()
                                    Text {
                                        anchors.centerIn: parent
                                        text: "点这里用网站账号登录"; color: th.op; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }

                    // ── 提醒（未实现）──
                    // QML 的 Rectangle 不支持虚线边框，所以沿用右栏「通知」按钮那套替代语言：
                    // 极淡描边 + 禁用级灰（#3A3A3A）—— 一眼看得出是一行还没做的东西。
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 14
                            Text { text: "提醒"; color: th.fg3; font.pixelSize: 11 }
                            Column {
                                width: parent.width
                                spacing: 10
                                Row {
                                    spacing: 10
                                    Rectangle {
                                        width: 20; height: 20; radius: 999
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: "transparent"
                                        border.color: th.line; border.width: 1
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "操作完成时提示"
                                        color: th.fg4; font.pixelSize: 12
                                    }
                                }
                                Row {
                                    spacing: 10
                                    Rectangle {
                                        width: 20; height: 20; radius: 999
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: "transparent"
                                        border.color: th.line; border.width: 1
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "设备离线时提醒"
                                        color: th.fg4; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }

                    // ── 外观（真：深浅切换在这版已经可用）──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 14
                            Text { text: "外观"; color: th.fg3; font.pixelSize: 11 }
                            // 稿 .pick：等宽两块，选中的那块边框走反白
                            RowLayout {
                                width: parent.width
                                spacing: 10
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    radius: th.rCtrl
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
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.darkMode = true
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    radius: th.rCtrl
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
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.darkMode = false
                                    }
                                }
                            }
                        }
                    }

                    // ── 课表编辑器（2026-10-06 并入）──
                    // 风格与「外观」等卡片一致：黑白、自绘按钮（不用 QtQuick.Controls 默认 Button，
                    // 那个默认蓝会把黑白界面破掉）。
                    Rectangle {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Layout.preferredHeight: 108
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            Text { text: "课表编辑器"; color: th.fg3; font.pixelSize: 11 }
                            Text {
                                text: "排课、时间轴、科目管理与多周轮换（数据格式兼容 ClassIsland）"
                                color: th.op; font.pixelSize: 12
                                wrapMode: Text.Wrap
                                width: parent.width
                            }
                            // 自绘按钮：反白风格与稿稿一致
                            Rectangle {
                                width: 132
                                height: 32
                                radius: th.rCtrl
                                color: hovered ? th.hover : th.cream
                                border.color: th.stroke
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "打开课表编辑器"
                                    color: th.fg
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: hovered
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: schedEditor.show()
                                }
                            }
                        }
                    }

                    // ── 关于（通栏）──
                    // 版本写「—」而不是以前那句「版本号未注入（构建时未写入）」：
                    // 那句是把构建系统的事写给用户看，犯了规则 1（不写开发说明）。
                    Rectangle {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 6
                            Text { text: "关于"; color: th.fg3; font.pixelSize: 11 }
                            Text { text: "星集控 · Qt 6.8.1"; color: th.op; font.pixelSize: 12 }
                            Row {
                                spacing: 0
                                Text { text: "版本"; color: th.fg3; font.pixelSize: 11; width: 70 }
                                Text { text: "—"; color: th.fg3; font.pixelSize: 11 }
                            }
                            Text {
                                text: "日志 stelarith-viewer-qt/viewer.log"
                                color: th.fg3; font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        // ── 底部标签（与手机端同一套心智）──
        // 令牌表：底标签 42px，顶上一条 #141414 分隔线。
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: th.win

            Rectangle { width: parent.width; height: 1; color: th.sepline }

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
    // 通知弹窗（2026-10-06 加）：被控端的大屏通知早就实现了，只有管理端这个发送方漏了。
    NotifyDialog { id: notifyDlg; theme: root.th }
    // 注意类名是 FilePushDialog 不是 FileDialog：同个文件里要 import QtQuick.Dialogs 拿原生
    // 选文件对话框，两个 FileDialog 撞名 qml 编译期就冲突了，所以弹窗这侧改名（2026-10-03）
    FilePushDialog { id: fileDlg; theme: root.th }

    // 课表编辑器独立窗口（2026-10-06 并入）：设置页按钮 schedEditor.show() 打开
    ScheduleEditor { id: schedEditor; visible: false }

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
        notifyDlg.close()
        if      (which === "software") softwareDlg.open()
        else if (which === "log")      logDlg.open()
        else if (which === "media")    mediaDlg.open()
        else if (which === "sched")    schedDlg.open()
        else if (which === "file")     fileDlg.open()
        else if (which === "notify")   notifyDlg.open()
    }

    // 右侧所有动作按钮的统一入口。以前这段分支散在 Repeater 的 onClicked 里，
    // 分组之后每组的 delegate 都得抄一份 —— 所以抽出来，按钮只管显示。
    // 几个"按文案分支"的特殊成员之所以特殊，是因为它们要开弹窗/配额外参数，
    // 不是简单的 sendAction(action)。
    // ── 能力门控（2026-10-06 契合度）──────────────────────────────
    // 被控端 register 时会把自己真实实现的指令清单（caps.actions）报上来，
    // 云端原样转进 devices 广播，这里按它决定按钮置不置灰。
    // 老版本被控端/老云端不带这个字段 → capActions 是空的 → 一律放行：
    // 门控的目的是"少让人白点一下"，不是拿它当闸门把老机器锁死。
    function capOk(a) {
        if (!a || backend.capActions.length === 0) return true
        return backend.capActions.indexOf(a) >= 0
    }
    // 几个 a 为空、实际走分支的按钮（音量/软件/定时/文件/通知/探活），真正依赖哪几条
    // 指令得单独点名 —— 按钮上写的 a 是空串，光看 d.a 拦不住它们。
    // 写成数组：像「软件」这种要同时拿到候选列表**和**进程列表才算真能开，单看一条会漏。
    readonly property var capByLabel: ({
        "音量": ["set_volume"],
        "软件": ["list_shortcut_candidates", "process_list"],
        "定时": ["list_schedules"],
        "文件": ["file_push"],
        "通知": ["notify"],
        "探活": ["ping"]
    })
    /** 这个按钮要哪些指令才点得动（[d.a] 或按文案取到的数组）。没声明 = 不参与门控。 */
    function capNeedOf(d) {
        return (d.a !== "" && d.a !== undefined) ? [d.a] : (capByLabel[d.l] || []);
    }
    /** 全部具备才放行。空数组 = 这条按钮不归门管。 */
    function capOkAll(d) {
        const need = capNeedOf(d);
        return need.length === 0 ? true : need.every(capOk);
    }

    function runAction(d) {
        if (d.t) return                      // 未实现的按钮：压根不该点得到

        // 能力门控兜底：按钮已经置灰了，但状态行/快捷键/老界面可能绕过 runAction，
        // 这里统一再拦一次，拦下就给一句话（reportUnsupported 里带日志，不静默）。
        if (!capOkAll(d)) {
            const need = capNeedOf(d).filter(function (x) { return !capOk(x) });
            backend.reportUnsupported(d.l, need.join(" / "));
            return;
        }

        if (d.l === "音量") {
            volumeDlg.open()
        } else if (d.l === "探活") {
            backend.sendPing()
        } else if (d.l === "软件") {
            // 软件弹窗要两块数据：可打开（选自这台机器）+ 正在运行
            backend.sendAction("list_shortcut_candidates")
            backend.sendAction("process_list")
        } else if (d.l === "定时") {
            // 先拉一次，回执到了（onResultReceived）再开弹窗 —— 免得开出来是空的
            backend.sendAction("list_schedules")
            openOnly("sched")
        } else if (d.l === "文件") {
            openOnly("file")
        } else if (d.l === "通知") {
            // 通知不是"点一下就发"——它要凑形态/标题/正文/时长/播报，所以开弹窗再发。
            openOnly("notify")
        } else if (d.l === "远控开") {
            backend.sendAction("remote_control_start", { "fps": 20 })
        } else if (d.a !== "") {
            backend.sendAction(d.a, {})
        }
    }
}
