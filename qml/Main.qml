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
import "components"        // 通用组件：Card / Btn / EmptyState / ToggleRow（2026-10-06 打磨抽出）
import "NotifyParams.js" as NotifyParams

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
    // 字体兜底：没显式写 font.pixelSize 的文字走这一档（"正"）。
    // 页面里该显式写还是要显式写 —— 这里只是保证"漏写"不会掉回系统默认的小字。
    font.pixelSize: 13

    // 关闭 = 隐藏到托盘（后台常驻；main.cpp 已 setQuitOnLastWindowClosed(false)，
    // 只有托盘菜单「退出管理端」才真正退出）
    onClosing: (close) => {
        close.accepted = false
        root.hide()
    }

    // ── 主题令牌（黑白默认黑；浅色为备选）──
    // 页面里一律从 th 取色，不写死 —— 否则换主题必花。
    property bool darkMode: true

    readonly property var darkTh: ({
        // ⚠️ 字号/对比度按 WCAG 定的：正文类文字对底色的对比度必须 ≥ 4.5:1（WCAG AA），
        //    弱化文字（fg4）也不低于 4:1 —— 它是"次要"，不是"看不清"。
        //    2026-10-07 实测旧值：fg3 #5A5A5A 只有 2.87:1、fg4 #3A3A3A 只有 1.74:1
        //    （一个低于 AA、一个连大字号 3:1 都不到）⇒ 小字在暗底上基本糊掉。
        win: "#0A0A0A", body: "#080808", panel: "#101010",
        cream: "#141414", hover: "#1E1E1E", seg: "#121212",
        hover2: "#111111", line: "#202020", canvas: "#1A1A1A",
        stroke: "#333333", stroke2: "#2A2A2A", todo: "#191919",
        card: "#242424", sepline: "#1E1E1E",
        fg: "#FAFAFA", op: "#C8C8C8", fg3: "#8A8A8A",
        fg4: "#707070", inv: "#F0F0F0", ph: "#6E6E6E",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var lightTh: ({
        win: "#F6F6F6", body: "#FFFFFF", panel: "#FFFFFF",
        cream: "#EDEDED", hover: "#E6E6E6", seg: "#E9E9E9",
        hover2: "#EFEFEF", line: "#D4D4D4", canvas: "#DCDCDC",
        stroke: "#B4B4B4", stroke2: "#ACACAC", todo: "#CCCCCC",
        card: "#CCCCCC", sepline: "#D0D0D0",
        fg: "#161616", op: "#3C3C3C", fg3: "#6E6E6E",
        fg4: "#808080", inv: "#1A1A1A", ph: "#8A8A8A",
        rWin: 16, rCard: 12, rCtrl: 8
    })

    readonly property var th: darkMode ? darkTh : lightTh

    // 界面自身的状态（只跟显示有关的东西）
    property int page: 1                 // 0 概览 / 1 控制 / 2 集控 / 3 设置
    property int volumeValue: 30
    // 远端确认过的音量（set_volume 回执里的 data.volume）。-1 = 还没拿到，就别往界面上写。
    // 刻意不用滑块那个值：root.volumeValue 只是本机拖动出来的数字，被控端根本没确认过，
    // 拿它冒充"教室机现在音量多少"就是假装成功了。
    property int remoteVolume: -1
    property var results: []          // 概览页的「最近回执」流水（最多 20 条，只收真回执）
    property var candidates: []       // 软件弹窗的「可打开」候选（来自被控端 list_shortcut_candidates）

    // ── 灵动岛的状态源（设计文档 3.8）────────────────────────────────
    // 状态归主界面算：设备表、回执、文件读数都在 C++ 那边，组件不该猜。
    // 每一项都对应一个真实存在的信号，缺数据源的一档（语音）不硬凑 —— 宁可不亮，
    // 也不摆一个永远空的胶囊在那儿冒充"聚合器"。
    property var islandStates: []      // [{ k, t, d, p }]，k ∈ offline/command/file/alert/monitor
    // 批量指令：下发 +1、回执 -1（4.5「进度可见」）。老师盯着数往上走，
    // 不让他对着一屋子设备猜"这会儿发到哪台了"。
    property var batch: ({ label: "", total: 0, done: 0, ok: 0, fail: 0, active: false })
    // 没处理的告警（4.3 场景四）：回执失败、文件没推成都算，攒起来给灵动岛。
    property var alerts: []
    // 全局提示（5.6 Toast）。见 toast() 上的注释：原来只有集控页内一份 hint。
    property string toastText: ""
    property int toastTick: 0
    // 多选的设备 uid（4.4）。和 currentUid 是两套东西：那个管"看哪台"，
    // 这个管"给哪些台发" —— 混在一处会互相踢（台数变了不知道该信哪个）。
    property var picked: []
    property int pickAnchor: -1       // Ctrl 点下的第一台，给 Shift 范围选当端点

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

            // 批量进度 / 告警 / 灵动岛：都挂在最后，别嵌进上面那些分支里 ——
            // 一条回执可能既是"批量的一台"又是"软件列表"，分着走才不会互相踩。
            if (batch.active) root.batchStep(uid, result === "done", error || "")
            if (result !== "done") root.pushAlert(uid, action, error || detail || "")
            root.refreshIsland()
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
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                Item { Layout.fillWidth: true }

                // ── 顶栏右侧：连接状态 + 账户/身份入口 ──
                // 2026-10-06 重写：原来这儿是「控制 ┃ 被控」一个分段控件 ——
                // 那是开发期用来"假装自己是被控端"的自检开关（被控端是另一台机器上另一个程序，
                // 这台机器上切它没有任何效果），留在外面就是个说不清来路的死开关。
                // 换成账户入口后，顶栏这一行讲的全是"谁在用、连没连上"。
                RowLayout {
                    spacing: 14

                    RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 5; height: 5; radius: 3
                            color: backend.connected ? th.inv : th.fg4
                        }
                        Text {
                            text: backend.connected
                                  ? (devices.length + " 台在线")
                                  : (backend.accountBusy ? "正在登录…"
                                     : (backend.loggedIn ? "没连上云端" : "没登录"))
                            color: th.fg3
                            font.pixelSize: 12
                            // 顶栏这一句必须能点：没登录/断开了的时候，用户第一件该做的事
                            // （去登录）不该藏在设置页第三张卡里。
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!backend.loggedIn && !backend.accountBusy)
                                        backend.loginWithSite()
                                    else
                                        root.page = 3
                                }
                            }
                        }
                    }

                    // 胶囊尺寸问容器（Item 默认 0×0，不给尺寸就是"顶栏右侧空一块"）
                    AccountMenu {
                        id: acctMenu
                        theme: root.th
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 28
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
                                Text { text: modelData.k; color: th.fg3; font.pixelSize: 12 }
                                Text {
                                    text: modelData.v
                                    color: th.fg; font.pixelSize: 20; font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    // ── 今日课表（2026-10-06 补上：读真机 ClassIsland 档案，不再空）──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 148
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8
                            // ⚠️ 这里原来是 Row + 子项 anchors.right —— Qt 每次启动刷两条
                            //    "Row: Cannot specify left/right/horizontalCenter/fill/centerIn
                            //     anchors for items inside Row. Row will not function."
                            //    Row 内部子项只能用 x/spacing 定位；要左右分栏就得用 Item（2026-10-06 修）。
                            Item {
                                width: parent.width
                                height: 16
                                Text {
                                    text: "今日课表"
                                    color: th.fg; font.pixelSize: 13; font.weight: Font.Medium
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Qt.formatDate(new Date(), "MM月dd日 dddd")
                                    color: th.fg3; font.pixelSize: 12
                                }
                            }
                            // 课程列表（scheduleToday.rows：JSON 数组）
                            ListView {
                                width: parent.width
                                height: parent.height - 30
                                clip: true
                                model: scheduleToday.rows
                                delegate: Rectangle {
                                    width: parent.width
                                    height: 30
                                    color: modelData.isNow ? th.cream : "transparent"
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: 8; spacing: 12
                                        Text {
                                            text: modelData.time
                                            color: modelData.isNow ? th.fg : th.fg3
                                            font.pixelSize: 12; width: 88
                                        }
                                        Text {
                                            text: modelData.subject
                                            color: modelData.isNow ? th.fg
                                                 : (modelData.isPast ? th.fg4 : th.fg)
                                            font.pixelSize: 13
                                            font.weight: modelData.isNow ? Font.Medium : Font.Normal
                                        }
                                    }
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

                                // Row 里不许给子项设 right/fill 之类的 anchors（会报 "Row will not
                                // function"）；左右分栏用 Item，同「今日课表」标题行（2026-10-06 修）。
                                Item {
                                    width: parent.width
                                    height: 16
                                    Text {
                                        text: "在线设备"
                                        color: th.fg; font.pixelSize: 13; font.weight: Font.Medium
                                    }
                                    Text {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.devices.length + " 台"
                                        color: th.fg3; font.pixelSize: 12
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
                                                color: th.fg; font.pixelSize: 14
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: (modelData.framesIn || 0) + " 帧"
                                                color: th.fg3; font.pixelSize: 12
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
                                            color: th.op; font.pixelSize: 13
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "屏幕上的机器会自动出现"
                                            color: th.fg3; font.pixelSize: 12
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
                                    color: th.fg; font.pixelSize: 13; font.weight: Font.Medium
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
                                            font.pixelSize: 12
                                            width: 42
                                        }
                                        Text {
                                            text: modelData.ok ? "✓" : "✕"
                                            color: th.fg
                                            font.pixelSize: 13
                                            width: 12
                                        }
                                        Text {
                                            width: parent.width - 76
                                            text: modelData.uid + " · " + modelData.action
                                                  + (modelData.ok ? ""
                                                        : (modelData.err ? "（" + modelData.err + "）" : ""))
                                            color: modelData.ok ? th.op : th.fg3
                                            font.pixelSize: 13
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
                                            color: th.op; font.pixelSize: 13
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "点了右边的动作，结果记在这儿"
                                            color: th.fg3; font.pixelSize: 12
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
                Layout.preferredWidth: 160
                // 硬上限。RowLayout 里"没写 preferredWidth 的项"会被内容顶大，
                // 而画面是靠 fillWidth 吃剩余的 —— 谁被顶大，画面就少一截。
                Layout.maximumWidth: 184
                Layout.fillHeight: true
                spacing: 2

                Text {
                    text: "设备"
                    color: th.fg3
                    font.pixelSize: 12
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

                        // 状态点：在线亮、离线暗。以前这一颗恒亮，掉线的机器也画成"在线"，
                        // 老师按着它发指令，等半天没回音 —— 先把真假摆出来。
                        // ⚠️ 只信云端给的 online 字段；老版本云端不带这个键时按在线处理
                        // （`=== false` 才判离线），免得整个列表一夜之间全灰掉。
                        property bool devOnline: (modelData.online !== false)

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 9
                            Rectangle {
                                width: 5; height: 5; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: devOnline ? th.inv : th.fg4
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.uid
                                color: cur ? th.fg : (devOnline ? th.op : th.fg4)
                                font.pixelSize: 14
                                font.weight: cur ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                // 右侧那行小字要留位置，别让长机器名压上去
                                width: Math.max(60, 168 - 10 - 5 - 9 - devMeta.width - 16)
                            }
                        }

                        // 右端一行小字：在线显示被控端版本，离线显示"多久没见"。
                        // 全部取自云端真字段（version / lastSeenAgoSec），没有就不画 —— 不编数据。
                        Text {
                            id: devMeta
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: !devOnline
                                  ? ((modelData.lastSeenAgoSec !== undefined
                                      && modelData.lastSeenAgoSec >= 0)
                                     ? (modelData.lastSeenAgoSec < 60 ? "刚断线"
                                        : Math.floor(modelData.lastSeenAgoSec / 60) + " 分前")
                                     : "离线")
                                  : (modelData.version !== undefined && modelData.version !== ""
                                     ? modelData.version : "")
                            color: th.fg4
                            font.pixelSize: 12
                        }

                        // ∠ 选中态（4.4）：选中的那台描边反白 + 右端一个小勾，
                        //   不加这个反馈就等于让老师猜"到底选上没"。
                        property bool picked: (root.picked.indexOf(modelData.uid) >= 0)
                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: picked ? th.cream : "transparent"
                            border.color: picked ? th.fg4 : "transparent"
                            border.width: picked ? 1 : 0
                        }
                        MouseArea {
                            id: devHover
                            anchors.fill: parent
                            hoverEnabled: true
                            // 多选交给 root：按钮位置要拿 mods 和 index，那是主界面才知道的事
                            onClicked: (mouse) => root.pickDevice(modelData.uid, mouse.modifiers, index)
                        }
                    }
                }

                // 已选 N 台 + 批量动作（4.4 多选 / 4.5 批量三段式）
                // ⚠️ 刻意放在**左栏设备列表下面**，不动右栏：右栏那列是固定高的一列，
                //    塞进去只会把底部标签栏顶出去（见文件头 minimumHeight 的注释）。
                Column {
                    visible: root.picked.length > 0
                    width: 168
                    spacing: 6
                    Text {
                        text: "已选 " + root.picked.length + " 台"
                        color: th.fg
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    // 批量按钮沿用右栏 ActBtn 的外观（同高 30、同圆角 rCtrl）：
                    // 同一屏里一个方块一个方块地漂，比样式不统一更容易看出不对。
                    Repeater {
                        model: [
                            { l: "锁屏选中的", a: "lock" },
                            { l: "关机选中的", a: "shutdown" }
                        ]
                        delegate: Rectangle {
                            id: batBtn
                            width: 168; height: 30; radius: th.rCtrl
                            color: batMa.containsMouse ? th.hover2 : "transparent"
                            border.color: th.stroke
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: modelData.l
                                color: th.op
                                font.pixelSize: 13
                            }
                            MouseArea {
                                id: batMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.runOnPicked(modelData.a)
                            }
                        }
                    }
                    Text {
                        text: "取消选择（Esc）"
                        color: th.fg4
                        font.pixelSize: 12
                        MouseArea { anchors.fill: parent; onClicked: root.clearPick() }
                    }
                }

                Item { width: 1; height: 14 }
                Rectangle { width: 148; height: 1; color: th.line; x: 10 }
                Text {
                    text: "全校"
                    color: th.fg3
                    font.pixelSize: 12
                    leftPadding: 10
                    topPadding: 12
                }
            }

            // ───── 中：画面（主体）─────
            ColumnLayout {
                // 主体：窗口有多宽，画面就吃多少（左上 badge 那行也在这栏里）。
                Layout.fillWidth: true
                Layout.preferredWidth: 640
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: root.currentUid === "" ? "未选择设备" : root.currentUid
                        color: th.fg
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }
                    Text {
                        visible: root.currentUid !== ""
                        text: Math.round(backend.fps * 10) / 10 + " fps · "
                              + Math.round(backend.lastFrameBytes / 1024) + " KB"
                        color: th.fg3
                        font.pixelSize: 12
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
                            color: backend.frameSource === "rtc" ? th.win
                                 : th.fg3
                            // 稿上没有 10px 这一档（最小是"注 11"），徽标字太小会把
                            // "实时/轮询"变成需要凑近看的装饰 —— 统一提到 11。
                            font.pixelSize: 12
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
                            font.pixelSize: 13
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.currentUid === "" ? "在左边选一台"
                                : root.linkBusy ? "连上就有画面"
                                : "这台机器还没发来第一帧"
                            color: th.fg3
                            font.pixelSize: 12
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
                        // 固定灰，刻意不跟主题走：这行浮在**远程画面**上，底色是对方的桌面（不可控），
                        // 用主题色反而可能在浅色画面上消失。
                        color: "#8A8A8A"
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
                        color: th.fg3; font.pixelSize: 12
                    }
                    Text {
                        visible: root.currentUid !== ""
                        text: backend.connected ? "连接正常" : "连不上云端"
                        color: th.fg3; font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // 只显示被控端确认过的音量，没确认过就空着（见 remoteVolume 注释）
                        visible: root.remoteVolume >= 0
                        text: root.remoteVolume + "%"
                        color: th.fg3; font.pixelSize: 12
                    }
                }
            }

            // ───── 右：动作（配角）─────
            // 以前 20 个按钮平铺在一个网格里，找"关机"要把整栏扫一遍，
            // 而它旁边就坐着"截图"——误点代价和它完全不在一个量级。
            // 现在按「点了会怎样」分三组：**电源**（不可逆，独占整行、视觉更重）
            // / **看看**（只读，安全）/ **让它做事**（会改远端状态，成对的一左一右）。
            ColumnLayout {
                // 2 列按钮（120+10+120=250）＋输入框＋回执条，280 够、300 封顶。
                // 实测这栏曾被内容顶到 657，画面被压成 299 宽的竖条 —— 就是"留给画面的区域太少"。
                Layout.preferredWidth: 280
                Layout.maximumWidth: 300
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
                    // d = { l 文案, a action, t 未实现, p 权限（"admin" = 只管理员）}；wide/heavy 由使用处注入
                    // p 是**权限门控**（是不是管理员），和 capOk 那套（这台机器认不认这条指令）
                    // 是两件事，别合并：前者看人，后者看机器。
                    property var d: ({ l: "", a: "", t: false, p: "" })
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
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: ma
                            anchors.fill: parent
                            // t = 这功能压根没做；capOk = 这台被控端上报的能力清单里认不认这条指令；
                            // permOk = 当前身份（管理员/教师）做不做得了。
                            // 三者都置灰：没做的留白、不支持的照样看得见（灰着），
                            // 没权限的也灰着 —— 但 tooltip 得说清"为什么灰"（下面那段），
                            // 否则灰按钮看起来跟坏了的一样。
                            enabled: !d.t && capOkAll(d) && permOk(d.p)
                            hoverEnabled: !d.t && capOkAll(d)
                            // 以前鼠标移上去还是箭头，看不出这东西能点
                            cursorShape: d.t ? Qt.ArrowCursor
                                        : (capOkAll(d) && permOk(d.p)) ? Qt.PointingHandCursor
                                        : Qt.ForbiddenCursor
                            onPressed: pressed2 = true
                            onReleased: pressed2 = false
                            onCanceled: pressed2 = false
                            onClicked: root.runAction(d)
                        }

                        ToolTip {
                            // 不支持 / 没权限的也显示 tooltip（不然灰按钮 Why 都不问，像坏了），
                            // 文案分五种情况：不可逆 / 没选机器 / 这台机器不支持 / 没权限 / 正常
                            visible: ma.containsMouse && (!d.t || !capOkAll(d) || !permOk(d.p))
                            delay: 500
                            text: !permOk(d.p) ? "要管理员身份才做得到（顶栏账户那里能切）"
                                : heavy ? "点了立刻执行，没法撤销"
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
                //
                // ⚠️ 这个文件里**不许**再把内联组件起名为 Btn：
                // 内联组件的声明作用域是**整个文档**，不是"写在哪个 Column 里"——
                // 曾经这里叫 `component Btn: Loader`，于是把 import "components" 带来的
                // 真·通用按钮（components/Btn.qml）整篇遮蔽掉：右栏之外所有 Btn 都变成了
                // 这个 Loader，没有 onClicked 信号，设置页一加载就报
                // "Cannot assign to non-existent property onClicked"，整页白屏。
                // 右栏这套自己用 ActBtn（Action 的缩写），和公共组件 Btn 各归各位。
                component ActBtn: Loader {
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
                    Text { text: "电源"; color: th.fg4; font.pixelSize: 12 }
                    Repeater {
                        model: [
                            { l: "锁屏", a: "lock",     t: false, p: "admin" },
                            { l: "重启", a: "reboot",   t: false, p: "admin" },
                            { l: "关机", a: "shutdown", t: false, p: "admin" }
                        ]
                        delegate: ActBtn { d: modelData; wide: true; heavy: true }
                    }
                }

                // ── 看看：只读，不会动那台机器的状态 ──
                Column {
                    spacing: 6
                    Text { text: "看看"; color: th.fg4; font.pixelSize: 12 }
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
                            delegate: ActBtn { d: modelData }
                        }
                    }
                }

                // ── 让它做事：会改远端状态；"开/停"成对的刻意排在同一行的左右两格，
                //    这样"这是一组互逆操作"一眼能看出来，而不是在两个位置各找一个 ──
                Column {
                    spacing: 6
                    Text { text: "让它做事"; color: th.fg4; font.pixelSize: 12 }
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
                                { l: "远控开", a: "remote_control_start", t: false, p: "admin" },
                                { l: "远控关", a: "remote_control_stop",  t: false, p: "admin" }
                            ]
                            delegate: ActBtn { d: modelData }
                        }
                    }
                    // 通知：和上面那两列按钮不同，它要**凑内容**（形态/标题/正文/时长/播报/紧急），
                    // 所以给整行宽度单独摆一行，而不是挤进 2 列网格当第 9 个。
                    ActBtn { d: ({ l: "通知", a: "", t: false }); wide: true }
                    // 远程终端：和通知一样单独占一行（full width）。它开着就是一条能跑任意命令的
                    // 通道，得和普通"点一下就发"的按钮区分开，别挤进 2 列网格里混过去。
                    ActBtn { d: ({ l: "终端", a: "", t: false, p: "admin" }); wide: true }
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
                        font.pixelSize: 13
                        selectByMouse: true
                        clip: true
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: typeInput.text === ""
                            text: "打字发给教室机，回车"
                            color: th.fg4
                            font.pixelSize: 13
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
                    // Column 是定位器，没有 padding：想要"离上面远点"得走 Layout 的边距
                    Layout.topMargin: 12

                    Rectangle { width: parent.width; height: 1; color: th.sepline }

                    Repeater {
                        model: root.results.slice(0, 3)
                        delegate: Row {
                            spacing: 9
                            width: parent.width
                            Text {
                                text: modelData.ok ? "✓" : "✕"
                                color: modelData.ok ? th.inv : th.op
                                font.pixelSize: 12
                            }
                            Text {
                                width: parent.width - 30
                                text: modelData.uid + " · " + modelData.action
                                color: modelData.ok ? th.op : th.fg3
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        visible: root.results.length === 0
                        text: "还没有操作"
                        color: th.fg4
                        font.pixelSize: 12
                    }
                }   // 回执流水（logbox）
            }       // 右栏动作（ColumnLayout）
            }       // 三栏 RowLayout
            }       // 控制页 Item

            // ══ 2 集控（2026-10-06：web console 核心功能并入管理端）══
            // 通知下发 / 定时任务 / 广播 —— 全部走 backend.sendAction 到云端 → 被控端。
            Item {
                id: consolePage
                Rectangle { anchors.fill: parent; color: th.body }

                // 操作结果提示（简单 toast）
                property string hintText: ""
                Timer { id: hintTimer; interval: 2500; onTriggered: consolePage.hintText = "" }
                function hint(s) { consolePage.hintText = s; hintTimer.restart() }
                Text {
                    anchors.top: parent.top; anchors.topMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: consolePage.hintText
                    // 黑白稿里没有彩色，"更亮/反白"就是唯一的强调手段。
                    // 原来是橙色 #e0a03a —— 全界面唯一一处彩色，且违反了本仓 UI 规范。
                    color: th.inv
                    font.pixelSize: 13
                    visible: consolePage.hintText !== ""
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 18
                    contentHeight: col.height
                    clip: true
                    Column {
                        id: col
                        width: parent.width
                        spacing: 14

                        // ── 通知下发 ──
                        // ⚠️ 2026-10-06 修：这张卡以前高度塌成 0（探针量到 cards=0/0/0，集控页整页空白）。
                        // 元凶是内层 Column 写 anchors.fill: parent + anchors.margins:16 ——
                        // 它要父 Rectangle 先有高度，而 Rectangle 在这层 Column 里又靠 implicitHeight
                        // 长高，两边互相等对方先给，最后都是 0。整整一个页面看起来像"没做完"。
                        // 现在：内层只锚 left/right/top（**绝不锚 bottom**，锚了等于把高度又绑回父），
                        // 高度自己由内容决定；外层显式 height = 内容 + 上下 16 留白。
                        Rectangle {
                            width: parent.width
                            height: ntCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: ntCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10

                                // 参数怎么拼由 NotifyParams.js 定（那边有 scripts/test-notify-params.mjs 的 16 条断言），
                                // 这里只负责收集 + 显示，绝不自己再拼一份 —— 之前界面自己拼了一份：
                                // tts 写在 params 顶层（被控端只读 flags.speech）、severity 不管什么形态都带
                                // （被控端只在全屏时读它）。结果就是「朗读」和「紧急/重要」点了等于没点。
                                property string ntKind: "popup"        // popup 居中弹窗 / island 灵动岛 / fullscreen 全屏
                                property string ntSeverity: "remind"   // 只有全屏形态下被控端才读
                                property bool ntSpeech: false          // flags.speech
                                property bool ntEmergency: false       // flags.emergency_confirm：不自动关 + 置顶

                                Text { text: "通知下发"; color: th.fg3; font.pixelSize: 12 }

                                Row {
                                    width: parent.width
                                    spacing: 10
                                    InputField {
                                        th: root.th
                                        id: ntTitle
                                        width: parent.width * 0.45
                                        placeholderText: "标题（必填，如：上课啦）"
                                    }
                                    InputField {
                                        th: root.th
                                        id: ntContent
                                        width: parent.width - ntTitle.width - 10
                                        placeholderText: "内容"
                                    }
                                }

                                // 字数：被控端会截（标题 24 / 正文 64），提前说一声，别让人以为漏发了
                                Text {
                                    width: parent.width
                                    font.pixelSize: 12
                                    property bool tOver: NotifyParams.willTruncate(ntTitle.text, NotifyParams.CAP_TITLE)
                                    property bool cOver: NotifyParams.willTruncate(ntContent.text, NotifyParams.CAP_CONTENT)
                                    color: (tOver || cOver) ? th.op : th.fg4
                                    text: (tOver || cOver)
                                          ? ((tOver ? "标题会截到 24 字  " : "") + (cOver ? "正文会截到 64 字" : ""))
                                          : ("标题 " + ntTitle.text.length + "/24    正文 " + ntContent.text.length + "/64")
                                }

                                // 形态（kind）：被控端按 popup / island / fullscreen 三选一，未知值一律回落 popup
                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Text {
                                        text: "形态"
                                        color: th.fg3
                                        font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Repeater {
                                        model: [ { k: "弹窗", v: "popup" }, { k: "灵动岛", v: "island" }, { k: "全屏", v: "fullscreen" } ]
                                        delegate: Rectangle {
                                            width: 64; height: 28; radius: th.rCtrl
                                            color: (ntCol.ntKind === modelData.v) ? th.inv : "transparent"
                                            border.color: th.stroke; border.width: 1
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.k
                                                color: (ntCol.ntKind === modelData.v) ? th.win : th.fg3
                                                font.pixelSize: 12
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: ntCol.ntKind = modelData.v
                                            }
                                        }
                                    }
                                }

                                // 朗读 / 紧急确认 / 停留秒数
                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Rectangle {
                                        width: ntSpeechLab.width + 20; height: 28; radius: th.rCtrl
                                        color: ntCol.ntSpeech ? th.inv : "transparent"
                                        border.color: th.stroke; border.width: 1
                                        Text {
                                            id: ntSpeechLab
                                            anchors.centerIn: parent
                                            text: "朗读"
                                            color: ntCol.ntSpeech ? th.win : th.fg3
                                            font.pixelSize: 12
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: ntCol.ntSpeech = !ntCol.ntSpeech
                                        }
                                    }
                                    Rectangle {
                                        width: ntEmgLab.width + 20; height: 28; radius: th.rCtrl
                                        color: ntCol.ntEmergency ? th.inv : "transparent"
                                        border.color: th.stroke; border.width: 1
                                        Text {
                                            id: ntEmgLab
                                            anchors.centerIn: parent
                                            text: "紧急确认（不自动关 + 置顶）"
                                            color: ntCol.ntEmergency ? th.win : th.fg3
                                            font.pixelSize: 12
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: ntCol.ntEmergency = !ntCol.ntEmergency
                                        }
                                    }
                                    InputField {
                                        th: root.th
                                        id: ntSeconds
                                        width: 150
                                        placeholderText: "秒数（空=按字数自适应）"
                                        validator: IntValidator { bottom: 0; top: 3600 }
                                    }
                                }

                                // 严重度：被控端只在 fullscreen 下读它，别的形态带了也是被忽略 ——
                                // 所以非全屏时把它置灰并写明原因，而不是让人选了半天发现没变化。
                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Text {
                                        text: (ntCol.ntKind === "fullscreen") ? "严重度" : "严重度（只有全屏才生效）"
                                        color: th.fg3
                                        font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Repeater {
                                        model: [ { k: "普通", v: "remind" }, { k: "重要", v: "inform" }, { k: "紧急", v: "urgent" } ]
                                        delegate: Rectangle {
                                            width: 64; height: 28; radius: th.rCtrl
                                            property bool on: (ntCol.ntKind === "fullscreen") && (ntCol.ntSeverity === modelData.v)
                                            opacity: (ntCol.ntKind === "fullscreen") ? 1.0 : 0.35
                                            color: on ? th.inv : "transparent"
                                            border.color: th.stroke; border.width: 1
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.k
                                                color: parent.on ? th.win : th.fg3
                                                font.pixelSize: 12
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                enabled: ntCol.ntKind === "fullscreen"
                                                onClicked: ntCol.ntSeverity = modelData.v
                                            }
                                        }
                                    }
                                }

                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Btn {
                                        theme: th
                                        width: 160
                                        text: root.picked.length > 1
                                              ? ("发送到选中的 " + root.picked.length + " 台")
                                              : "发送到选中设备"
                                        onClicked: {
                                            const r = NotifyParams.buildNotifyParams({
                                                kind: ntCol.ntKind,
                                                title: ntTitle.text,
                                                content: ntContent.text,
                                                seconds: ntSeconds.text,
                                                speech: ntCol.ntSpeech,
                                                severity: ntCol.ntSeverity,
                                                emergency: ntCol.ntEmergency
                                            })
                                            if (!r.ok) { root.toast(r.error); return }
                                            if (!root.permOk("notify")) { backend.reportDenied("通知"); return }
                                            // 选了多台就按批量走：进度计数 + 结果汇总都交给 root 那套（4.5 三段式）
                                            const list = (root.picked.length > 0)
                                                         ? root.picked.slice()
                                                         : (backend.currentUid ? [backend.currentUid] : [])
                                            if (list.length === 0) { root.toast("先在控制页选中一台设备"); return }
                                            root.batchStart("通知", list.length)
                                            const dead = []
                                            for (let i = 0; i < list.length; ++i) {
                                                if (!root.isDevOnline(list[i])) { dead.push(list[i]); continue }
                                                backend.currentUid = list[i]
                                                backend.sendAction("notify", r.params)
                                            }
                                            for (let j = 0; j < dead.length; ++j) root.batchStep(dead[j], false, "设备不在线")
                                        }
                                    }
                                }
                            }
                        }

                        // ── 定时任务 ──
                        // 高度写法同「通知下发」那张卡（2026-10-06 统一修掉 anchors.fill 撑父的塌陷）。
                        Rectangle {
                            width: parent.width
                            height: schedCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: schedCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                property string schedWhat: "schedule_shutdown"
                                Text { text: "定时任务（被控端执行）"; color: th.fg3; font.pixelSize: 12 }
                                Row {
                                    width: parent.width
                                    spacing: 10
                                    Repeater {
                                        model: [ { k: "定时关机", a: "schedule_shutdown" }, { k: "定时重启", a: "schedule_reboot" } ]
                                        delegate: Rectangle {
                                            width: 84; height: 28; radius: th.rCtrl
                                            color: (schedCol.schedWhat === modelData.a) ? th.inv : "transparent"
                                            border.color: th.stroke; border.width: 1
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.k
                                                color: (schedCol.schedWhat === modelData.a) ? th.win : th.fg3
                                                font.pixelSize: 12
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: schedCol.schedWhat = modelData.a
                                            }
                                        }
                                    }
                                    InputField {
                                        th: root.th
                                        id: schedAt
                                        width: 140
                                        placeholderText: "HH:mm"
                                        validator: RegularExpressionValidator { regularExpression: /^([01]\d|2[0-3]):[0-5]\d$/ }
                                    }
                                    Btn {
                                        theme: th
                                        width: 60
                                        text: "设定"
                                        onClicked: {
                                            // 定时关机/重启会把一批教室机排进日程，跟电源键一个量级
                                            if (!backend.mayDo("schedule_shutdown")) {
                                                hint("定时关机/重启要管理员身份");
                                                return
                                            }
                                            const at = schedAt.text.trim()
                                            if (!/^([01]\d|2[0-3]):[0-5]\d$/.test(at)) { hint("时间格式 HH:mm"); return }
                                            const uid = backend.currentUid
                                            if (!uid) { hint("先选中设备"); return }
                                            const now = new Date()
                                            const [h, m] = at.split(":").map(Number)
                                            const target = new Date(now.getFullYear(), now.getMonth(), now.getDate(), h, m)
                                            if (target <= now) target.setDate(target.getDate() + 1)
                                            // 被控端要 ISO 8601（含毫秒）——见 control-qt main.cpp schedule 分支
                                            const iso = target.toISOString()
                                            backend.sendAction(schedWhat, { "at": iso })
                                            hint("已设定 " + at + " → " + uid)
                                        }
                                    }
                                }
                            }
                        }

                        // ── 广播（对所有在线设备）──
                        Rectangle {
                            width: parent.width
                            height: bcastCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: bcastCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                Text { text: "广播（发给所有在线设备）"; color: th.fg3; font.pixelSize: 12 }
                                InputField {
                                    th: root.th
                                    id: bcastText
                                    width: parent.width
                                    placeholderText: "广播内容"
                                }
                                Btn {
                                    theme: th
                                    width: parent.width
                                    text: "广播"
                                    // 真正的事（权限、空内容、逐台下发）都交给 root.broadcastAll()：
                                    // 命令面板上同一件事也得做一遍，抽出来才不会两边漂移
                                    onClicked: root.broadcastAll(bcastText.text)
                                }
                            }
                        }

                        // ── 考试模式（2026-10-06，设计文档 3.7 第一版）──
                        // 对当前选中设备下发 exam_mode（全屏拦截 + 白名单轮询 + 倒计时）。
                        // 参数：minutes 时长（0 = 不自动结束）、whitelist 允许保留的程序名列表。
                        Rectangle {
                            width: parent.width
                            height: examCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: examCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                Text { text: "考试模式（全屏拦截 + 白名单进程）"; color: th.fg3; font.pixelSize: 12 }
                                Row {
                                    spacing: 8
                                    InputField {
                                        th: root.th
                                        id: examMinutes
                                        width: 90
                                        placeholderText: "时长(分钟)"
                                        validator: IntValidator { bottom: 0; top: 300 }
                                    }
                                    InputField {
                                        th: root.th
                                        id: examWhitelist
                                        width: 200
                                        placeholderText: "白名单(逗号分隔，如 examclient,notepad)"
                                    }
                                }
                                Row {
                                    spacing: 8
                                    Btn {
                                        theme: th
                                        width: 90
                                        text: "开始考试"
                                        onClicked: {
                                            const uid = backend.currentUid
                                            if (!uid) { hint("先在控制页选中一台设备"); return }
                                            var whitelist = []
                                            examWhitelist.text.split(",").forEach(function(s) {
                                                var t = s.trim()
                                                if (t) whitelist.push(t)
                                            })
                                            backend.sendAction("exam_mode", {
                                                "minutes": parseInt(examMinutes.text, 10) || 0,
                                                "whitelist": whitelist
                                            })
                                            hint("考试模式已下发 → " + uid)
                                        }
                                    }
                                    Btn {
                                        theme: th
                                        width: 90
                                        text: "结束考试"
                                        onClicked: {
                                            const uid = backend.currentUid
                                            if (!uid) { hint("先在控制页选中一台设备"); return }
                                            backend.sendAction("exam_mode_stop", {})
                                            hint("已请求结束考试 → " + uid)
                                        }
                                    }
                                }
                            }
                        }

                        // ── 语音对讲（2026-10-07，设计文档 3.3 第一版）──
                        // 老师→全班单向广播：开麦采集 PCM → 云端 fan-out → 学生端播放。
                        // 安全（3.3.6）：默认静音，只有点"开始讲话"才采集；停止即完全静音。
                        Rectangle {
                            width: parent.width
                            height: speakCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: speakCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                Text { text: "语音对讲（老师讲话，全班听）"; color: th.fg3; font.pixelSize: 11 }
                                Row {
                                    spacing: 8
                                    Button {
                                        text: backend.speaking ? "停止讲话" : "开始讲话"
                                        onClicked: {
                                            if (backend.speaking) {
                                                backend.stopSpeaking()
                                                hint("语音已停止（静音）")
                                            } else {
                                                if (backend.startSpeaking()) {
                                                    hint("🎤 正在讲话，全班可听；再次点击停止")
                                                } else {
                                                    hint("开麦失败：" + backend.speakError)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── 屏幕广播（2026-10-07，设计文档《屏幕广播-第一版设计》）──
                        // 老师屏幕 → 全部在线设备全屏显示（3fps JPEG，60 台可承受）。
                        Rectangle {
                            width: parent.width
                            height: bcastCol2.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: bcastCol2
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                Text { text: "屏幕广播（老师屏幕 → 全部在线设备）"; color: th.fg3; font.pixelSize: 11 }
                                Row {
                                    spacing: 8
                                    Button {
                                        text: backend.broadcasting ? "停止广播" : "开始广播"
                                        onClicked: {
                                            if (backend.broadcasting) {
                                                backend.stopBroadcast()
                                                hint("屏幕广播已停止")
                                            } else {
                                                if (backend.startBroadcast()) {
                                                    hint("📺 正在屏幕广播；再次点击停止")
                                                } else {
                                                    hint("广播启动失败")
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── 班级监控（2026-10-07，设计文档 3.6 第一版）──
                        // 录制列表（被控端 camera_record 录制 → 自动上传云端 recordings/）。
                        Rectangle {
                            width: parent.width
                            height: recCol.height + 32
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: recCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                spacing: 10
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Text { text: "班级监控（录制回放）"; color: th.fg3; font.pixelSize: 11 }
                                    Button {
                                        text: "刷新"
                                        onClicked: {
                                            backend.fetchRecordings()
                                            hint("正在拉取录制列表…")
                                        }
                                    }
                                }
                                // 按设备分组列出录制
                                Repeater {
                                    model: Object.keys(backend.recordings).sort()
                                    delegate: Item {
                                        width: parent.width
                                        height: 40
                                        Row {
                                            spacing: 8
                                            Text { text: modelData; color: th.fg; font.pixelSize: 12; width: 130 }
                                            Text {
                                                text: (backend.recordings[modelData].length || 0) + " 段"
                                                color: th.fg3; font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                                Text {
                                    text: "录制在被控端 camera_record_start 后生成，结束自动上传云端；点上面「刷新」查看。"
                                    color: th.fg3; font.pixelSize: 10; wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }

            // ══ 3 设置 ══
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

                    // ── 账户（真：OAuth 一户通，2026-10-06 打磨）──
                    // 以前这张卡是空的：一行"还没有账户 / 登录后显示"，既不能登录也不能注册，
                    // 用户看到的是"这功能没做完"而不是"我该点哪儿"。现在按状态给出口：
                    //   没登录 → 登录 / 注册 两颗按钮（没登录就是没凭据，连云端也连不上，要说清）
                    //   登录中 → 一句进度
                    //   已登录 → 账号 + 角色 + 换身份
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
                            Text { text: "账户"; color: th.fg3; font.pixelSize: 12 }

                            // 没登录：两颗按钮 + 一句话说清"登录之后会发生什么"
                            Column {
                                visible: !backend.loggedIn
                                width: parent.width
                                spacing: 10
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: backend.accountBusy
                                          ? "正在等浏览器里授权…（授权完会自动接上教室机）"
                                          : "还没登录。点登录会用星璃账号授权，" +
                                            "之后这台机器自动拿到接入票，不用填密钥。"
                                    color: th.op; font.pixelSize: 13
                                }
                                RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    Btn {
                                        Layout.preferredWidth: 120
                                        theme: th
                                        text: "登录"
                                        strong: true
                                        enabled: !backend.accountBusy
                                        onClicked: backend.loginWithSite()
                                    }
                                    Btn {
                                        Layout.preferredWidth: 120
                                        theme: th
                                        text: "注册新账号"
                                        enabled: !backend.accountBusy
                                        onClicked: backend.openRegisterPage()
                                    }
                                }
                            }

                            // 已登录：账号 + 角色
                            Column {
                                visible: backend.loggedIn
                                width: parent.width
                                spacing: 10
                                RowLayout {
                                    width: parent.width
                                    Text {
                                        text: backend.accountName === "" ? "星璃账号" : backend.accountName
                                        color: th.fg; font.pixelSize: 15
                                        font.weight: Font.Medium
                                    }
                                    Rectangle {
                                        height: 20
                                        width: roleTxt.implicitWidth + 14
                                        radius: 10
                                        color: th.cream
                                        border.color: th.stroke; border.width: 1
                                        Text {
                                            id: roleTxt
                                            anchors.centerIn: parent
                                            anchors.leftMargin: 7
                                            anchors.rightMargin: 7
                                            text: backend.role === "admin" ? "管理员" : "教师"
                                            color: th.op; font.pixelSize: 12
                                        }
                                    }
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: backend.role === "admin"
                                          ? "管理员：能看画面，也能操作教室机（电源 / 远控 / 终端 / 广播）。"
                                            + "右边看得到但点不动的按钮，就是这个身份之外的事。"
                                          : "教师：能看画面、发通知、推文件；电源、远控、终端、广播这类动作要管理员。"
                                    color: th.fg3; font.pixelSize: 12
                                }
                                RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    Text { text: "这一台的身份"; color: th.fg3; font.pixelSize: 12 }
                                    Item { Layout.fillWidth: true }
                                    Btn {
                                        Layout.preferredWidth: 96
                                        Layout.preferredHeight: 26
                                        theme: th
                                        text: "换成管理员"
                                        strong: backend.role !== "admin"
                                        enabled: backend.role !== "admin"
                                        tip: "切到管理员后，操作类按钮（电源/远控/终端/广播）才能点"
                                        onClicked: backend.setRole("admin")
                                    }
                                    Btn {
                                        Layout.preferredWidth: 96
                                        Layout.preferredHeight: 26
                                        theme: th
                                        text: "换成教师"
                                        strong: backend.role === "admin"
                                        enabled: backend.role === "admin"
                                        tip: "切成教师后，操作类按钮会置灰，避免误点教室机"
                                        onClicked: backend.setRole("teacher")
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
                            Text { text: "连接"; color: th.fg3; font.pixelSize: 12 }
                            // 稿 .kv：键 70px 辅助色，值 12px 操作色
                            Column {
                                width: parent.width
                                spacing: 6
                                Row {
                                    Text { text: "云端"; color: th.fg3; font.pixelSize: 13; width: 70 }
                                    Text { text: backend.cloudUrl; color: th.op; font.pixelSize: 13 }
                                }
                                Row {
                                    Text { text: "状态"; color: th.fg3; font.pixelSize: 13; width: 70 }
                                    Text {
                                        // 术语换日常词：不说"已连接/未连接"以外的协议词（规则 2）
                                        text: backend.connected ? "连接正常" : "连不上云端"
                                        color: th.op; font.pixelSize: 13
                                    }
                                }
                                // 网站账号（2026-10-06，OAuth 一户通）：账号过期/没登录时要能看见原因，
                                // 并且能当场点一下重新登录 —— 不能让用户去翻日志才知道"票过期了"。
                                Row {
                                    Text { text: "账号"; color: th.fg3; font.pixelSize: 13; width: 70 }
                                    Text {
                                        id: acctText
                                        text: backend.accountText
                                        color: backend.accountBusy ? th.fg3 : th.op
                                        font.pixelSize: 13
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
                                        text: "点这里用网站账号登录"; color: th.op; font.pixelSize: 13
                                    }
                                }
                            }
                        }
                    }

                    // ── 提醒（2026-10-06：以前是画着玩的假开关，现在是真的）──
                    // 两个开关都在后端接了真事件：操作完成（机器真做了才提示）、
                    // 与云端断开。状态落在 QSettings，重开程序还在。
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
                            Text { text: "提醒"; color: th.fg3; font.pixelSize: 12 }
                            Column {
                                width: parent.width
                                spacing: 4
                                ToggleRow {
                                    width: parent.width
                                    theme: th
                                    text: "操作完成时提示"
                                    note: "机器上真的做完了才说一句（云端收下不算）"
                                    checked: backend.notifyOnDone
                                    onToggled: function (on) { backend.notifyOnDone = on }
                                }
                                ToggleRow {
                                    width: parent.width
                                    theme: th
                                    text: "设备离线时提醒"
                                    note: "与云端断开时弹一条（后台也能看见）"
                                    checked: backend.notifyOnOffline
                                    onToggled: function (on) { backend.notifyOnOffline = on }
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
                            Text { text: "外观"; color: th.fg3; font.pixelSize: 12 }
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
                                        font.pixelSize: 13
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
                                        font.pixelSize: 13
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
                            Text { text: "课表编辑器"; color: th.fg3; font.pixelSize: 12 }
                            Text {
                                text: "排课、时间轴、科目管理与多周轮换（数据格式兼容 ClassIsland）"
                                color: th.op; font.pixelSize: 13
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
                                    font.pixelSize: 13
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
                    // 版本以前写「—」（等于没做），现在读编译期注入的 backend.version。
                    // 「被控端是什么」以前在顶栏那个死开关上没地方讲 —— 挪到这儿，
                    // 一句话说清两台程序的关系 + 上哪儿拿被控端。
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
                            Text { text: "关于"; color: th.fg3; font.pixelSize: 12 }
                            Text { text: "星集控 · 管理端"; color: th.fg; font.pixelSize: 13 }
                            Row {
                                spacing: 0
                                Text { text: "版本"; color: th.fg3; font.pixelSize: 12; width: 70 }
                                Text { text: backend.version; color: th.op; font.pixelSize: 12 }
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: "被控端是另一台机器上的另一个程序（星集控被控端 / 绿色包），" +
                                      "装到教室机上、登录同一个星璃账号之后，这台管理端就能看到它。" +
                                      "这里切不出被控端 —— 要看哪台机器，就在控制页左边选。"
                                color: th.fg3; font.pixelSize: 12
                            }
                            RowLayout {
                                width: parent.width
                                spacing: 10
                                Btn {
                                    Layout.preferredWidth: 118
                                    Layout.preferredHeight: 26
                                    theme: th
                                    text: "去官网下载页"
                                    tip: "在浏览器里打开 www.245959623.xyz/download"
                                    onClicked: backend.openExternal("https://www.245959623.xyz/download")
                                }
                            }
                            Text {
                                text: "日志 stelarith-viewer-qt/viewer.log"
                                color: th.fg3; font.pixelSize: 12
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
                    model: [ "概览", "控制", "集控", "设置" ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: (index === root.page) ? th.fg : th.fg3
                            font.pixelSize: 13
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
                font.pixelSize: 14
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
                    font.pixelSize: 15
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
                        font.pixelSize: 14
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
                        font.pixelSize: 14
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
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                text: "填程序名或路径（不在安全名单里的会被拒绝）"
                color: th.fg3
                font.pixelSize: 12
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
                    font.pixelSize: 14
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
                        font.pixelSize: 14
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
                        font.pixelSize: 14
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
    TerminalDialog { id: termDlg; theme: root.th }

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
    // 顶栏那颗「去设置」在账户组件里够不着 root，这里把跳转塞给它。
    // 不用 root.page = 3 写在组件里：页号是主界面的事，别让它去猜。
    // 命令面板：组件只负责「收得进 / 搜得到 / 显示对」，命令表由主界面在这里装配 ——
    // 命令要跑 runAction / openOnly / page，那些都住在主界面里，组件自己猜不到。
    // 灵动岛（3.8）：挂**窗口层** —— 它讲"现在系统在干什么"，跟老师在哪个标签无关。
    // 悬浮在内容区上方（顶条 44 + 8 安全区），不占布局高度：
    // 右栏那列是固定高度的结构，给它加高度只会把底部标签栏顶出去（见 minimumHeight 注释）。
    Item {
        anchors.top: parent.top
        anchors.topMargin: 52
        anchors.horizontalCenter: parent.horizontalCenter
        width: island.width
        height: island.height
        DynamicIsland { id: island; theme: root.th; states: root.islandStates }
    }

    // 批量的超时兜底（见 batchTimeoutClose）。放窗口层：跟老师在哪个标签页无关。
    Timer {
        id: batchTimeout
        repeat: false
        onTriggered: root.batchTimeoutClose()
    }

    // Toast（5.6 组件规范）：底部居中、距底 32px、3 秒消失、上移淡入 200ms。
    // 放在窗口层，所以任何一页都够得着（集控页那颗 hint 留着，它贴着卡片更好使）。
    Rectangle {
        id: toastw
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastwText.width + 28
        height: 30
        radius: th.rCtrl
        color: th.cream
        border.color: th.stroke
        border.width: 1
        opacity: root.toastText === "" ? 0 : 1
        z: 40
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Text {
            id: toastwText
            anchors.centerIn: parent
            text: root.toastText
            color: th.fg
            font.pixelSize: 13
        }
        Timer { id: toastTimer; interval: 3000; onTriggered: root.toastText = "" }
    }

    CommandPalette {
        id: pal
        theme: root.th
        ctx: root
    }

    // 快捷键（设计文档 4.7）：老师站在讲台前，一等一按就出事。
    // Ctrl+F 在原设计里是「集焦搜索框」，这版界面没有设备搜索框，命令面板就是那个入口。
    Shortcut { sequence: "Ctrl+K"; onActivated: { pal.open() } }
    Shortcut { sequence: "Ctrl+F"; onActivated: { pal.open() } }
    Shortcut { sequence: "F5";     onActivated: { backend.requestDevices() } }
    Shortcut { sequence: "Ctrl+1"; onActivated: { root.page = 0 } }
    Shortcut { sequence: "Ctrl+2"; onActivated: { root.page = 1 } }
    Shortcut { sequence: "Ctrl+3"; onActivated: { root.page = 2 } }
    Shortcut { sequence: "Ctrl+4"; onActivated: { root.page = 3 } }
    // 选择类（4.7）：锁屏是最高频也最容易误伤的操作，给它两个键不加面板；
    // Ctrl+L 的单键形态和 Ctrl+Shift+L 的全量形态分开，避免"我本来只想锁一台"。
    Shortcut { sequence: "Ctrl+A";     onActivated: { root.selectAll() } }
    Shortcut { sequence: "Esc";        onActivated: { root.clearPick() } }
    Shortcut { sequence: "Ctrl+L";     onActivated: { root.runOnPicked("lock") } }
    Shortcut { sequence: "Ctrl+Shift+L"; onActivated: { root.runOnPicked("") } }

    Component.onCompleted: {
        acctMenu.goSettings = function () { root.page = 3 }
        pal.cmds = [
            // ── 教室机动作 ─────────────────────────────────────────────
            // 全部走 runAction()，和右栏按钮同一条路 —— 权限门控、能力门控都在那儿，
            // 命令面板绕过去就等于开了条后门（界面置灰了，快捷键却点得动，最容易出事）。
            { name: "锁屏教室机",       keys: ["lock", "suo", "sjb"], tip: "教室机立刻锁屏（要管理员身份）", run: function () { root.runAction({ l: "锁屏", a: "lock", t: false, p: "admin" }) } },
            { name: "重启教室机",       keys: ["reboot", "chongqi", "cq"], tip: "重启教室机（要管理员身份）", run: function () { root.runAction({ l: "重启", a: "reboot", t: false, p: "admin" }) } },
            { name: "关机教室机",       keys: ["shutdown", "guanji", "gj"], tip: "关掉教室机（要管理员身份）", run: function () { root.runAction({ l: "关机", a: "shutdown", t: false, p: "admin" }) } },
            { name: "给教室机发通知",   keys: ["notify", "tongzhi", "tz"], tip: "填标题和正文后下发（要填内容）", run: function () { root.runAction({ l: "通知", a: "", t: false }) } },
            { name: "打开终端",         keys: ["terminal", "cmd", "zhongduan"], tip: "命令行窗口（要管理员身份）", run: function () { root.runAction({ l: "终端", a: "", t: false, p: "admin" }) } },
            { name: "开始远程控制",     keys: ["remote", "yuankong", "yk"], tip: "实时接管鼠标键盘（要管理员身份）", run: function () { root.runAction({ l: "远控开", a: "remote_control_start", t: false, p: "admin" }) } },
            { name: "停止远程控制",     keys: ["remote", "yk"], tip: "把上一路的远控收掉", run: function () { root.runAction({ l: "远控关", a: "remote_control_stop", t: false, p: "admin" }) } },
            { name: "截图",             keys: ["screenshot", "jieku", "jk"], tip: "抓一帧画面回来", run: function () { root.runAction({ l: "截图", a: "screenshot", t: false }) } },
            { name: "拍一张（摄像头）", keys: ["camera_snapshot", "paizhao", "pz"], tip: "摄像头存一帧", run: function () { root.runAction({ l: "拍一张", a: "camera_snapshot", t: false }) } },
            { name: "看可打开的软件",   keys: ["software", "ruanjian", "rj"], tip: "这台机器上有的程序 + 正在跑的", run: function () { root.runAction({ l: "软件", a: "process_list", t: false }) } },
            { name: "看教室机日志",     keys: ["log", "rizhi", "rz"], tip: "读日志尾部", run: function () { root.runAction({ l: "日志", a: "log_tail", t: false }) } },
            { name: "探一探活",         keys: ["ping", "tanhuo", "th"], tip: "看这台机器还在不在", run: function () { root.runAction({ l: "探活", a: "", t: false }) } },
            { name: "看摄像头列表",     keys: ["camera", "shexiangtou", "sxt"], tip: "这台机器上有几个摄像头", run: function () { root.runAction({ l: "摄像头", a: "camera_list", t: false }) } },
            { name: "调音量",           keys: ["volume", "yinliang", "yl"], tip: "打开音量浮层", run: function () { root.runAction({ l: "音量", a: "", t: false }) } },
            { name: "分发文件",         keys: ["file", "wenjian", "wj"], tip: "选一个文件推给教室机", run: function () { root.runAction({ l: "文件", a: "", t: false }) } },
            { name: "定时任务",         keys: ["schedule", "dingshi", "ds"], tip: "排一个定时关机 / 重启", run: function () { root.runAction({ l: "定时", a: "list_schedules", t: false }) } },
            { name: "看媒体文件",       keys: ["media", "meiti", "mt"], tip: "教室机上的影音文件", run: function () { root.runAction({ l: "媒体", a: "media_list", t: false }) } },
            { name: "开始录像",         keys: ["record", "luxiang", "lx"], tip: "摄像头开始录（要管理员身份）", run: function () { root.runAction({ l: "开录", a: "camera_record_start", t: false, p: "admin" }) } },
            { name: "停止录像",         keys: ["record", "lx"], tip: "把录制收掉", run: function () { root.runAction({ l: "停录", a: "camera_record_stop", t: false }) } },
            { name: "播放媒体",         keys: ["play", "bofang", "bf"], tip: "在教室机放一遍", run: function () { root.runAction({ l: "播放", a: "media_session_start", t: false }) } },
            { name: "停止播放",         keys: ["stop", "tingbo", "tb"], tip: "把播放停掉", run: function () { root.runAction({ l: "停播", a: "media_session_stop", t: false }) } },
            // ── 选择（4.4 / 4.7）──────────────────────────────────────
            // 这四条走的是和多选按钮同一条路（runOnPicked），免得命令面板和按钮两条逻辑各写一版。
            { name: "选中全部在线设备", keys: ["selectall", "quanxuan", "qx"], tip: "Ctrl+A：把在线设备全选上", run: function () { root.selectAll() } },
            { name: "取消选择",         keys: ["clearpick", "quxiaoxuan", "qxz"], tip: "Esc/点空白：把选中的机器清掉", run: function () { root.clearPick() } },
            { name: "锁屏选中的设备",   keys: ["locksel", "suoxuan", "sx"], tip: "Ctrl+L：只锁刚才选中的那几台", run: function () { root.runOnPicked("lock") } },
            { name: "锁屏全部在线设备", keys: ["lockall", "quanbu", "qb"], tip: "Ctrl+Shift+L：所有在线机器一起锁（要管理员身份）", run: function () { root.runOnPicked("") } },
            // ── 视图 ───────────────────────────────────────────────────
            { name: "切到概览",         keys: ["overview", "gailan", "gl"], tip: "在线设备 + 最近回执 + 今日课表", run: function () { root.page = 0 } },
            { name: "切到控制",         keys: ["control", "kongzhi", "kz"], tip: "看教室机画面 + 下发动作", run: function () { root.page = 1 } },
            { name: "切到集控",         keys: ["jikong", "jk"], tip: "集控面板", run: function () { root.page = 2 } },
            { name: "切到设置",         keys: ["settings", "shezhi", "sz"], tip: "账户 / 连接 / 提醒 / 外观", run: function () { root.page = 3 } },
            { name: "去广播给所有在线设备", keys: ["broadcast", "guangbo", "gb"], tip: "广播要填内容，去集控页下面填再点", run: function () { root.page = 2 } },
            // ── 系统 ───────────────────────────────────────────────────
            { name: "刷新设备列表",     keys: ["refresh", "shuaxin", "sx"], tip: "重新拉一遍云端下发（也可按 F5）", run: function () { backend.requestDevices() } },
            { name: "用网站账号登录",   keys: ["login", "denglu", "dl"], tip: "走星璃账号授权，不用填密钥", run: function () { backend.loginWithSite() } },
            { name: "切成管理员身份",   keys: ["admin", "guanliyuan"], tip: "能操作教室机（电源 / 远控）", run: function () { backend.setRole("admin") } },
            { name: "切成教师身份",     keys: ["teacher", "jiaoshi", "js"], tip: "只留看画面 / 发通知 / 推文件", run: function () { backend.setRole("teacher") } },
            { name: "切到黑白外观",     keys: ["dark", "heibai", "hb"], tip: "默认外观", run: function () { root.darkMode = true } },
            { name: "切到浅色外观",     keys: ["light", "qianse", "qs"], tip: "换个亮堂的", run: function () { root.darkMode = false } },
            { name: "打开官网下载页",   keys: ["download", "xiazai", "xz"], tip: "在浏览器里打开 www.245959623.xyz/download", run: function () { backend.openExternal("https://www.245959623.xyz/download") } }
        ]
    }


    function openOnly(which) {
        softwareDlg.close()
        logDlg.close()
        mediaDlg.close()
        schedDlg.close()
        fileDlg.close()
        notifyDlg.close()
        termDlg.close()
        if      (which === "software") softwareDlg.open()
        else if (which === "log")      logDlg.open()
        else if (which === "media")    mediaDlg.open()
        else if (which === "sched")    schedDlg.open()
        else if (which === "file")     fileDlg.open()
        else if (which === "notify")   notifyDlg.open()
        else if (which === "term")     termDlg.open()
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
        "探活": ["ping"],
        // 没带 terminal_open 的被控端（0.6.3 之前那批）开不了终端，
        // 按钮得跟着置灰 —— 让"点下去只弹一句不支持"不如让入口就不亮。
        "终端": ["terminal_open"]
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
    /** 权限门控：p 是权限键（"lock"/"reboot"/"terminal_open"/"broadcast"），空 = 谁都能做。
     *  判据一律问 backend（真实角色在那儿），界面不自己记一份"我以为我是管理员"。 */
    function permOk(p) {
        return !p || backend.mayDo(p);
    }

    function runAction(d) {
        if (d.t) return                      // 未实现的按钮：压根不该点得到

        // 权限门控兜底：按钮已经置灰了，但状态行/快捷键/老界面可能绕过 runAction，
        // 这里统一再拦一次，拦下就给一句话（reportUnsupported 里带日志，不静默）。
        if (!permOk(d.p)) {
            backend.reportDenied(d.l);
            return;
        }
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
        } else if (d.l === "终端") {
            // 开之前先把别的弹窗收掉：这条链路弹出的是全屏终端窗口，和画面弹窗叠着没法用
            termDlg.open()
        } else if (d.a !== "") {
            backend.sendAction(d.a, {})
        }
    }

    // 广播：原来这段逻辑写死在控制页那颗按钮的 onClicked 里，现在命令面板也要发广播，
    // 抽到这儿两边共用（顺序保持原样：权限 → 内容 → 有没有设备 → 逐台下发）。
    function broadcastAll(text) {
        if (!backend.mayDo("broadcast")) {
            toast("广播要给所有机器发，要管理员身份");
            return
        }
        const content = (text || "").trim()
        if (content === "") { toast("先填广播内容"); return }
        const devs = backend.devices
        if (!devs || devs.length === 0) { toast("没有在线设备"); return }
        for (let i = 0; i < devs.length; ++i) {
            backend.currentUid = devs[i].uid
            backend.sendAction("notify", {
                "title": "广播", "content": content,
                "seconds": 10, "tts": false,
                "flags": { "severity": "inform" }
            })
        }
        toast("已广播给 " + devs.length + " 台设备")
    }

    // ── 全局提示（设计文档 5.6 Toast）──────────────────────────────────
    // ⚠️ 原来只有一个 consolePage.hint()，而它**住在集控页里**：在别的页（概览 / 控制 / 设置）
    //    调用 hint() 是 ReferenceError，提示根本不出来。broadcastAll 就踩过这一下 ——
    //    广播发出去了、一句回话没有，看着跟"点了没反应"一模一样（4.6 点名的头号体验杀手）。
    // 一律走 root.toast()；集控页那颗旧的保留（它贴着卡片、就在手边，换掉反而别扭）。
    function toast(s) {
        root.toastText = s || ""
        root.toastTick = root.toastTick + 1
        toastTimer.restart()
    }

    // 灵动岛：把"正在进行 / 需要处理"的事聚成一条（3.8）。
    // 数组顺序就是优先级（3.8.3：离线 > 批量指令 > 文件 > 告警 > 监控），
    // 组件取 states[0] 当主状态，其余在展开态列出来。
    // 语音那一档本项目还没有数据源（被控端没这个能力），宁可不亮，不摆一个永远空的胶囊。
    function refreshIsland() {
        const s = []
        if (!backend.loggedIn)
            s.push({ k: "offline", t: "还没登录", d: "点右上角账户，用网站账号登录" })
        else if (!backend.connected)
            s.push({ k: "offline", t: "连接断开，重连中…", d: "设备表还是上次拉到的" })
        if (batch.active)
            s.push({ k: "command", t: batch.label + " " + batch.done + "/" + batch.total,
                     d: (batch.fail > 0 ? "已失败 " + batch.fail + " 台" : "逐台下发中"),
                     p: batch.total > 0 ? batch.done / batch.total : 0 })
        if (backend.fileState === "sending" || backend.fileState === "pushing")
            s.push({ k: "file", t: "分发文件 " + backend.filePercent + "%",
                     d: backend.fileTarget || backend.fileName || "",
                     p: Math.max(0, Math.min(100, backend.filePercent)) / 100 })
        if (alerts.length > 0)
            s.push({ k: "alert", t: alerts.length + " 条没处理", d: alerts[0].uid + " · " + alerts[0].action })
        if (backend.rtcState === "track")
            s.push({ k: "monitor", t: "正在看 " + backend.currentUid, d: "实时画面" })
        islandStates = s
    }

    function pushAlert(uid, action, why) {
        // 同一台同一条不重复攒：60 台一起失败会瞬间把列表顶满 —— 那不叫告警，叫噪声
        for (let i = 0; i < alerts.length; ++i)
            if (alerts[i].uid === uid && alerts[i].action === action) return
        const arr = alerts.slice()
        arr.unshift({ t: Qt.formatTime(new Date(), "hh:mm:ss"), uid: uid, action: action, why: why || "" })
        alerts = arr.slice(0, 20)
    }

    // ── 批量三段式：发之前说清几台、发之中看得见走到哪、发完了报结果（4.5）──
    function batchStart(label, total) {
        batch = { label: label, total: total, done: 0, ok: 0, fail: 0, active: true }
        // 收口兜底：设备掉线、云端不回执时，不能让这句话永远挂在灵动岛上。
        // 台数越多给的时间越长（每台 4 秒），卡在 10s ~ 60s 之间。
        batchTimeout.interval = Math.max(10000, Math.min(60000, total * 4000))
        batchTimeout.restart()
        refreshIsland()
    }

    // 某台设备现在在不在线（认云端给的 online；老云端不带这个键就按在线处理）
    function isDevOnline(uid) {
        for (let i = 0; i < root.devices.length; ++i)
            if (root.devices[i].uid === uid) return (root.devices[i].online !== false)
        return false
    }

    // 超时收口：没回话的按"没成"结账，并且说出来 —— 不假装成功
    function batchTimeoutClose() {
        if (!batch.active) return
        const left = Math.max(0, batch.total - batch.done)
        batch.done = batch.total
        batch.fail = batch.fail + left
        batch.active = false
        toast(batch.label + "：等超时了，" + batch.ok + " 台确认"
              + (left > 0 ? "，" + left + " 台没回话（多半是掉线）" : ""))
        refreshIsland()
    }
    function batchStep(uid, ok, why) {
        if (!batch.active) return
        batch.done = batch.done + 1
        if (ok) batch.ok = batch.ok + 1
        else batch.fail = batch.fail + 1
        if (!ok) root.pushAlert(uid, batch.label, why || "")
        if (batch.total > 0 && batch.done >= batch.total) {
            // 全成了也得说一句（4.5）："没消息"不该被当成"都成了"
            batch.active = false
            batchTimeout.stop()
            toast(batch.label + "：完了 " + batch.ok + " 台" + (batch.fail > 0 ? "，" + batch.fail + " 台没成" : ""))
        } else {
            batchTimeout.restart()      // 还有台在等，把超时往后推
        }
        refreshIsland()
    }

    // ── 设备多选（4.4）：单击 / Ctrl / Shift / Ctrl+A / Esc ──────────────
    // 单机选中（currentUid）管"看哪台"，多选（picked）管"给哪些台发" —— 两件事别合并：
    // 老师先看一台确认，再对同班 30 台一起锁，这是两条动作链，硬并成一条会互相踢。
    function pickDevice(uid, mods, idx) {
        if (mods & Qt.ControlModifier) {
            if (root.picked.indexOf(uid) >= 0)
                root.picked = root.picked.filter(function (x) { return x !== uid })
            else
                root.picked = root.picked.concat([uid])
            root.pickAnchor = idx
        } else if (mods & Qt.ShiftModifier) {
            if (root.pickAnchor < 0 || root.pickAnchor >= root.devices.length) {
                root.picked = [uid]
                root.pickAnchor = idx
                return
            }
            const a = Math.min(root.pickAnchor, idx), b = Math.max(root.pickAnchor, idx)
            const s2 = []
            for (let i = a; i <= b; ++i) s2.push(root.devices[i].uid)
            root.picked = s2
        } else {
            root.picked = [uid]
            root.pickAnchor = idx
            backend.currentUid = uid        // 单击照旧把画面切过去（原本就是这个行为）
        }
    }
    function selectAll() {
        const s2 = []
        for (let i = 0; i < root.devices.length; ++i) s2.push(root.devices[i].uid)
        root.picked = s2
        root.pickAnchor = -1
        toast("已选 " + root.picked.length + " 台")
    }
    function clearPick() {
        if (root.picked.length > 0) toast("取消了选择")
        root.picked = []
        root.pickAnchor = -1
    }
    /** 对选中设备批量下发。a 为空 = 对全部在线设备（Ctrl+Shift+L 走这条）。
     *  权限判据直接问 permOk(a)：a 就是 action 名，adminOnlyActions 里那批都要管理员。 */
    function runOnPicked(a) {
        if (!permOk(a)) {
            backend.reportDenied("锁屏 / 关机")
            return
        }
        const list = []
        if (a === "") {
            for (let i = 0; i < root.devices.length; ++i) list.push(root.devices[i].uid)
        } else {
            for (let i = 0; i < root.picked.length; ++i) list.push(root.picked[i])
        }
        if (list.length === 0) {
            toast("还没有可操作的设备")
            return
        }
        const label = (a === "lock" ? "锁屏" : a === "shutdown" ? "关机" : a)
        root.batchStart(label + " " + list.length + " 台", list.length)
        const dead = []
        let sent = 0
        for (let i = 0; i < list.length; ++i) {
            // 已经掉线的就别发了：发出去也没人回执，只会在灵动岛上挂成一个永不推进的 0/N
            if (!root.isDevOnline(list[i])) { dead.push(list[i]); continue }
            backend.currentUid = list[i]
            backend.sendAction(a, {})
            sent += 1
        }
        for (let j = 0; j < dead.length; ++j) root.batchStep(dead[j], false, "设备不在线")
        toast(label + "已下发 " + sent + " 台" + (dead.length > 0 ? "，" + dead.length + " 台不在线" : ""))
        root.picked = []
    }
}

