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
import "components"        // 通用组件：Card / Btn / ToggleRow（2026-10-06 打磨抽出；EmptyState 已清理，全库 0 引用）
import "NotifyParams.js" as NotifyParams

ApplicationWindow {
    id: root
    width: 1180
    // 2026-10-08：760 → 700，最小 680 → 600。原因不是审美，是**装不下**：
    // 1280x720 这类教室屏/投影很常见，再把标题栏(≈31) 和任务栏(≈40) 算进去，
    // 能用的高度只有 649 —— 原来"默认 760 / 最小 680"在这类机器上必然被切掉底部，
    // 表现就是用户说的"界面不在屏幕内"。
    // 现在右栏动作区已经改成可滚动（Flickable，见控制页那段的说明），
    // 所以窗口可以压到 600 而不丢内容。main.cpp 那边还会把窗口夹进屏幕可用区兜底。
    height: 700
    minimumWidth: 940
    minimumHeight: 600
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

    // ── 浮层宿主（账户菜单）──────────────────────────────────────────
    // 2026-10-07 修「点右上角只出一层灰、菜单本体不出」：
    //   旧实现是 Popup{modal:true}，Qt 会把「遮罩 + 菜单」一起 reparent 到
    //   Overlay.overlay，谁压着谁由插进去的先后决定 —— 实测灰底盖住了菜单，
    //   屏幕上就只剩一层透明度降低的灰，点什么都是它。
    //   现在遮罩与菜单都挂在这一层：z 恒压所有页面之上，位置用 anchors 贴着胶囊算
    //   （不碰 mapToGlobal、不手算坐标），既不会被盖住也不会飞出屏幕。
    Item {
        id: acctLayer
        z: 9990
        anchors.fill: parent
    }

    // ── 主题令牌（黑白默认黑；浅色为备选）──
    // 页面里一律从 th 取色，不写死 —— 否则换主题必花。
    property bool darkMode: true

    // 主题 Token 单一真源：qml/components/Theme.qml（深 / 浅两套由 dark 切换）。
    // 2026-10-09 把原来内联的 darkTh / lightTh 两个 JS 对象抽到 Theme 组件，
    // 行为与旧版逐字一致，token 改一处即全局生效。
    readonly property var darkTh:  Theme { dark: true }
    readonly property var lightTh: Theme { dark: false }

    readonly property var th: darkMode ? darkTh : lightTh

    // 界面自身的状态（只跟显示有关的东西）
    property int page: 1                 // 0 概览 / 1 集控（带画面主页面）/ 2 设置

    // 2026-10-08 修复「选中设备却不在带画面页 → 无限拉流」：
    // 切到其它页就退订当前设备画面 / 多班缩略图墙，云端按订阅发帧，退订即停流。
    // 回到对应页再恢复订阅。两个订阅互不干扰（退订缩略图会跳过当前选中那台）。
    // 2026-10-11：「批量管控」页已删，多班墙改由「多班墙」弹窗显式订阅（见 multiThumbDlg），
    // 不再跟随页签自动拉 —— 否则每次切页都在后台偷偷订阅一遍。
    onPageChanged: {
        backend.setScreenActive(root.page === 1)   // 单台大画面只在集控（带画面主页）拉
        if (!multiThumbDlg.visible) backend.subscribeThumbnails([])
    }
    function syncPageLive() {
        backend.setScreenActive(root.page === 1)
        if (!multiThumbDlg.visible) backend.subscribeThumbnails([])
    }
    property int volumeValue: 30
    // 远端确认过的音量（set_volume 回执里的 data.volume）。-1 = 还没拿到，就别往界面上写。
    // 刻意不用滑块那个值：root.volumeValue 只是本机拖动出来的数字，被控端根本没确认过，
    // 拿它冒充"教室机现在音量多少"就是假装成功了。
    property int remoteVolume: -1
    property var results: []          // 概览页的「最近回执」流水（最多 20 条，只收真回执）
    property var candidates: []       // 软件弹窗的「可打开」候选（来自被控端 list_shortcut_candidates）
    // 通知确认汇总（2026-10-07）：当前会话内「需确认」通知的学生确认统计。
    // 由 onResultReceived 收 notify+confirmed 回执累加；{count, replies[]}。
    property var confirmStats: ({ count: 0, replies: [] })
    // 更新日志（需求 #7：把官网已有的「版本历史」能力搬进管理端）。
    // 数据面是静态文案，与官网 content/changelog.json 同源；这里内嵌一份只读镜像，离线也能看。
    // 官网那份是发布归档的唯一真源，这里绝不覆盖官网的更新历史（只追加以保持完整）。
    readonly property var changelogModel: [
        {
            version: "2026.10.08", date: "2026-10-08",
            title: "星集控：分班制与离线设备可见",
            highlight: "管理端现在能按班级筛选设备、看到离线教室机，概览页也多了云端存储概况。",
            items: [
                { kind: "feat", text: "设备列表支持按班级筛选（分班制）：按班级维度管理设备与分组" },
                { kind: "feat", text: "离线设备不再从列表消失：云端持久「已知设备表」，在线状态由连接派生" },
                { kind: "feat", text: "概览页新增「云端存储概况」卡：已知/在线/离线设备数、班级数、待执行指令、事件落盘量" },
                { kind: "feat", text: "管理端新增「更新日志」与「帮助」（与官网一致）" },
                { kind: "change", text: "底部导航「控制」更名「集控」，原集控批量页并入主页后改叫「批量管控」" }
            ]
        },
        {
            version: "2026.10.06", date: "2026-10-06",
            title: "星集控 0.6.2：管理端「集控」页上线",
            highlight: "管理端把网页控制台的核心功能搬进桌面端：通知下发、定时任务、广播一条龙，课表编辑器也补全了撤销与导入导出。",
            items: [
                { kind: "feat", text: "管理端新增「集控」页：通知下发（普通/重要/紧急）、定时任务、广播" },
                { kind: "feat", text: "课表编辑器补全：撤销/重做、CSV 导入导出、学期起点可配置" },
                { kind: "fix", text: "管理端托盘常驻：关窗只隐藏到托盘，不再整程序退出" }
            ]
        },
        {
            version: "2026.10.06", date: "2026-10-06",
            title: "星集控 0.6.0：被控端能自己升级，官网有了下载中心",
            highlight: "教室机装上后就不必再一台台手动换包；下载页的版本号和校验值自动跟着云端走，不会再写错。",
            items: [
                { kind: "feat", text: "官网新增「下载中心」：版本号/体积/sha256 直接读云端发布清单" },
                { kind: "feat", text: "被控端支持自更新（OTA）：托盘发现新版本点一下升级，先校验 sha256" },
                { kind: "fix", text: "被控端只允许开一个：重复启动直接提示已在运行并退出" }
            ]
        }
    ]
    // 设备分组视图（4.4）：全部 / 在线 / 离线 三段筛选（Repeater 用 filteredDevices）
    property string deviceFilter: "all"   // all / online / offline
    readonly property var filteredDevices: (function () {
        var arr = []
        var devs = backend.devices || []
        var cls = backend.currentClass || ""
        for (var i = 0; i < devs.length; ++i) {
            var d = devs[i]
            if (!d) continue
            var on = (d.online !== false)
            if (root.deviceFilter === "online" && !on) continue
            if (root.deviceFilter === "offline" && on) continue
            // 2026-10-08 分班制：选了某班就只显示该班设备
            if (cls && d.classCode !== cls) continue
            arr.push(d)
        }
        return arr
    })()

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
    // 多选的设备 uid（4.4）。和 currentUid 是两套东西：那个管"看哪台"，
    // 这个管"给哪些台发" —— 混在一处会互相踢（台数变了不知道该信哪个）。
    property var picked: []
    property int pickAnchor: -1       // Ctrl 点下的第一台，给 Shift 范围选当端点

    // 云端回来的东西：一切以它为准
    readonly property var devices: backend.devices
    readonly property string currentUid: backend.currentUid
    // ⚠️ 2026-10-10：给「在子组件里引用 backend」的地方一个**不重名**的转发名。
    //   子组件若自己也有 `backend` 属性（如 CameraCenter/CameraBindDialog），挂 `backend: backend`
    //   时右侧的 `backend` 会被**自身属性遮蔽**（QML 非限定名先查自身），变成自绑定 ⇒ 引擎报
    //   `Binding loop detected for property "backend"`，随后该子组件里所有 `xxx.backend.*` 全变 undefined。
    readonly property var backendRef: backend
    // 屏幕上这张画面**确实是当前选中那台**的吗？
    // ⚠️ 2026-10-08 修「切了设备画面还是第一台」：光看 frameCount 不够 —— 上一台的帧
    //    还在路上（云端退订不是瞬时的），落进同一张 frame 就会继续冒充新设备。
    //    后端现在给每帧记了 frameUid（这帧属于谁），这里必须两边对齐才上屏。
    readonly property bool frameReady: backend.frameCount > 0
                                        && backend.frameUid === backend.currentUid
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
            if (action === "set_volume" && data && data.volume !== undefined) {
                root.remoteVolume = data.volume
                // 2026-10-11：只改界面数字、不给提示 = 老师看不见到底成没成。
                // 被控端夹过的（比如传了 120）会带 clampedFrom，一并说出来。
                if (data.clampedFrom !== undefined)
                    root.toast("音量已设为 " + data.volume + "%（你填的 " + data.clampedFrom + " 超出 0-100，已夹到边界）")
                else
                    root.toast("音量已设为 " + data.volume + "%")
            }

            // 截图（2026-10-11）：以前点了就没声没息——指令确实发了、也确实执行了，
            // 但界面一个字都不说，老师只能靠"没报错"猜。这里把回执念出来。
            // 注意 path 是**教室机**上的路径，本机打不开，别误导成"已存到你电脑"。
            if (action === "screenshot") {
                if (result === "done" && data && data.path !== undefined)
                    root.toast("已截图 → 教室机 " + uid + "：" + data.path
                               + "（" + (data.width || "?") + "×" + (data.height || "?")
                               + "，" + Math.round((data.bytes || 0) / 1024) + " KB）")
                else
                    root.toast("截图失败 → " + uid + "：" + (error || "未知原因"))
            }

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

            // 通知确认汇总（2026-10-07）：result=confirmed 是学生点了「确认/快捷回复」的
            // 二次回执（云端 recordResult 已放行，见 c1bd8f2）。在这里累加统计，
            // 通知卡片显示「已确认 N / 最近回复」，让老师不用翻回执流水数。
            if (action === "notify" && result === "confirmed" && data) {
                var cs = root.confirmStats
                cs.count = (cs.count || 0) + 1
                var rep = String(data.reply || "").trim()
                if (rep) {
                    var ls = (cs.replies || []).slice()
                    ls.unshift(rep)
                    cs.replies = ls.slice(0, 8)   // 只留最近 8 条
                }
                root.confirmStats = cs
            }

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
            if (batch.active) {
                // 「确认 / 快捷回复」是**同一条通知的第二次回执**，不是"又一台设备完成了"。
                // 回馈必须按设备算（4.5）：只有每台设备的第一次回执参与进度，
                // 确认只落进这台设备的明细行 —— 否则 30 台通知里只要 1 台确认，
                // 进度就 1/30 直接顶满，收口文案报出假的"30 台都成了"，
                // 老师看到的"只有一次反馈"就是这么来的。
                if (action === "notify" && result === "confirmed") root.batchNote(uid)
                else root.batchStep(uid, result === "done", error || "")
            }
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
                        // 2026-10-10 彩色令牌试点：连接状态点 = ok 绿（在线）/ err 红（离线）
                        // 规范：状态强调只用点/标签/描边，不做背景铺色
                        Rectangle {
                            width: 5; height: 5; radius: 3
                            color: backend.connected ? th.ok : th.err
                        }
                        Text {
                            text: backend.connected
                                  ? (backend.onlineCount + " 台在线")
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
                                        root.page = 2
                                }
                            }
                        }
                    }

                    // 胶囊尺寸问容器（Item 默认 0×0，不给尺寸就是"顶栏右侧空一块"）
                    AccountMenu {
                        id: acctMenu
                        theme: root.th
                        hostLayer: acctLayer
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
                                { k: "在线",   v: backend.onlineCount },
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

                    // ── 快捷操作（4.7 效率功能）──
                    // 高频动作钉在概览页顶部，一个键直达（不翻菜单 / 不挨卡片）。
                    Row {
                        spacing: 8
                        Repeater {
                            model: [
                                { l: "锁屏",       a: "lock" },
                                { l: "发通知",     a: "notify" },
                                { l: "广播",       a: "broadcast" },
                                { l: "语音对讲",   a: "voice" },
                                { l: "屏幕广播",   a: "screen" },
                                { l: "考试模式",   a: "exam" },
                                { l: "刷新多班墙", a: "multiband" }
                            ]
                            delegate: Rectangle {
                                width: 76; height: 30; radius: th.rCtrl
                                color: qhMa.containsMouse ? th.hover2 : th.panel
                                border.color: th.stroke; border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.l
                                    color: th.fg
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: qhMa
                                    anchors.fill: parent
                                    onClicked: root.quickAction(modelData.a)
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

                    // ── 云端存储概况（需求 #2：概览页要展示云端存储信息）──
                    // 数据面来自 backend.storage（/api/storage 返回），界面只消费，不自己再数一遍。
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 124
                        radius: th.rCard
                        color: th.panel
                        border.color: th.card
                        border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Item {
                                width: parent.width; height: 16
                                Text { text: "云端存储概况"; color: th.fg; font.pixelSize: 13; font.weight: Font.Medium }
                                Text {
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    text: (backend.storage.at ? ("更新于 " + backend.storage.at) : "")
                                    color: th.fg3; font.pixelSize: 12
                                }
                            }
                            Flow {
                                width: parent.width
                                spacing: 18
                                Repeater {
                                    model: [
                                        { k: "已知设备",   v: backend.storage.knownDevices },
                                        { k: "在线",       v: backend.storage.onlineDevices },
                                        { k: "离线",       v: backend.storage.offlineDevices },
                                        { k: "班级",       v: backend.storage.classes },
                                        { k: "待执行指令", v: backend.storage.pendingInstructions },
                                        { k: "事件落盘",   v: backend.storage.eventsFiles }
                                    ]
                                    delegate: Column {
                                        spacing: 2
                                        Text { text: modelData.k; color: th.fg3; font.pixelSize: 12 }
                                        Text {
                                            text: (modelData.v === undefined || modelData.v === null) ? "—" : String(modelData.v)
                                            color: th.fg; font.pixelSize: 16; font.weight: Font.Medium
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: (backend.storage.at === undefined)
                                text: "尚未从云端取到存储概况（连上后会自动拉取）"
                                color: th.fg4; font.pixelSize: 12
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
                                        text: backend.onlineCount + " 台在线"
                                        color: th.fg3; font.pixelSize: 12
                                    }
                                }
                                // 2026-10-09：这张卡叫「在线设备」，只列 true 在线的；
                                // 离线机器留到控制页（那里按班分组、离线画灰，便于看哪些教室掉了）。
                                // refreshDevices 现在把离线也留在台账里，所以这里要自己滤一遍，
                                // 否则离线机也会顶着亮点和「在线设备」的标题混进来。
                                Repeater {
                                    model: root.devices.filter(function (d) { return d.online !== false })
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
                                                // 2026-10-09：优先显示设备名（云端 register 带上来），没有才回退 uid，
                                                // 与控制页设备列表保持一致，不再只裸一个 uid。
                                                text: (modelData.name !== undefined && modelData.name !== null && modelData.name !== "")
                                                      ? modelData.name : modelData.uid
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
                                    visible: (root.devices.filter(function (d) { return d.online !== false })).length === 0
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
                                        SvgIcon {
                                            name: modelData.ok ? "check" : "x"
                                            tint: th.fg
                                            width: 13
                                            height: 13
                                            anchors.verticalCenter: parent.verticalCenter
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

                // 分组视图（4.4）：全部 / 在线 / 离线 三段筛选
                Row {
                    width: 168
                    spacing: 4
                    leftPadding: 10
                    bottomPadding: 4
                    Repeater {
                        model: [ { k: "all", l: "全部" }, { k: "online", l: "在线" }, { k: "offline", l: "离线" } ]
                        delegate: Rectangle {
                            width: 42; height: 22; radius: th.rCtrl
                            color: (root.deviceFilter === modelData.k) ? th.inv : "transparent"
                            border.color: th.stroke; border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: modelData.l
                                color: (root.deviceFilter === modelData.k) ? th.win : th.fg3
                                font.pixelSize: 11
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.deviceFilter = modelData.k
                            }
                        }
                    }
                    // 计数（放在筛选右边，一眼看到每组多少台）
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.deviceFilter === "all")
                              ? (root.devices.length + " 台")
                              : (root.filteredDevices.length + " 台")
                        color: th.fg4
                        font.pixelSize: 11
                    }
                }

                // 分班制（需求 #4）：按班级维度筛选设备。班级来自云端 /api/classes（界面只消费
                // backend.classes），当前选中写 backend.currentClass，filteredDevices 已按它过滤。
                // 班级可能不止几个，用 Flickable 兜住横向滚动，不挤占下面的设备列表宽度。
                // ⚠️ 2026-10-11 修「选中班级是灰的、看不见字」：
                //   旧写法是 Item + Rectangle + Text，宽度写 `clsLbl.width + 16`，而 clsLbl
                //   又是 `anchors.centerIn: parent` —— 宽度绕回自己身上算，chip 被压到几乎没有
                //   宽度，文字全被裁掉，只剩一条灰边（看着就是"灰的、没字"）。
                //   改成 Rectangle 自己当 chip，宽度取 Text 的 **implicitWidth**（纯内容宽，
                //   不依赖布局），并给未选中态一个淡底，选中/未选中一眼可分。
                Flickable {
                    width: 168
                    height: 26
                    contentWidth: Math.max(clsRow.implicitWidth, clsRow.width)
                    clip: true
                    Row {
                        id: clsRow
                        spacing: 4
                        leftPadding: 10
                        Repeater {
                            model: (function () {
                                var arr = [{ code: "", name: "全部班级" }]
                                var cs = backend.classes || []
                                for (var i = 0; i < cs.length; ++i) {
                                    arr.push({ code: cs[i].code, name: (cs[i].name || cs[i].code) })
                                }
                                return arr
                            })()
                            delegate: Rectangle {
                                height: 22
                                width: clsLbl.implicitWidth + 16
                                radius: th.rCtrl
                                color: (backend.currentClass === modelData.code) ? th.inv
                                                                                : th.hover2
                                border.color: (backend.currentClass === modelData.code) ? th.inv
                                                                                        : th.stroke
                                border.width: 1
                                Text {
                                    id: clsLbl
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: (backend.currentClass === modelData.code) ? th.win : th.fg3
                                    font.pixelSize: 11
                                    font.weight: (backend.currentClass === modelData.code)
                                                 ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.setCurrentClass(modelData.code)
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: root.filteredDevices
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
                                // 2026-10-10 彩色令牌：在线=ok 绿 / 离线=err 红
                                color: devOnline ? th.ok : th.err
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                // 2026-10-08：优先显示设备名（云端 register 带上来），没有才回退 uid
                                text: (modelData.name !== undefined && modelData.name !== null && modelData.name !== "")
                                      ? modelData.name : modelData.uid
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
                        visible: backend.frameSource !== "rtc"
                        cache: false
                        property int tick: 0
                        source: root.frameReady ? ("image://frames/f?" + tick) : ""

                        Connections {
                            target: backend
                            function onFrameChanged() { screenImg.tick++ }
                        }
                    }

                    // ── 实时画面（WebRTC / libdatachannel）：frameSource==='rtc' 时挂载 ──
                    // 接线：viewerbackend.rtcFrameReady(QImage) → vidSurf.onFrame(img)
                    // 与 MobileMain.qml:494 同一接法；这是 #171「WebRTC 画面在桌面管理端显出来」的关键。
                    // jpeg 轮询模式下本组件隐藏，画面走上面的 screenImg；rtc 模式下反过来。
                    RtcVideoSurface {
                        id: vidSurf
                        anchors.fill: parent
                        theme: th
                        visible: backend.frameSource === "rtc"
                    }
                    Connections {
                        target: backend
                        function onRtcFrameReady(img) { vidSurf.onFrame(img) }
                    }

                    // 空态照稿 .empty：标题 12/操作色 + 说明 11/辅助色，两行。
                    // 分三种情形（没选机器 / 正在接通 / 选了但没帧）：以前一句话包打天下，
                    // 用户看不出是"正在办"还是"没人理"。
                    Column {
                        anchors.centerIn: parent
                        visible: !root.frameReady && backend.frameSource !== "rtc"
                        spacing: 6

                        BusyIndicator {
                            visible: root.linkBusy || (root.currentUid !== "" && backend.frameCount > 0)
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 28
                            height: 28
                            running: root.linkBusy || (root.currentUid !== "" && backend.frameCount > 0)
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.currentUid === "" ? "还没选机器"
                                : root.linkBusy ? "正在接通"
                                // 切设备时旧画面已经作废、新机器的第一帧还没到 ——
                                // 以前这里什么都不显示（frameCount 非 0 ⇒ 走不到空态），
                                // 屏幕上是上一台的残影，看着像"切了没用"。
                                : backend.frameCount > 0 ? "正在接入这台机器"
                                : "等画面"
                            color: th.op
                            font.pixelSize: 13
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.currentUid === "" ? "在左边选一台"
                                : root.linkBusy ? "连上就有画面"
                                : backend.frameCount > 0 ? "第一帧马上就到"
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
                        visible: backend.screenStatic && root.frameReady && backend.frameSource !== "rtc"
                        text: "画面未变化 · 已暂停刷新"
                        // 固定灰，刻意不跟主题走：这行浮在**远程画面**上，底色是对方的桌面（不可控），
                        // 用主题色反而可能在浅色画面上消失。
                        color: "#8A8A8A"
                        font.pixelSize: 12
                    }

                    // ── 远控输入：绝对坐标「指哪打哪」─────────────────────────
                    // ⚠️ 画面是 Image.PreserveAspectFit：窗口比例和被控端屏幕比例不一致时，
                    //   画面上下/左右会留黑边。旧写法拿「鼠标位置 ÷ 画面区尺寸」当归一化坐标，
                    //   等于把黑边也算进去了 —— 点黑边上的位置操作会整体落到机器边缘，
                    //   越靠边偏得越离谱（这就是"我点这儿、光标跑那儿"）。
                    //   现在先把画面区换算成**内接矩形**，只认矩形内的比例，矩形外夹到边界。
                    //   坐标是绝对的（0~1 → 被控端屏幕像素），不做任何相对位移累积，
                    //   所以拖到哪就到哪，不存在越用越飘。
                    MouseArea {
                        id: screenArea
                        anchors.fill: parent
                        enabled: backend.frameCount > 0 || vidSurf.framesReceived > 0

                        // 鼠标键 → 协议里的 button：以前只有左键，右键/中键远控过去被当左键按，
                        //   右键菜单、中键自动滚全指望它。
                        // ⚠️ 2026-10-10：这三个原先声明在**外层 Rectangle** 上，而 QML 的非限定名
                        //   只查「自身对象 → 组件根」两级 —— MouseArea 的手柄够不到外层兄弟元素的
                        //   函数，于是每次点画面都刷 `ReferenceError: normOf is not defined`，
                        //   远控点击整个失效。移到 MouseArea 自身（唯一使用者）即根治。
                        function btnOf(b) {
                            if (b === Qt.RightButton) return "right"
                            if (b === Qt.MiddleButton) return "middle"
                            return "left"
                        }
                        readonly property rect fitRect: {
                            var fw = backend.frame.width, fh = backend.frame.height
                            var w = screenArea.width, h = screenArea.height
                            if (fw <= 0 || fh <= 0 || w <= 0 || h <= 0)
                                return Qt.rect(0, 0, w, h)
                            var s = Math.min(w / fw, h / fh)
                            var iw = fw * s, ih = fh * s
                            return Qt.rect((w - iw) / 2, (h - ih) / 2, iw, ih)
                        }
                        // 画面内像素 → 被控端屏幕归一化坐标（0~1，绝对位置）
                        // ⚠️ 2026-10-11：画面区尺寸还没算出来时 fitRect 宽高是 0，
                        //    除零得到 NaN，传到 C++ 转 int 就是 INT_MIN —— 日志里刷几百行
                        //    `操控→ move 在 (-2147483648%, -2147483648%)`，光标被甩到教室机角上。
                        //    所以这里返回 null，调用处直接不发（C++ 侧也有一道 isfinite 兜底）。
                        function normOf(mx, my) {
                            var r = fitRect
                            if (!(r.width > 0) || !(r.height > 0)) return null
                            return [Math.min(1, Math.max(0, (mx - r.x) / r.width)),
                                    Math.min(1, Math.max(0, (my - r.y) / r.height))]
                        }
                        // 左 / 中 / 右键全收：这三键在教室机上都用得上
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.CrossCursor

                        onPressed: function (mouse) {
                            var n = normOf(mouse.x, mouse.y)
                            if (!n) return
                            backend.sendPointer("down", n[0], n[1], btnOf(mouse.button))
                        }
                        // 只在**按住时**才发移动：悬停绝不能往教室机灌鼠标移动
                        // （曾经用 hoverEnabled + 自记 dragging，结果悬停就发指令、把日志刷爆）
                        onPositionChanged: function (mouse) {
                            if (mouse.buttons & (Qt.LeftButton | Qt.MiddleButton | Qt.RightButton)) {
                                var n = normOf(mouse.x, mouse.y)
                                if (!n) return
                                backend.sendPointer("move", n[0], n[1])
                            }
                        }
                        onReleased: function (mouse) {
                            var n = normOf(mouse.x, mouse.y)
                            if (!n) return
                            backend.sendPointer("up", n[0], n[1], btnOf(mouse.button))
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
            //
            // ⚠️ 2026-10-08 改成**可滚动**（Flickable 包一层）。原因：这栏是固定高度的一列，
            //    而 1280x720 这类教室屏/投影很常见 —— 窗口根本放不下 680 的最小高度
            //    （再加标题栏和任务栏），窗口底部的标签栏被切在屏幕外，
            //    表现就是"界面跑到屏幕外面去了"。改成可滚之后窗口能压到更矮，
            //    内容放不下就滚，而不是顶出去。
            //    内层 ColumnLayout 的 Layout.* 一律失效（它现在的父是 Flickable，不是定位器），
            //    所以宽高都写在它自己身上 —— 别再往它身上加 Layout.xxx。
            Flickable {
                Layout.preferredWidth: 280
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: rightCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                id: rightCol
                width: parent.width
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
                // 右栏三组的组名：以前三组各写一遍 Text（字号/颜色/上间距都可能漂）。
                // 抽成一个内联组件后，改一处三组一起变。
                component GroupTitle: Text {
                    leftPadding: 0
                    topPadding: 0
                    rightPadding: 0
                    bottomPadding: 0
                    text: ""
                    color: th.fg4
                    font.pixelSize: 12
                }

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

                // ── 通知：老师用得最多、也最该一眼看到，所以压在右栏最顶上 ──
                //    它要**凑内容**（形态/标题/正文/时长/播报/紧急），所以占整行、用强调描边。
                Column {
                    spacing: 6
                    ActBtn { d: ({ l: "通知", f: function () {
                                            // 通知不是"点一下就发"——它要凑形态/标题/正文/时长/播报，所以开弹窗再发。
                                            openOnly("notify")
                                        }, t: false }); wide: true; heavy: true }
                }

                // ── 电源：这三个点下去就不可逆，所以独占整行、描边比别人重 ──
                Column {
                    spacing: 6
                    GroupTitle { text: "电源" }
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
                    GroupTitle { text: "看看" }
                    Grid {
                        columns: 2
                        columnSpacing: 7
                        rowSpacing: 7
                        Repeater {
                            model: [
                                { l: "截图",   a: "screenshot",       t: false },
                                { l: "软件",   f: function () {
                                    // 软件弹窗要两块数据：可打开（选自这台机器）+ 正在运行
                                    backend.sendAction("list_shortcut_candidates")
                                    backend.sendAction("process_list")
                                } },
                                { l: "监控",   a: "camera_list",      t: false },
                                { l: "多班墙", f: function () { openOnly("multiband") } },
                                { l: "音量",   f: function () { volumeDlg.open() } },
                                { l: "文件",   f: function () { openOnly("file") } }
                            ]
                            delegate: ActBtn { d: modelData }
                        }
                    }
                }

                // ── 任务：会让教室机在未来某个时点自己做事 ──
                //    远控开/关已从这里移除：选中班级（设备）即进入远控，开关是多余的（见 selectDevice）。
                Column {
                    spacing: 6
                    GroupTitle { text: "任务" }
                    ActBtn { d: ({ l: "定时任务", f: function () {
                                            // 先拉一次，回执到了（onResultReceived）再开弹窗 —— 免得开出来是空的
                                            backend.sendAction("list_schedules")
                                            openOnly("sched")
                                        }, t: false }); wide: true }
                    // ⚠️ 远程终端是**开发者排障通道**（能跑任意命令），不占老师日常界面。
                    //    入口收进命令面板（Ctrl+K 搜「终端」），避免误点。
                }

                // ── 批量：对"一批机器"生效的动作，统一收在这个面板里 ──
                //    广播 / 考试模式 / 语音对讲 / 屏幕广播（2026-10-11 从已删的「批量管控」页搬来）
                Column {
                    spacing: 6
                    GroupTitle { text: "批量" }
                    ActBtn { d: ({ l: "管控…", f: function () {
                                            openOnly("control")
                                        }, t: false }); wide: true }
                }

                // （这里原先有个「给被控端打字」的输入框：老师日常用不上，且和通知/终端重复，
                //   2026-10-11 按需求移除；要打字走命令面板或终端。）

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
                            SvgIcon {
                                name: modelData.ok ? "check" : "x"
                                tint: modelData.ok ? th.inv : th.op
                                width: 12
                                height: 12
                                anchors.verticalCenter: parent.verticalCenter
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
            }       // 右栏动作（ColumnLayout，装在 Flickable 里，见上面的说明）
            }       // 右栏 Flickable
            }       // 三栏 RowLayout
            }       // 控制页 Item


            // ══ 2 设置 ══
            // 稿 03：pad + 两列卡片（账户 / 连接 / 提醒 / 外观）+ 通栏「关于」。
            // 现在只有「连接」「外观」两张是真的：前者读运行时状态，后者深浅切换确实做了。
            // 账户、提醒没有数据源 —— 规则要求"不假装能用"，所以卡片位置留着、内容留白，
            // 而不是像以前那样写一句「尚未接入（无数据源）」的开发说明（规则 1）。
            Item {
                Rectangle { anchors.fill: parent; color: th.body }

                // ⚠️ 2026-10-08：这里**必须可滚动**。设置页内容本来就超过一屏（六张卡），
                //    1280×720 的教室机上更紧。以前 GridLayout 直接 anchors.fill —— 内容一超
                //    就把最后几行挤成零高，「关于」整张卡（含「检查更新」）在屏幕上根本点不到
                //    （有截图实证）。改成 Flickable + 内容高度：卡片按内容撑开，超出的滚动看。
                //    注：下面各卡的 Layout.fillHeight 在这个「按内容定高」的网格里是惰性的，
                //    同一行的等高等由 GridLayout 本身保证，留着只是万一以后又改回填充式。
                Flickable {
                    id: settingsFlick
                    anchors.fill: parent
                    anchors.margins: 18
                    contentWidth: width
                    contentHeight: settingsGrid.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        width: 6
                    }

                    GridLayout {
                        id: settingsGrid
                        width: parent.width
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
                            Layout.preferredHeight: colSettings1.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings1
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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
                            Layout.preferredHeight: colSettings2.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings2
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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
                            Layout.preferredHeight: colSettings3.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings3
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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

                        // ── 启动（2026-10-09 · 用户清单第②条：开机自启 / 桌面快捷方式）──
                        // 管理端是「老师自己的机器」⇒ 走**当前用户**范围、**免提权**：
                        //   自启写 HKCU\...\Run；快捷方式建在当前用户桌面。
                        // 状态直接读后端属性（backend.autoStart / desktopShortcut = 现读注册表/桌面），
                        // 不缓存到 QML，避免"界面说开着、注册表其实没有"的假象。
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: colSettingsBoot.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettingsBoot
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 16
                                spacing: 14
                                Text { text: "启动"; color: th.fg3; font.pixelSize: 12 }
                                Column {
                                    width: parent.width
                                    spacing: 4
                                    ToggleRow {
                                        width: parent.width
                                        theme: th
                                        text: "开机自动启动"
                                        note: "登录 Windows 后自动打开管理端（写当前用户注册表，免管理员）"
                                        checked: backend.autoStart
                                        onToggled: function (on) { backend.setAutoStart(on) }
                                    }
                                    ToggleRow {
                                        width: parent.width
                                        theme: th
                                        text: "桌面快捷方式"
                                        note: "在桌面放一枚「星集控管理端」图标"
                                        checked: backend.desktopShortcut
                                        onToggled: function (on) { backend.setDesktopShortcut(on) }
                                    }
                                }
                            }
                        }

                        // ── 外观（真：深浅切换在这版已经可用）──
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: colSettings4.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings4
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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
                            Layout.preferredHeight: colSettings5.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings5
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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
                            Layout.preferredHeight: colSettings6.implicitHeight + 32
                            Layout.fillHeight: true
                            radius: th.rCard
                            color: th.panel
                            border.color: th.card
                            border.width: 1
                            Column {
                                id: colSettings6
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
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
                                // 更新日志 / 帮助：把官网已有的「版本历史」与「帮助」能力搬进管理端（需求 #7）。
                                // 之前管理端缺这两块、也没有替代，老师想看更新了什么只能去翻网页。
                                Row {
                                    spacing: 8
                                    Rectangle {
                                        width: 96; height: 30; radius: th.rCtrl
                                        color: clHover.containsMouse ? th.hover : th.panel
                                        border.color: th.stroke; border.width: 1
                                        Text { anchors.centerIn: parent; text: "更新日志"; color: th.fg; font.pixelSize: 13 }
                                        MouseArea { id: clHover; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: openOnly("changelog") }
                                    }
                                    Rectangle {
                                        width: 80; height: 30; radius: th.rCtrl
                                        color: hpHover.containsMouse ? th.hover : th.panel
                                        border.color: th.stroke; border.width: 1
                                        Text { anchors.centerIn: parent; text: "帮助"; color: th.fg; font.pixelSize: 13 }
                                        MouseArea { id: hpHover; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: openOnly("help") }
                                    }
                                }
                                // ── 更新（2026-10-08）──
                                // 状态机在 C++ 的 Updater 里，界面只做三件事：把 updater.message 原样显示、
                                // 按状态决定按钮文案与灰否、把更新说明摊开。「有没有新版」是云端算的
                                // （客户端只把本地版本号发过去），界面不自己比版本。
                                // ⚠️ 行数都封顶：这张卡在 GridLayout 里，涨太高会把上面的卡挤扁。
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    visible: updater.message !== ""
                                    text: updater.message
                                    color: updater.state === "failed" ? th.fg : th.fg3
                                    font.pixelSize: 12
                                    font.bold: updater.state === "failed"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    visible: updater.state === "available" && updater.notes !== ""
                                    text: updater.sizeBytes > 0
                                          ? ("更新说明：" + updater.notes +
                                             "（" + (updater.sizeBytes / 1048576).toFixed(1) + " MB）")
                                          : ("更新说明：" + updater.notes)
                                    color: th.fg3; font.pixelSize: 12
                                }
                                RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    Btn {
                                        Layout.preferredWidth: 118
                                        Layout.preferredHeight: 26
                                        theme: th
                                        text: updater.busy ? "处理中…" : "检查更新"
                                        tip: "向云端问一下有没有新版本"
                                        disabled: updater.busy
                                        onClicked: updater.checkForUpdate()
                                    }
                                    // 只在"确实有新版本"时出现 —— 常驻一颗不能点的按钮只会让人犯嘀咕。
                                    Btn {
                                        Layout.preferredWidth: 118
                                        Layout.preferredHeight: 26
                                        theme: th
                                        visible: updater.state === "available"
                                        strong: true
                                        text: updater.mandatory ? "必须更新" : "立即更新"
                                        tip: "下载 → sha256 校验 → 解压 → 自动重启完成更新，不用手动覆盖"
                                        disabled: updater.busy
                                        onClicked: updater.installUpdate()
                                    }
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
                    // 2026-10-11：原来第三个标签「批量管控」是一整页卡片（通知/广播/考试/语音/监控…），
                    // 与「集控」页割裂 —— 老师要在两个页面之间来回跳才能管完一台机器。
                    // 按「一站式」需求：那一页**删掉**，卡片改成集控页右栏按钮点开的弹窗，
                    // 于是底部只剩三个标签：0 概览 / 1 集控 / 2 设置。
                    model: [ "概览", "集控", "设置" ]
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

    // #93 监控＋摄像头绑定：把写好的 CameraCenter 组件真正挂进界面（此前只建未挂，
    // 所以「摄像头监控」功能在界面上完全够不着）。由监控区「摄像头监控」按钮打开。
    // 摄像头枚举 / 拍照回执经 backend.resultReceived 转发给 CameraCenter。
    Popup {
        id: camCenterDlg
        anchors.centerIn: parent
        width: 560
        height: 440
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: th.panel
            radius: th.rCard
            border.color: th.card
            border.width: 1
        }
        CameraCenter {
            id: camCenter
            theme: root.th
            backend: root.backendRef
            anchors.fill: parent
            anchors.margins: 12
        }
    }

    // ── 多班监控缩略图墙（2026-10-11：原在「批量管控」页，该页已删，改成弹窗）──
    // 一屏看多个教室画面（3×3 网格），点格子切到那台看大画面。
    // 订阅前 9 台在线设备；帧头 uid → backend 按 uid 分槽缓存缩略图。
    // ⚠️ 打开即订阅、关闭即退订：否则关掉弹窗后还在后台偷偷拉 9 路流。
    Popup {
        id: multiThumbDlg
        anchors.centerIn: Overlay.overlay
        width: 640
        height: 520
        modal: true
        padding: 0
        background: Rectangle {
            color: th.panel
            radius: th.rCard
            border.color: th.card
            border.width: 1
        }
        onOpened: multiThumb.watch()
        onClosed: backend.subscribeThumbnails([])

        Column {
            id: thumbCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Row {
                width: parent.width
                spacing: 8
                Text { text: "多班监控（缩略图墙）"; color: th.fg3; font.pixelSize: 11 }
                Button {
                    text: "刷新"
                    onClicked: {
                        multiThumb.watch()
                        root.toast("正在订阅多班缩略图…")
                    }
                }
            }

            // 网格：最多 9 台（在线优先），每格一个设备的缩略图
            GridLayout {
                id: multiThumb
                width: parent.width
                columns: 3
                columnSpacing: 6
                rowSpacing: 6

                property var watched: []   // 已订阅的设备 uid

                function watch() {
                    // 收集在线的设备 uid（最多 9 台），保持设备表顺序
                    var uids = []
                    var devs = backend.devices || []
                    for (var i = 0; i < devs.length && uids.length < 9; ++i) {
                        if (devs[i] && devs[i].online && devs[i].uid
                                && uids.indexOf(devs[i].uid) < 0)
                            uids.push(devs[i].uid)
                    }
                    watched = uids
                    backend.subscribeThumbnails(uids)
                }

                Repeater {
                    model: multiThumb.watched
                    delegate: Rectangle {
                        id: cell
                        width: (multiThumb.width - 12) / 3
                        height: 96
                        radius: th.rCtrl
                        color: th.canvas
                        border.color: th.stroke
                        border.width: 1
                        clip: true

                        // 缩略图：帧更新时 tick++ 换缓存键
                        Image {
                            id: thumbImg
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            // ⚠️ 2026-10-10：tick 是**本 Image** 的属性，原来却写 `cell.tick++`
                            //   （cell 是那一格的 Rectangle，没这属性）⇒ 每来一帧都刷
                            //   `Cannot assign to non-existent property "tick"`，缓存键永不刷新、
                            //   缩略图看着"不更新"。改用 Image 自己的 id 引用。
                            property int tick: 0
                            source: "image://frames/" + modelData + "?t" + tick
                            Connections {
                                target: backend
                                function onThumbnailChanged(uid) { if (uid === modelData || uid === "") thumbImg.tick++ }
                            }
                        }

                        // uid 标签（底部小条，别盖住画面太多）
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 18
                            color: Qt.rgba(0, 0, 0, 0.55)
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#FAFAFA"
                                font.pixelSize: 9
                                elide: Text.ElideRight
                                width: parent.width - 8
                            }
                        }

                        // 点击 → 切到这台看大画面（控制页）
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                backend.setCurrentUid(modelData)
                                root.page = 1   // 集控页（带大画面）
                                multiThumbDlg.close()
        controlDlg.close()
                                root.toast("已切换查看 " + modelData)
                            }
                        }

                        // 空态：还没收到这台帧
                        Text {
                            anchors.centerIn: parent
                            visible: thumbImg.tick === 0
                            text: "等待画面…"
                            color: th.fg4
                            font.pixelSize: 10
                        }
                    }
                }
            }

            Text {
                text: "最多展示 9 台在线设备；点格子切换到该教室大画面。离线设备不占格。"
                color: th.fg3; font.pixelSize: 10; wrapMode: Text.Wrap
            }
        }
    }

    // ── 管控面板（2026-10-11：原在「批量管控」页的 4 张卡片，该页已删，改成弹窗）──
    // 广播 / 考试模式 / 语音对讲 / 屏幕广播 —— 都是"对一批机器生效"的批量动作，
    // 所以放在同一个面板里挨着排，不用老师在几个页面之间跳。
    Popup {
        id: controlDlg
        anchors.centerIn: Overlay.overlay
        width: 560
        height: 560
        modal: true
        padding: 0
        background: Rectangle {
            color: th.panel
            radius: th.rCard
            border.color: th.card
            border.width: 1
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: 16
            contentHeight: ctlCol.height
            clip: true
            Column {
                id: ctlCol
                width: parent.width
                spacing: 14

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
                // 2026-10-09 改**黑名单**：语义从"名单之外全杀"反转为"只杀名单里的"。
                //   起因是学校机房反复重启 —— 白名单制下没列全的系统进程会被杀，
                //   杀到 critical 的 svchost 就是 CRITICAL_PROCESS_DIED(0xEF) 蓝屏。
                //   黑名单的好处是"配错了最多没杀干净，绝不把机器弄崩"。
                // ⚠️ 别为了"更严格"把这里改回白名单，也别给默认黑名单塞进程名。
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
                        Text { text: "考试模式（全屏拦截 + 黑名单进程）"; color: th.fg3; font.pixelSize: 12 }
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
                                id: examBlacklist
                                width: 200
                                placeholderText: "黑名单(逗号分隔，如 chrome,steam)"
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
                                    if (!uid) { root.toast("先在控制页选中一台设备"); return }
                                    var blacklist = []
                                    examBlacklist.text.split(",").forEach(function(s) {
                                        var t = s.trim()
                                        if (t) blacklist.push(t)
                                    })
                                    backend.sendAction("exam_mode", {
                                        "minutes": parseInt(examMinutes.text, 10) || 0,
                                        "blacklist": blacklist
                                    })
                                    root.toast("考试模式已下发 → " + uid)
                                }
                            }
                            Btn {
                                theme: th
                                width: 90
                                text: "结束考试"
                                onClicked: {
                                    const uid = backend.currentUid
                                    if (!uid) { root.toast("先在控制页选中一台设备"); return }
                                    backend.sendAction("exam_mode_stop", {})
                                    root.toast("已请求结束考试 → " + uid)
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
                        Text { text: "语音对讲（老师讲话，全班听）"; color: th.fg3; font.pixelSize: 12 }
                        Text { text: "默认关闭；到点自动停（按钮上显示剩余秒）"; color: th.fg4; font.pixelSize: 12 }
                        Row {
                            spacing: 8
                            Button {
                                text: backend.speaking ? ("停止讲话（剩 " + backend.speakLeftSec + " 秒）") : "开始讲话"
                                onClicked: {
                                    if (backend.speaking) {
                                        backend.stopSpeaking()
                                        root.toast("语音已停止（静音）")
                                    } else {
                                        if (backend.startSpeaking()) {
                                            root.toast("正在讲话，全班可听；再次点击停止")
                                        } else {
                                            root.toast("开麦失败：" + backend.speakError)
                                        }
                                    }
                                    root.refreshIsland()   // 灵动岛语音胶囊跟手开/关
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
                        Text { text: "屏幕广播（老师屏幕 → 全部在线设备）"; color: th.fg3; font.pixelSize: 12 }
                        Text { text: "默认关闭；到点自动停（按钮上显示剩余秒）"; color: th.fg4; font.pixelSize: 12 }
                        Row {
                            spacing: 8
                            Button {
                                text: backend.broadcasting ? ("停止广播（剩 " + backend.bcastLeftSec + " 秒）") : "开始广播"
                                onClicked: {
                                    if (backend.broadcasting) {
                                        backend.stopBroadcast()
                                        root.toast("屏幕广播已停止")
                                    } else {
                                        if (backend.startBroadcast()) {
                                            root.toast("正在屏幕广播；再次点击停止")
                                        } else {
                                            root.toast("广播启动失败")
                                        }
                                    }
                                    root.refreshIsland()   // 灵动岛广播胶囊跟手开/关
                                }
                            }
                        }
                    }
                }

            }
        }
    }

    // #93：摄像头枚举 / 拍照 / 自检 / 绑定回执 → CameraCenter（与云端 instruction-result 路由对齐）
    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, err, detail, data) {
            if (action === "camera_list") {
                camCenter.onCams(data || {})
            } else if (action === "camera_snapshot") {
                camCenter.onShotResult(result === "done", detail || err)
            } else if (action === "camera_test") {
                camCenter.onTestResult(result === "done", detail || err, data || {})
            } else if (action === "camera_bind") {
                camCenter.onBindResult(result === "done", err || detail, data || {})
            }
        }
    }

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
    // 不用 root.page = 2 写在组件里：页号是主界面的事，别让它去猜。
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

        // 不许让状态只走"记得手动刷一次"这条路：
        //  · 自动停（到点 / 断线）必须让老师看见 —— 按钮会自己变回"开始讲话"，
        //    但不弹一句，老师只觉得"怎么自己断了，也没提示"。所以 autoNote 非空就弹一次。
        //  · 语音 / 广播状态变了（含被自动停掉），灵动岛要跟着重算一次。
        Connections {
            target: backend
            // ⚠️ 2026-10-10：统一改成 `function onX(...)` 函数式声明 —— 老写法（直接写属性名）
            //   Qt 6 已弃用，会刷 "Implicitly defined onFoo properties in Connections are deprecated"；
            //   而 `onIslandNote: island.showNote(title, desc, icon, …)` 更严重：隐式参数注入同样
            //   弃用，且上面还刷 "Parameter \"title\" is not declared"。改函数式后参数显式、告警消失。
            function onAutoNoteChanged() { if (backend.autoNote !== "") root.toast(backend.autoNote); root.refreshIsland() }
            function onSpeakingChanged() { root.refreshIsland() }
            function onBroadcastingChanged() { root.refreshIsland() }
            // 设备表一变（有人掉线 / 新机上线 / 掉线重连）就重算：
            // 掉线是教室里最高频的异常，之前灵动岛对它是瞎的。
            function onDevicesChanged() { root.refreshIsland() }
            // 云端状态通知（断线重连 / 指令执行完成）统一走灵动岛，不再弹系统托盘气泡
            function onIslandNote(title, desc, icon) { island.showNote(title, desc, icon, 3000) }
        }
        // 启动时没有任何输入进来，灵动岛从来没被刷过一次（一直是空的）。
        // 这里补一次：空的也得是"真的空"。
        Component.onCompleted: root.refreshIsland()

        DynamicIsland { id: island; theme: root.th; states: root.islandStates }
    }

    // 批量的超时兜底（见 batchTimeoutClose）。放窗口层：跟老师在哪个标签页无关。
    Timer {
        id: batchTimeout
        repeat: false
        onTriggered: root.batchTimeoutClose()
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

    // 选择类（4.7）：锁屏是最高频也最容易误伤的操作，给它两个键不加面板；
    // Ctrl+L 的单键形态和 Ctrl+Shift+L 的全量形态分开，避免"我本来只想锁一台"。
    Shortcut { sequence: "Ctrl+A";     onActivated: { root.selectAll() } }
    Shortcut { sequence: "Esc";        onActivated: { root.clearPick() } }
    Shortcut { sequence: "Ctrl+L";     onActivated: { root.runOnPicked("lock") } }
    Shortcut { sequence: "Ctrl+Shift+L"; onActivated: { root.runOnPicked("") } }

    Component.onCompleted: {
        syncPageLive()   // 2026-10-08：启动即按当前页归一化拉流（默认 page=1，缩略图墙不预订阅）
        acctMenu.goSettings = function () { root.page = 2 }
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
            { name: "看可打开的软件",   keys: ["software", "ruanjian", "rj"], tip: "这台机器上有的程序 + 正在跑的", run: function () { root.runAction({ l: "软件", a: "process_list", t: false }) } },
            { name: "监控（摄像头）",   keys: ["camera", "shexiangtou", "sxt"], tip: "这台机器上有几个摄像头", run: function () { root.runAction({ l: "监控", a: "camera_list", t: false }) } },
            { name: "调音量",           keys: ["volume", "yinliang", "yl"], tip: "打开音量浮层", run: function () { root.runAction({ l: "音量", a: "", t: false }) } },
            { name: "分发文件",         keys: ["file", "wenjian", "wj"], tip: "选一个文件推给教室机", run: function () { root.runAction({ l: "文件", a: "", t: false }) } },
            { name: "定时任务",         keys: ["schedule", "dingshi", "ds"], tip: "排一个定时关机 / 重启", run: function () { root.runAction({ l: "定时任务", a: "list_schedules", t: false }) } },
            // ── 选择（4.4 / 4.7）──────────────────────────────────────
            // 这四条走的是和多选按钮同一条路（runOnPicked），免得命令面板和按钮两条逻辑各写一版。
            { name: "选中全部在线设备", keys: ["selectall", "quanxuan", "qx"], tip: "Ctrl+A：把在线设备全选上", run: function () { root.selectAll() } },
            { name: "取消选择",         keys: ["clearpick", "quxiaoxuan", "qxz"], tip: "Esc/点空白：把选中的机器清掉", run: function () { root.clearPick() } },
            { name: "锁屏选中的设备",   keys: ["locksel", "suoxuan", "sx"], tip: "Ctrl+L：只锁刚才选中的那几台", run: function () { root.runOnPicked("lock") } },
            { name: "锁屏全部在线设备", keys: ["lockall", "quanbu", "qb"], tip: "Ctrl+Shift+L：所有在线机器一起锁（要管理员身份）", run: function () { root.runOnPicked("") } },
            // ── 视图 ───────────────────────────────────────────────────
            { name: "切到概览",         keys: ["overview", "gailan", "gl"], tip: "在线设备 + 最近回执 + 今日课表", run: function () { root.page = 0 } },
            { name: "切到控制",         keys: ["control", "kongzhi", "kz"], tip: "看教室机画面 + 下发动作", run: function () { root.page = 1 } },
            { name: "切到集控",         keys: ["jikong", "jk"], tip: "看教室机画面 + 下发动作", run: function () { root.page = 1 } },
            { name: "切到设置",         keys: ["settings", "shezhi", "sz"], tip: "账户 / 连接 / 提醒 / 外观", run: function () { root.page = 2 } },
            { name: "去广播给所有在线设备", keys: ["broadcast", "guangbo", "gb"], tip: "广播要填内容，点开弹窗填再发", run: function () { root.page = 1; openOnly("broadcast") } },
            { name: "开始语音对讲",     keys: ["voice", "yuyin", "yy"], tip: "老师讲话，全班在线设备可听（再次点停）", run: function () { if (backend.startSpeaking()) { root.refreshIsland(); root.toast("正在讲话") } else { root.toast("开麦失败：" + backend.speakError) } } },
            { name: "停止语音对讲",     keys: ["voice", "yuyin", "yy"], tip: "把语音广播收掉（静音）", run: function () { backend.stopSpeaking(); root.refreshIsland(); root.toast("语音已停止") } },
            { name: "开始屏幕广播",     keys: ["screen", "guangbo2", "gb2"], tip: "老师屏幕推给全部在线设备（再次点停）", run: function () { if (backend.startBroadcast()) { root.refreshIsland(); root.toast("正在屏幕广播") } else { root.toast("广播启动失败") } } },
            { name: "停止屏幕广播",     keys: ["screen", "tingbo"], tip: "把屏幕广播收掉", run: function () { backend.stopBroadcast(); root.refreshIsland(); root.toast("屏幕广播已停止") } },
            { name: "开始考试模式",     keys: ["exam", "kaoshi", "ks"], tip: "全屏拦截 + 黑名单（无时长=手动结束）", run: function () { const uid = backend.currentUid; if (!uid) { root.toast("先在控制页选中一台设备"); return }; backend.sendAction("exam_mode", { "minutes": 0, "blacklist": [] }); root.toast("考试模式已下发 → " + uid) } },
            { name: "结束考试模式",     keys: ["exam", "kaoshi", "ks"], tip: "恢复教室机（全屏拦截收掉）", run: function () { const uid = backend.currentUid; if (!uid) { root.toast("先在控制页选中一台设备"); return }; backend.sendAction("exam_mode_stop", {}); root.toast("已结束考试 → " + uid) } },
            { name: "刷新多班监控墙",   keys: ["multiband", "duoban", "db"], tip: "重订阅多班缩略图（最多 9 台在线）", run: function () { root.page = 1; openOnly("multiband") } },
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
        changelogDlg.close()
        helpDlg.close()
        multiThumbDlg.close()
        controlDlg.close()
        if      (which === "software") softwareDlg.open()
        else if (which === "log")      logDlg.open()
        else if (which === "media")    mediaDlg.open()
        else if (which === "sched")    schedDlg.open()
        else if (which === "file")     fileDlg.open()
        else if (which === "notify")   notifyDlg.open()
        else if (which === "term")     termDlg.open()
        else if (which === "changelog") changelogDlg.open()   // 需求 #7
        else if (which === "help")       helpDlg.open()        // 需求 #7
        else if (which === "multiband")  multiThumbDlg.open()  // 多班缩略图墙（2026-10-11 从「批量管控」页搬来）
        else if (which === "control")    controlDlg.open()    // 广播/考试/语音/屏幕广播（同上）
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

    // 回执流水：打字这类"点下去就知道在办"的动作也给它一条。
    // 等被控端回话要几百毫秒，界面上什么都不变，用户会以为没发出去。
    // ⚠️ property var 只有在**整体重新赋值**时才重绘 —— 往数组里 push 是不触发的，必须 slice 出新数组。
    function pushReceipt(label, ok) {
        var next = [{ ok: ok, uid: root.currentUid, action: label }]
        for (var i = 0; i < root.results.length && next.length < 20; ++i)
            next.push(root.results[i])
        root.results = next
    }

    // ── 概览页快捷操作分发（4.7 效率功能，2026-10-07）──
    // 与命令面板同款调用（复用集控卡片/命令逻辑，不绕权限门控）。
    // a ∈ lock | notify | broadcast | voice | screen | exam | multiband
    function quickAction(a) {
        if (a === "lock") {
            if (!root.permOk("lock")) { backend.reportDenied("锁屏"); return }
            root.runAction({ l: "锁屏", a: "lock", t: false, p: "admin" })
        } else if (a === "notify") {
            root.page = 1; openOnly("notify")
        } else if (a === "broadcast") {
            root.page = 1; openOnly("broadcast")
        } else if (a === "voice") {
            if (backend.speaking) { backend.stopSpeaking(); root.toast("语音已停止") }
            else if (backend.startSpeaking()) { root.toast("正在讲话") }
            else { root.toast("开麦失败：" + backend.speakError) }
            root.refreshIsland()
        } else if (a === "screen") {
            if (backend.broadcasting) { backend.stopBroadcast(); root.toast("屏幕广播已停止") }
            else if (backend.startBroadcast()) { root.toast("正在屏幕广播") }
            else { root.toast("广播启动失败") }
            root.refreshIsland()
        } else if (a === "exam") {
            const uid = backend.currentUid
            if (!uid) { root.toast("先在控制页选中一台设备"); return }
            backend.sendAction("exam_mode", { "minutes": 0, "blacklist": [] })
            root.toast("考试模式已下发 → " + uid)
        } else if (a === "multiband") {
            root.page = 1; openOnly("multiband")
        }
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

        // ⚠️ 判据一律按**动作名**（d.f 自定义 / d.a 一条指令），绝不按中文文案。
        //   2026-10-07 改：这条链原来是 if (d.l === "音量") … 一串 ——
        //   把按钮文案从「音量」改成「喇叭」，这条链就断了：点下去走 sendAction("") 空转，
        //   界面上什么报错都没有（灰按钮永远点不到，只有从状态行 / 快捷键触发才露出来）。
        if (typeof d.f === "function") { d.f(); return }
        if (d.a !== "") { backend.sendAction(d.a, {}); return }

        // 兜底：老调用点（命令面板 / 快捷键）只给了中文 label 的情况
        const legacy = { "音量": "volume", "探活": "ping", "软件": "process_list", "定时": "list_schedules" }
        if (legacy[d.l]) { backend.sendAction(legacy[d.l], {}); return }
        backend.reportUnsupported(d.l, "这一条没有动作");
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
        island.showNote(s, "", "bell", 3000)
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
        // 语音对讲 / 屏幕广播：有真实状态源（backend.speaking / broadcasting）
        // 才亮；没在讲/没在播就不出现（宁可不亮，不冒充聚合器）
        if (backend.speaking)
            s.push({ k: "voice", t: "正在语音对讲 " + backend.speakLeftSec + " 秒",
                     d: "老师讲话，全班在听；到点自动停" })

        if (backend.broadcasting)
            s.push({ k: "broadcast", t: "正在屏幕广播 " + backend.bcastLeftSec + " 秒",
                     d: "老师屏幕推给全部在线设备；到点自动停" })
        // 设备离线（3.8 的"告警"档）：这是教室里最高频的异常 —— 有人拔了网线、
        // 机器睡了、被控端被杀。之前这一档只有"批量下发失败"会亮，
        // 于是设备表那边掉线一片，灵动岛却纹丝不动 = 看着像没接上。
        // 认云端给的 online 字段，老版本云端不带这个键时按在线处理（和设备表同口径）。
        const off = root.offlineDevices()
        if (off.length > 0)
            s.push({ k: "alert", t: off.length + " 台设备离线",
                     d: deviceLabel(off[0].uid) + (off.length > 1 ? " 等 " + off.length + " 台" : ""),
                     p: -1 })

        islandStates = s
    }

    // 设备表里"没在线"的那几台（online === false 才算离线；字段缺失按在线）。
    function offlineDevices() {
        const arr = root.devices || []
        const off = []
        for (let i = 0; i < arr.length; ++i)
            if (arr[i] && arr[i].online === false) off.push(arr[i])
        return off
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
    // ⚠️ 记账单位是**设备**，不是回执条数（2026-10-07 修）：一屋子 30 台，
    // 通知每条都会回两条（先 done、学生再确认回一条 confirmed），
    // 按条数计 → 第 2 台确认就把 2/30 顶到 2/30、第 3 台…收口文案直接报"30 台都成了"。
    // 改成 rows：每台一个格子，一台只有第一次回执能落账，回馈才是"按设备逐个发出"。
    function batchStart(label, list) {
        const total = (list && list.length) ? list.length : 0
        batch = { label: label, total: total, done: 0, ok: 0, fail: 0, active: true, rows: [] }
        // 先把名单铺成明细行（st：0 待回话 / 1 成功 / 2 失败），离线那几台随后直接判失败
        for (let i = 0; i < total; ++i)
            batch.rows.push({ uid: list[i], st: 0, why: "", late: false })
        // 收口兜底：设备掉线、云端不回执时，不能让这句话永远挂在灵动岛上。
        // 台数越多给的时间越长（每台 4 秒），卡在 10s ~ 60s 之间。
        batchTimeout.interval = Math.max(10000, Math.min(60000, total * 4000))
        batchTimeout.restart()
        refreshIsland()
    }

    // 取某台设备在这批里的明细行（找不到就补一个"非本批"行，绝不丢回馈）
    function batchRowOf(uid) {
        for (let i = 0; i < batch.rows.length; ++i)
            if (batch.rows[i].uid === uid) return batch.rows[i]
        return null
    }

    // 某台设备现在在不在线（认云端给的 online；老云端不带这个键就按在线处理）
    function isDevOnline(uid) {
        for (let i = 0; i < root.devices.length; ++i)
            if (root.devices[i].uid === uid) return (root.devices[i].online !== false)
        return false
    }

    // 设备显示名：明细点名要用（没有就退回 uid，好过显示 undefined）
    function deviceLabel(uid) {
        for (let i = 0; i < root.devices.length; ++i)
            if (root.devices[i].uid === uid) return (root.devices[i].name || root.devices[i].uid || uid)
        return (uid || "未知设备")
    }

    // 二次回执（通知确认）：只写明细，不参与进度 —— 一台设备 = 一份回馈
    function batchNote(uid) {
        if (!batch.active) return
        let row = batchRowOf(uid)
        if (!row) { row = { uid: uid, st: batch.done >= batch.total ? 1 : 0, why: "", late: false }; batch.rows.push(row) }
        row.late = true
        refreshIsland()
    }

    // 超时收口：没回话的按"没成"结账，并且说出来 —— 不假装成功
    function batchTimeoutClose() {
        if (!batch.active) return
        const left = Math.max(0, batch.total - batch.done)
        batch.done = batch.total
        batch.fail = batch.fail + left
        for (let i = 0; i < batch.rows.length; ++i)
            if (batch.rows[i].st === 0) batch.rows[i].st = 2
        batch.active = false
        toast(batch.label + "：等超时了，" + batch.ok + " 台确认"
              + (left > 0 ? "，" + left + " 台没回话（多半是掉线）" : ""))
        refreshIsland()
    }

    // 收尾文案按设备逐条点名：只说"29 成 1 败"，老师还得一台台猜是谁；
    // 把没成的直接点出来（最多 6 台，剩下的补一句"…等 N 台"）。
    function batchSummary() {
        const bad = []
        for (let i = 0; i < batch.rows.length; ++i)
            if (batch.rows[i].st === 2) bad.push(batch.rows[i])
        let s = batch.label + "：完了 " + batch.ok + " 台"
        if (bad.length > 0) {
            s += "，" + bad.length + " 台没成："
            for (let i = 0; i < bad.length && i < 6; ++i) {
                const r = bad[i]
                s += (i > 0 ? "、" : "") + deviceLabel(r.uid) + (r.why ? "（" + r.why + "）" : "")
            }
            if (bad.length > 6) s += " 等 " + bad.length + " 台"
        }
        return s
    }

    function batchStep(uid, ok, why) {
        if (!batch.active) return
        let row = batchRowOf(uid)
        if (!row) {
            // 单台下发时这条回执可能先于批量名单落账 —— 补行并把它算进去，别让这台凭空消失
            row = { uid: uid, st: 0, why: "", late: false }
            batch.rows.push(row)
            batch.total = batch.total + 1
        }
        if (row.st !== 0) {
            // 这台已经结过账（离线直判失败 / 已经报过成功），后面回来的都是同一台的二次回执。
            // 只把最新状态补进明细，绝不重复计数 —— 这是"按设备"而不是"按回执"的关键一句。
            row.late = true
            if (!ok) row.why = why || row.why
            refreshIsland()
            return
        }
        row.st = ok ? 1 : 2
        row.why = why || ""
        row.late = false
        batch.done = batch.done + 1
        if (ok) batch.ok = batch.ok + 1
        else batch.fail = batch.fail + 1
        if (!ok) root.pushAlert(uid, batch.label, why || "")
        if (batch.total > 0 && batch.done >= batch.total) {
            // 全成了也得说一句（4.5）："没消息"不该被当成"都成了"
            batch.active = false
            batchTimeout.stop()
            toast(batchSummary())
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
            // 范围选择走当前显示的过滤后列表（界面显示的就是它，anchor/索引才对齐）
            const list = (root.deviceFilter !== "all") ? root.filteredDevices : root.devices
            if (root.pickAnchor < 0 || root.pickAnchor >= list.length) {
                root.picked = [uid]
                root.pickAnchor = idx
                return
            }
            const a = Math.min(root.pickAnchor, idx), b = Math.max(root.pickAnchor, idx)
            const s2 = []
            for (let i = a; i <= b; ++i) s2.push(list[i].uid)
            root.picked = s2
        } else {
            root.picked = [uid]
            root.pickAnchor = idx
            backend.currentUid = uid        // 单击照旧把画面切过去（原本就是这个行为）
        }
    }
    function selectAll() {
        // 只选**在线**的：设备表现在含离线台账（见 backend 的说明），把离线的一起选上，
        // 批量下发会给它们记一串注定失败的账，看着像"发失败了"，其实是机器没开。
        const s2 = []
        for (let i = 0; i < root.devices.length; ++i) {
            if (root.devices[i].online !== false) s2.push(root.devices[i].uid)
        }
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
            // 「全部在线」就真的是全部在线：设备表含离线台账，把离线的一起发过去
            // 只会换来一串注定失败的回执（机器没开），账面上像"下发失败"。
            for (let i = 0; i < root.devices.length; ++i) {
                if (root.devices[i].online !== false) list.push(root.devices[i].uid)
            }
        } else {
            for (let i = 0; i < root.picked.length; ++i) list.push(root.picked[i])
        }
        if (list.length === 0) {
            toast("还没有可操作的设备")
            return
        }
        const label = (a === "lock" ? "锁屏" : a === "shutdown" ? "关机" : a)
        root.batchStart(label + (list.length > 1 ? " " + list.length + " 台" : ""), list)
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

    // ── 更新日志（需求 #7：把官网已有的「版本历史」能力搬进管理端）──
    // 数据面是 changelogModel（内嵌、与官网同源），离线也能看；官网那份是发布归档唯一真源，这里只读镜像。
    Popup {
        id: changelogDlg
        anchors.centerIn: Overlay.overlay
        width: 560; height: 460
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: th.win; radius: th.rCard; border.color: th.card; border.width: 1 }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Text { text: "更新日志"; color: th.fg; font.pixelSize: 16; font.weight: Font.Medium }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                Column {
                    spacing: 18
                    Repeater {
                        model: root.changelogModel
                        delegate: Column {
                            spacing: 6
                            width: changelogDlg.width - 36
                            Row { spacing: 8
                                Text { text: modelData.version; color: th.inv; font.pixelSize: 13; font.weight: Font.Medium }
                                Rectangle { width: 2; height: 12; color: th.stroke }
                                Text { text: modelData.date; color: th.fg3; font.pixelSize: 12 }
                            }
                            Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.title; color: th.fg; font.pixelSize: 14; font.weight: Font.Medium }
                            Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.highlight; color: th.fg3; font.pixelSize: 12 }
                            Repeater {
                                model: modelData.items
                                delegate: Row { spacing: 7; width: parent.width
                                    Text { text: ({ feat: "新增", fix: "修复", change: "调整" })[modelData.kind] || "·"; color: th.fg3; font.pixelSize: 12; width: 36 }
                                    Text { width: parent.width - 43; wrapMode: Text.Wrap; text: modelData.text; color: th.op; font.pixelSize: 13 }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 30; radius: th.rCtrl
                color: th.hover; border.color: th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "关闭"; color: th.fg; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; onClicked: changelogDlg.close() }
            }
        }
    }

    // ── 帮助（需求 #7：把官网帮助能力搬进管理端）──
    Popup {
        id: helpDlg
        anchors.centerIn: Overlay.overlay
        width: 480; height: 420
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: th.win; radius: th.rCard; border.color: th.card; border.width: 1 }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Text { text: "帮助"; color: th.fg; font.pixelSize: 16; font.weight: Font.Medium }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                Column {
                    spacing: 12
                    width: helpDlg.width - 36
                    Text { width: parent.width; wrapMode: Text.Wrap; text: "星集控管理端用来统一管教室里的被控端（教室机）。下面是常见操作："; color: th.fg3; font.pixelSize: 13 }
                    Repeater {
                        model: [
                            { t: "看某台教室机的画面", d: "控制页（集控）左边选设备，画面出现在中间。" },
                            { t: "给全班发通知 / 广播", d: "控制页右侧或集控页，发通知可选普通/重要/紧急，广播一次发全员。" },
                            { t: "按班级筛选设备", d: "设备列表上方点「全部班级」切换班级，只显示该班机器。" },
                            { t: "锁屏 / 重启 / 关机", d: "控制页右侧「电源」分组，需管理员身份。" },
                            { t: "看版本更新历史", d: "设置页「更新日志」。" },
                            { t: "快速跳页与触发动作", d: "用顶部的命令入口（或快捷键）直接跳到概览/集控/批量管控/设置，并触发常用动作。" }
                        ]
                        delegate: Column {
                            spacing: 3
                            width: parent.width
                            Text { text: modelData.t; color: th.fg; font.pixelSize: 13; font.weight: Font.Medium }
                            Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.d; color: th.fg3; font.pixelSize: 12 }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 30; radius: th.rCtrl
                color: th.hover; border.color: th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "关闭"; color: th.fg; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; onClicked: helpDlg.close() }
            }
        }
    }

    // ── 首启引导（批次6：应用感；方案 §2.1）──
    // 未完成（backend.firstRunDone == false）时主窗叠这一层；走完三步点「完成」、或任意步点「跳过」都写标记收起。
    // 内联而非独立 WelcomePage.qml：th 目前是 root 属性、不是单例（批次7 待抽），独立文件够不到 th。
    Item {
        id: welcomeLayer
        z: 9995                       // 压在页面与账户菜单之上
        anchors.fill: parent
        visible: !backend.firstRunDone
        property int step: 0          // 0 选班级 / 1 登录绑定 / 2 完成
        function finish() { backend.markFirstRunDone() }

        Rectangle { anchors.fill: parent; color: th.win }   // 整屏遮底：首启不让人看到后面空窗

        Rectangle {
            anchors.centerIn: parent
            width: 560; height: 388
            radius: th.rWin
            color: th.body
            border.color: th.card; border.width: 1

            // 跳过（任意步都能跳过，写标记收起）
            Text {
                anchors.top: parent.top; anchors.right: parent.right
                anchors.topMargin: 14; anchors.rightMargin: 16
                text: "跳过"; color: th.fg4; font.pixelSize: 12
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: welcomeLayer.finish() }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 28
                anchors.topMargin: 46
                spacing: 14

                Text {
                    text: ["① 选班级", "② 登录绑定", "③ 启动方式", "④ 完成"][welcomeLayer.step]
                    color: th.fg; font.pixelSize: 16; font.weight: Font.Medium
                }

                // 步骤 0：选班级
                Column {
                    visible: welcomeLayer.step === 0
                    width: parent.width; spacing: 12
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: "先选你管的班级，后面设备列表默认只显示这个班。"; color: th.fg3; font.pixelSize: 13 }
                    Flickable {
                        width: parent.width; height: 30
                        contentWidth: Math.max(wClsRow.implicitWidth, wClsRow.width); clip: true
                        Row {
                            id: wClsRow; spacing: 6
                            Repeater {
                                model: (function () {
                                    var arr = [{ code: "", name: "全部班级" }]
                                    var cs = backend.classes || []
                                    for (var i = 0; i < cs.length; ++i) arr.push({ code: cs[i].code, name: (cs[i].name || cs[i].code) })
                                    return arr
                                })()
                                // 同 clsRow：chip 宽度取 implicitWidth，别绕回 centerIn 的 Text 算宽度
                                delegate: Rectangle {
                                    height: 26; width: wLbl.implicitWidth + 16
                                    radius: th.rCtrl
                                    color: (backend.currentClass === modelData.code) ? th.inv : th.hover2
                                    border.color: (backend.currentClass === modelData.code) ? th.inv
                                                                                            : th.stroke
                                    border.width: 1
                                    Text {
                                        id: wLbl; anchors.centerIn: parent
                                        text: modelData.name
                                        color: (backend.currentClass === modelData.code) ? th.win : th.fg3
                                        font.pixelSize: 12
                                        font.weight: (backend.currentClass === modelData.code)
                                                     ? Font.DemiBold : Font.Normal
                                    }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.setCurrentClass(modelData.code) }
                                }
                            }
                        }
                    }
                }

                // 步骤 1：登录绑定
                Column {
                    visible: welcomeLayer.step === 1
                    width: parent.width; spacing: 12
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: "登录网站账号，才能用云端票连设备（也可用 viewer.env 里的静态令牌，跳过这步）。"; color: th.fg3; font.pixelSize: 13 }
                    Rectangle {
                        width: 200; height: 32; radius: th.rCtrl; color: th.inv
                        Text { anchors.centerIn: parent; text: "登录网站账号"; color: th.win; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.loginWithSite() }
                    }
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: backend.accountText; color: th.fg4; font.pixelSize: 12; visible: backend.accountText !== "" }
                }

                // 步骤 2：启动方式（2026-10-09 · 用户清单第②条：引导里就能设自启/快捷）
                Column {
                    visible: welcomeLayer.step === 2
                    width: parent.width; spacing: 12
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: "要不要让它开机就自己起来、顺手在桌面放个图标？（之后随时能在设置页改）"; color: th.fg3; font.pixelSize: 13 }
                    ToggleRow {
                        width: parent.width; theme: th
                        text: "开机自动启动"
                        note: "登录 Windows 后自动打开（当前用户，免管理员）"
                        checked: backend.autoStart
                        onToggled: function (on) { backend.setAutoStart(on) }
                    }
                    ToggleRow {
                        width: parent.width; theme: th
                        text: "桌面快捷方式"
                        note: "在桌面放一枚「星集控管理端」图标"
                        checked: backend.desktopShortcut
                        onToggled: function (on) { backend.setDesktopShortcut(on) }
                    }
                }

                // 步骤 3：完成
                Column {
                    visible: welcomeLayer.step === 3
                    width: parent.width; spacing: 12
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: "都好了。以后想改，去设置页或右下角账户菜单。"; color: th.fg3; font.pixelSize: 13 }
                    Text {
                        width: parent.width; wrapMode: Text.Wrap; color: th.fg; font.pixelSize: 13
                        text: "当前班级：" + (function () {
                            if (!backend.currentClass) return "全部班级"
                            var cs = backend.classes || []
                            for (var i = 0; i < cs.length; ++i) if (cs[i].code === backend.currentClass) return (cs[i].name || cs[i].code)
                            return backend.currentClass
                        })()
                    }
                }

                Item { width: 1; height: 8 }

                // 底部：上一步 / 下一步 / 完成
                Row {
                    width: parent.width; spacing: 10
                    Rectangle {
                        visible: welcomeLayer.step > 0
                        width: 96; height: 32; radius: th.rCtrl
                        color: th.hover; border.color: th.stroke; border.width: 1
                        Text { anchors.centerIn: parent; text: "上一步"; color: th.fg; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: welcomeLayer.step = Math.max(0, welcomeLayer.step - 1) }
                    }
                    Item { width: Math.max(0, parent.width - (welcomeLayer.step > 0 ? 106 : 0)
                            - (welcomeLayer.step < 3 ? 96 : 110)); height: 1 }
                    Rectangle {
                        visible: welcomeLayer.step < 3
                        width: 96; height: 32; radius: th.rCtrl; color: th.inv
                        Text { anchors.centerIn: parent; text: "下一步"; color: th.win; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: welcomeLayer.step = Math.min(3, welcomeLayer.step + 1) }
                    }
                    Rectangle {
                        visible: welcomeLayer.step === 3
                        width: 110; height: 32; radius: th.rCtrl; color: th.inv
                        Text { anchors.centerIn: parent; text: "完成"; color: th.win; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: welcomeLayer.finish() }
                    }
                }
            }
        }
    }
}

