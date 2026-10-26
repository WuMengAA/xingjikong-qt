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
import "components/theme"   // Md3 令牌（pragma Singleton，2026-10-25；dark 由主外壳统一写）
import "components/md3"    // MD3 组件库（Md3Btn/Card/…；令牌见上一行 components/theme 的 singleton）
// 2026-10-22：全局主框架（App Scaffold · MainWindow）——原 pages/Md3Shell.qml 更名移入。
// 它是管理端唯一应用外壳（侧栏+顶栏+内容区+状态条），业务页经它挂载；见 AGENTS.md §9。
import "shell"
import "shell/ClassSort.js" as ClassSort   // 引导第一步的班级 chip 顺序（1班<…<12班）
// 2026-10-18：截图预览窗直接声明在本文件（回执驱动，不走 UiPanelHost 清单）
// ⇒ 必须 import "panels" 才能按类型名实例化 panels/ScreenshotPreviewDialog.qml。
import "panels"
import "island"          // SvgIcon 的唯一真源在共享岛目录（Stelarith-shared-qml/island/，构建期拷到 qml/island/）——
                      // 2026-10-16 修：A 档把 viewer 自己的 components/SvgIcon.qml 当"重复件"删了，
                      // 但那 4 个同名件里只有它的接口是超集且被**全套面板 + Main.qml**在用；
                      // 另 3 个（IslandClock/MarqueeText/WhaleMascot）确实只被岛用，删对了。
import "UiPlugins.js" as UiPlugins   // U3：界面声明清单（「加一个界面=加一条声明」的入口）
import "ActionDefs.js" as ActionDefs // U4：设备动作 / 命令的**唯一真源**（右栏 + 快捷行 + 命令面板
                                     // 三处都从它派生；以前同一批动作在这三处各有一份硬编码）

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
    // 2026-10-11：手机端（Android）隐藏桌面专属（开机自启/屏幕广播等）。
    // 桌面/手机同源一套 Main.qml，桌面功能零影响。
    readonly property bool isPhone: Qt.platform.os === "android"
    // 响应式（三·响应式增量层，2026-10-12）：窗口窄到 < 1040 进入 compact，左「设备」栏自动收起
    // （监控画面吃满宽度），由中栏「设备列表」按钮按需展开。完整三栏重排属 dsh S2 Main.qml 重构。
    readonly property bool compact: root.width < 1040
    property bool devOpen: false
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

    // ── 主题令牌（E4：默认浅色 + 三档持久化）──
    // 页面里一律从 th 取色，不写死 —— 否则换主题必花。
    // darkMode 由 backend.themeMode 派生：0 浅色 / 1 深色 / 2 跟随系统（取 backend.systemDark）。
    // 真源 = backend.themeMode（QSettings ui/themeMode，默认 0 浅色），界面只读 darkMode。
    readonly property bool darkMode: (backend.themeMode === 1) ? true
                                      : (backend.themeMode === 2 ? backend.systemDark : false)

    // 主题 Token 单一真源：shared-qml/ui/theme/Md3.qml（构建期拷到 components/theme/；
    // 用户批准 MaterialLab 为全项目唯一模板）。深 / 浅两套由 dark 切换。
    // Md3 提供 legacy 别名（win/panel/fg/acc/rCtrl…）⇒ 本文件 3802 行零改动换肤；
    // 组件逐个迁 md3/ 后别名块删除。原 Theme.qml 全端迁移完成后退役。
    // 2026-10-25（用户点单「MD3 太难控制」）：Md3 改 pragma Singleton（真源
    // shared-qml/ui/theme/ + qmldir），本文件是**桌面端唯一 dark 写入口**。
    // 原先 darkTh/lightTh 两个实例只是因为 th 声明成 property var 需要预构造对象，
    // 实际 th 只取其一（darkMode ? darkTh : lightTh），从未同时显示两套 ——
    // 所以「需要深浅两套并存」的旧理由不成立，singleton 后全局 binding 自动随动。
    readonly property var th: Md3
    Binding { target: Md3; property: "dark"; value: root.darkMode }

    // 界面自身的状态（只跟显示有关的东西）
    // 2026-10-22（用户点单「把新界面全面替换老界面」）：默认页由 1（旧壳集控）改为
    // **6（MD3 主界面）**。旧壳的 1-5 页一律保留，作为下钻目标（骨架侧栏「运行监控」
    // 跳 3、SideSheet「在集控页打开」跳 1、「返回经典界面」也回 1），功能不丢。
    // 想临时看旧壳：侧栏底部「返回经典界面」，或命令行 --page 1（若后续需要）。
    property int page: 6

    /** 栏目跳转通道：由 MainWindow 启动时注册（打开某个新栏目）。
     *  旧壳 7 页栈已退役（2026-10-24 旧界面迁移第三刀），但外部仍有 20+ 处按**老页号**
     *  调 goPage（弹窗、侧滑面板、业务页的「返回」等）—— 这里把老页号**映射**成新栏目，
     *  调用点不必逐个改，且语义仍对得上（老 page 1 是集控，新 control 也是集控）。 */
    property var sectionHook: null

    /** 旧页号 → 新栏目 key。6（主框架）不在此表里。 */
    readonly property var _sectionOfPage: ({
        0: "dash", 1: "control", 2: "settings", 3: "monitor", 4: "classes", 5: "classes"
    })

    // 2026-10-08 修复「选中设备却不在带画面页 → 无限拉流」：
    // 切到其它页就退订当前设备画面 / 多班缩略图墙，云端按订阅发帧，退订即停流。
    // 2026-10-11：「批量管控」页已删，多班墙改由「多班墙」弹窗显式订阅（见 multiThumbDlg），
    // 不再跟随页签自动拉 —— 否则每次切页都在后台偷偷订阅一遍。
    // 2026-10-24：单台大画面的拉流开关改由 MainWindow 按栏目驱动（只有它知道当前 section），
    // 这里只保留多班墙退订；page 收敛逻辑见下方 onPageChanged。
    onPageChanged: {
        if (root.page !== 6) {
            // 谁写了老页号（例如 MultiThumbDialog 直接 `page = 1`）都收敛到主框架，
            // 再按映射切栏目 —— 一个入口，不会出现"页号变了但界面没动"。
            const key = root._sectionOfPage[root.page]
            root.page = 6
            if (key && root.sectionHook) root.sectionHook(key)
            return
        }
        root.syncPageLive()
    }
    function syncPageLive() {
        if (!panelHost.isOpen("multiband")) backend.subscribeThumbnails([])
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
            title: "分班制与离线设备可见",
            highlight: "按班级筛选设备，离线设备不再消失。",
            items: [
                { kind: "feat", text: "设备列表按班级筛选" },
                { kind: "feat", text: "离线设备常驻列表，状态由连接派生" },
                { kind: "feat", text: "概览新增云端存储概况" },
                { kind: "feat", text: "新增「更新日志」与「帮助」" },
                { kind: "change", text: "「控制」更名「集控」" }
            ]
        },
        {
            version: "2026.10.06", date: "2026-10-06",
            title: "集控页上线",
            highlight: "通知、定时任务、广播集中到一页。",
            items: [
                { kind: "feat", text: "集控页：通知、定时任务、广播" },
                { kind: "feat", text: "课表编辑器：撤销重做、CSV 导入导出" },
                { kind: "fix", text: "关窗只收进托盘" }
            ]
        },
        {
            version: "2026.10.06", date: "2026-10-06",
            title: "被控端自更新，官网下载中心",
            highlight: "设备可自行升级，下载页版本号自动同步。",
            items: [
                { kind: "feat", text: "官网下载中心，版本与校验值读云端" },
                { kind: "feat", text: "被控端 OTA 自更新" },
                { kind: "fix", text: "被控端只允许开一个" }
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

    // ── 批量指令 / 告警记账（2026-10-26 P0：灵动岛已移除，状态只记账、落点见各条）──
    // 批量指令：下发 +1、回执 -1（4.5「进度可见」）。老师盯着数往上走，
    // 不让他对着一屋子设备猜"这会儿发到哪台了"。
    // rows = 明细行 [{ uid, st, why, late }]，每台一格，一台只落一次账（4.5 按设备记账）。
    property var batch: ({ label: "", total: 0, done: 0, ok: 0, fail: 0, active: false, rows: [] })
    // 未处理告警（4.3 场景四）：回执失败、文件没推成都算；pushAlert 时立即 toast（岛已移除）。
    property var alerts: []
    // 2026-10-21（审查 #8）：告警可清除 —— 批量收口时自动清掉本批攒的告警。
    function clearAlerts() { root.alerts = [] }
    // 多选的设备 uid（4.4）。和 currentUid 是两套东西：那个管"看哪台"，
    // 这个管"给哪些台发" —— 混在一处会互相踢（台数变了不知道该信哪个）。
    property var picked: []

    // ── 危险操作二次确认（2026-10-12 · UI 评审二.3）──
    // 锁屏 / 重启 / 关机 点错一次就是几十台教室机，光靠"按钮排得开、颜色区分"防不住误触。
    // ⚠️ 判据一律按**动作名**（lock/reboot/shutdown），绝不按中文文案 ——
    //    2026-10-07 踩过：把按钮文案从「音量」改成「喇叭」，按文案判的那条链当场断。
    property string dangerAct: ""        // 待确认的动作名（"" = 当前没有待确认的）
    property int    dangerN: 0           // 会中招的台数
    property string dangerOne: ""        // 只有 1 台时的目标 uid（显示用）
    property var    dangerList: []       // 批量时的完整 uid 列表（单台时为空）
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
    // 「正在接通」：选了某台机器、画面还没出第一帧，且 VNC 链路确实在建
    // （vncState 由 backend 维护：idle → 连接中 → 已连接 / 连接失败）。
    // 只写"还没出帧"会把"被控端根本没起 VNC 服务"也误报成在忙，让用户空等。
    readonly property bool linkBusy: backend.currentUid !== ""
                                     && backend.frameCount === 0
                                     && backend.vncState === "连接中"

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
            //
            // ⚠️ 2026-10-18 用户反馈「截图后没有传回图片且没有预览界面」——根因：
            //    被控端只把 PNG 存到**教室机本地**，回执里只有 path/bytes/width/height，
            //    图片内容从未回传，管理端自然无图可看。
            //    现在：回执若带 `data.thumb`（JPEG base64，新被控端才发）⇒ 弹**预览窗**；
            //    不带（老被控端）⇒ 保持原来的 toast + 提示"该设备未回传预览图"，
            //    不搞假成功、也不弹空白框。
            if (action === "screenshot") {
                if (result === "done" && data && data.path !== undefined) {
                    if (data.thumb && data.thumb.length > 0) {
                        shotDlg.showShot({
                            uid: uid, path: data.path,
                            bytes: data.bytes, width: data.width, height: data.height,
                            thumb: data.thumb
                        })
                        root.toast("截图已传回，见预览窗")
                    } else {
                        root.toast("已截图（该设备未回传预览图）→ 设备 " + uid + "：" + data.path
                                   + "（" + (data.width || "?") + "×" + (data.height || "?")
                                   + "，" + Math.round((data.bytes || 0) / 1024) + " KB）")
                    }
                } else {
                    root.toast("截图失败 → " + uid + "：" + (error || "未知原因"))
                }
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
                panelHost.item("software").items = data.items
                panelHost.item("software").truncated = (data.truncated === true)
                openOnly("software")
            } else if (action === "log_tail" && data && data.lines) {
                panelHost.item("log").lines = data.lines
                panelHost.item("log").logPath = data.path || ""
                openOnly("log")
            } else if (action === "media_list" && data) {
                // 删完自动重拉时也会走到这儿（弹窗已开着）→ 先灌数据再开，开的时候顺手关掉别的
                panelHost.item("media").setItems(data.items || [], data.dir || "", data.truncated === true)
                openOnly("media")
            } else if (action === "list_schedules" && data) {
                panelHost.item("sched").setItems(data.items || [], data.file || "")
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

            // ── ① 所有功能必须做到有反馈（2026-10-19 · 用户指令）────────────
            // 之前成功回执**只有特例有 toast**（截图/音量），其余只进"最近回执"
            // 流水列表（隐性，老师看不见）。这里在通用回执处补**默认成功反馈**：
            //   · done 且非静默/面板/批量/多选类 ⇒ toast「XXX 成功 → 教室机XX」
            //   · 失败（非 done）已由上面 pushAlert 覆盖，不重复
            // 排除清单（这些成功已有别的反馈或无需打扰）：
            //   面板/拉数据：notify / screenshot / set_volume / process_list /
            //     log_tail / media_list / list_schedules / list_shortcut_candidates
            //   批量（batch.active 已逐台反馈）；多选（pick-* 走 runOnPicked 自己的汇总）
            //   相机 / 语音 / 屏幕广播 / 考试模式：均有专属面板或状态源
            else if (!batch.active && action.indexOf("pick-") !== 0) {
                var _quiet = { notify:1, screenshot:1, set_volume:1, process_list:1,
                               log_tail:1, media_list:1, list_schedules:1,
                               list_shortcut_candidates:1, camera_list:1, camera_test:1,
                               camera_bind:1, camera_snapshot:1, terminal_open:1 };
                if (!_quiet[action]) {
                    var _title = root.actionTitleOf(action)
                    if (_title !== "")
                        root.toast(_title + " 成功 → " + uid)
                }
            }
        }
    }

    // ══════════════ 主结构 ══════════════
    ColumnLayout {
        anchors.fill: parent
        spacing: 0


        // ── 主体：只有一页（MainWindow 是唯一外壳）──
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 2026-10-24：旧壳页已删，这里只剩 MainWindow 一项，索引恒 0。
            // page 属性仍保留作**兼容层**（外部按老页号调 goPage），但不再驱动界面。
            currentIndex: 0

            // ══ 旧壳 0-5 页已退役（2026-10-24 旧界面迁移第三刀）══
            // 概览/集控/设置/监控/看板/班级详情 的界面全部迁入 MainWindow 的五个栏目
            // （dash/classes/control/monitor/settings）。原内联段 ~560 行已删除；
            // 对应 .qml 文件保留（ControlPage/SettingsPage/MonitorPage 由新壳 Loader 复用，
            // AdminDashboard/ClassDetailPage 暂留待删）。老页号 → 新栏目的映射见 goPage。
            // ══ 6 全局主框架 MainWindow ══（2026-10-20 V1.0 P1 立骨架；2026-10-22 更名）
            // 唯一应用外壳：240px 侧栏 + 64px 顶栏 + 操作区 + 响应式设备网格 + 400px 侧滑面板
            // + 底部 32 状态条。旧壳 1-5 页保留作下钻目标（侧栏「运行监控」跳 3、
            // SideSheet「在集控页打开」跳 1、「返回经典界面」回 1），功能不丢。
            // ⚠️ 新页面一律长在本骨架里（AGENTS.md §9），禁止自绘第二个外壳。
            MainWindow { ctx: root._pageCtx }
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

    // ── 语音对讲（2026-10-11 集控界面直达：按住讲话 + 自动接收播放班级语音）──

    // 诊断与反馈（2026-10-12）：GUI 子系统后没控制台，出错看这里；托盘菜单也会拉起它。
    DiagFeedbackDialog { id: diagDlg; theme: root.th }
    // 托盘「诊断与反馈…」→ 打开弹窗（C++ 发信号，QML 收）
    Connections {
        target: backend
        function onDiagFeedbackRequested() { diagDlg.refreshLog(); diagDlg.open() }
    }

    // 被控端访问密码弹窗（2026-10-26）：接管启用了访问密码的设备前，云端要求先鉴权。
    VncAuthDialog { id: vncAuthDlg; theme: root.th }
    Connections {
        target: backend
        function onVncAuthRequired(uid) {
            // 用 devices 解析设备显示名（拿不到就退回 uid 本身）
            var name = uid
            var arr = backend.devices
            for (var i = 0; i < arr.length; i++) {
                if (arr[i].uid === uid) {
                    name = arr[i].name || arr[i].label || arr[i].hostname || uid
                    break
                }
            }
            vncAuthDlg.showFor(uid, name)
        }
        function onVncAuthFailed(uid, reason) { vncAuthDlg.showError(reason) }
        function onVncAuthOk(uid) { vncAuthDlg.close() }
    }

    // 通知弹窗（2026-10-06 加）：被控端的大屏通知早就实现了，只有管理端这个发送方漏了。
    // 2026-10-15 组件6 配套：互动审批面板（点歌/祝福待审 + 右侧「学生在教室机上会看到」
    // 的岛预览 —— 那颗岛与被控端**是同一份** IslandRoot.qml，真源在
    // Stelarith-shared-qml/island/）。
    // 注意类名是 FilePushDialog 不是 FileDialog：同个文件里要 import QtQuick.Dialogs 拿原生
    // 选文件对话框，两个 FileDialog 撞名 qml 编译期就冲突了，所以弹窗这侧改名（2026-10-03）

    // #93 监控＋摄像头绑定：把写好的 CameraCenter 组件真正挂进界面（此前只建未挂，
    // 所以「摄像头监控」功能在界面上完全够不着）。由监控区「摄像头监控」按钮打开。
    // 摄像头枚举 / 拍照回执经 backend.resultReceived 转发给 CameraCenter。

    // ── 多班监控缩略图墙（2026-10-11：原在「批量管控」页，该页已删，改成弹窗）──
    // 一屏看多个教室画面（3×3 网格），点格子切到那台看大画面。
    // 订阅前 9 台在线设备；帧头 uid → backend 按 uid 分槽缓存缩略图。
    // ⚠️ 打开即订阅、关闭即退订：否则关掉弹窗后还在后台偷偷拉 9 路流。

    // ── 管控面板（2026-10-11：原在「批量管控」页的 4 张卡片，该页已删，改成弹窗）──
    // 广播 / 考试模式 / 语音对讲 / 屏幕广播 —— 都是"对一批机器生效"的批量动作，
    // 所以放在同一个面板里挨着排，不用老师在几个页面之间跳。

    // #93：摄像头枚举 / 拍照 / 自检 / 绑定回执 → CameraCenter（与云端 instruction-result 路由对齐）
    Connections {
        target: backend
        function onResultReceived(uid, action, state, result, err, detail, data) {
            if (action === "camera_list") {
                panelHost.item("camera").onCams(data || {})
            } else if (action === "camera_snapshot") {
                panelHost.item("camera").onShotResult(result === "done", detail || err)
            } else if (action === "camera_test") {
                panelHost.item("camera").onTestResult(result === "done", detail || err, data || {})
            } else if (action === "camera_bind") {
                panelHost.item("camera").onBindResult(result === "done", err || detail, data || {})
            }
        }
    }

    // 课表编辑器独立窗口（2026-10-06 并入）：设置页按钮 schedEditor.show() 打开
    ScheduleEditor { id: schedEditor; visible: false }

    // ══ U3：界面插件注册表 + 面板宿主（声明驱动）══════════════════════════
    // 真源在 Stelarith-shared-qml/ui/（与岛同模式；两端将来共用同一份）。
    // 原来「有哪些面板、怎么开」写死在本文件的 if-else 里，现在写进 qml/UiPlugins.js。
    UiRegistry {
        id: uiReg
        scope: "viewer"
        // 管理端当前没有"权限授权"这条链路 ⇒ 传空数组（契约是**容错**口径：
        // 环境未上报权限时一律放行，不会因为这里空着就把面板藏起来）。
        permissions: []
        // 被控端上报的指令能力（选择设备后由云端广播带下来）；本清单的面板都没声明
        // caps，故当前不影响可见性 —— 接线是给 U4 的动作面用的。
        capabilities: backend.capActions
        manifest: UiPlugins.manifest().concat(ActionDefs.manifest())
    }
    UiPanelHost {
        id: panelHost
        // ⚠️ 必须铺满窗口：面板里有 8 个用 `anchors.centerIn: parent` 定位（其余 6 个用
        //    Overlay.overlay）。这些面板以前内联在本文件里，parent 就是窗口 ⇒ 居中正确；
        //    现在 parent 变成了宿主，宿主没尺寸的话它们全会缩到左上角。
        anchors.fill: parent
        registry: uiReg
        theme: root.th
        // 面板透传用：从 Main.qml 抽出的那 6 个面板靠 ctx.window 够到主窗口
        // （原来它们在同一文件里直接用 `root.`）。
        window: root
        scope: "viewer"
        // entry 相对本文件所在目录（qml/）解析 —— 声明里写 "LogDialog.qml" /
        // "panels/CameraCenterDialog.qml" 都能对得上。copy_directory 保留目录树，
        // 故源码树与构建产物里的解析结果一致。
        entryBase: Qt.resolvedUrl(".")
    }

    // ── U5：「整屏页」的上下文（ctx）────────────────────────────────────
    // 抽出的页（`qml/pages/`）通过它够到窗口 / 令牌 / 面板宿主。形状与共享
    // `shared-qml/ui/UiPageHost.qml` 的 `_ctx()` 是**同一份契约**（UiContract.CTX_FIELDS）。
    // ⚠️ 页够不到**子对象 id**（`schedEditor` 这种）—— QML 的 id 不是属性，外部读不到
    //    ⇒ 由窗口包一层函数给它（见下面的 openScheduleEditor）。
    readonly property var _pageCtx: ({
        theme: root.th,
        window: root,
        registry: uiReg,
        host: panelHost,
        scope: "viewer",
        open:        function (id, args) { return panelHost.open(id, args) },
        close:       function () { return panelHost.closeAll() },
        closePanels: function () { return panelHost.closeAll() }
    })
    /** 设置页「课表编辑器」按钮用：`ScheduleEditor` 是窗口的子对象，外部访问不到。 */
    function openScheduleEditor() { schedEditor.show() }

    /** 系统设置「诊断与反馈」用（2026-10-25）。
     *  为什么必须有这层：`diagDlg` 是本文件的**组件内 id**，独立的 SettingsPage.qml
     *  看不见它 —— 那边原来写裸 `diagDlg.refreshLog()`，点下去只会抛 ReferenceError，
     *  而 GUI 子系统没有控制台，表现就是"按钮静默失灵"。与 openScheduleEditor 同款收口。 */
    function openDiagnostics() { diagDlg.refreshLog(); diagDlg.open() }

    /** 整屏页「返回/退出」用：页号是主界面的事，不让子组件猜（见下方 _pageCtx 注释）。
     *  2026-10-24 旧界面迁移第三刀：旧壳 7 页栈退役，本函数变成**老页号 → 新栏目**的
     *  兼容层 —— 老调用点（弹窗、侧滑面板、业务页「返回」）一行不改也落到正确位置：
     *      0→仪表盘 1→设备集控 2→系统设置 3→运行监控 4/5→班级看板 6→主框架本身
     *  RBAC 门禁不在这里了：唯一判定在 MainWindow.navActivated（绕过去就是开第二个口子）。 */
    function goPage(idx) {
        if (idx === undefined || idx === null || idx >= 6) { root.page = 6; return }
        const key = root._sectionOfPage[idx]
        root.page = 6
        if (!key) return
        if (root.sectionHook) root.sectionHook(key)
    }

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

    // ── 截图预览窗（2026-10-18 · 用户反馈「截图后没有预览界面」）──────────
    // 直接在本文件声明而**不走 UiPanelHost 清单**，理由：本窗是**回执驱动**的
    // （收到 screenshot 回执就 showShot），不是用户点面板入口打开的；
    // 走清单得先 register 再 open，多一层且语义不符。
    // ctx 契约与抽出的 6 个 panels 一致（ctx.window 够到主窗口）。
    ScreenshotPreviewDialog {
        id: shotDlg
        ctx: ({ window: root })
    }

    // 2026-10-26（产品笔记 P0）：管理端移除灵动岛浮层（「无用/不好的信息通知来得方便」）。
    // 被移除项的落点：
    //  · autoNote（语音/广播自动停提示）→ 直接 toast，不依赖岛；
    //  · 云端状态通知（onIslandNote：断线重连/指令完成）→ toast 通道（bridge.showNote，3s）；
    //  · 批量进度 / 回执失败告警 → 完成/超时 toast + pushAlert 实时 toast；
    //  · 互动审批面板 InteractDialog 内的岛预览（用户 2026-10-15 点单）→ 保留。
    Connections {
        target: backend
        function onAutoNoteChanged() { if (backend.autoNote !== "") root.toast(backend.autoNote) }
        function onIslandNote(title, desc, icon) { bridge.showNote(title, desc, icon, 3000) }
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
    // 2026-10-21（审查 #1 同源）：首启引导期间全部快捷键禁用 —— 否则老师一边看引导
    // 一边按 Ctrl+L/Ctrl+A 就操作到了后台（引导层只挡鼠标，挡不住键盘）。
    // Esc 额外要求 picked 非空（审查 #9）：弹窗开着时按 Esc 不应被全局吃掉、抢了
    // Popup 的 CloseOnEscape。
    Shortcut { sequence: "Ctrl+K"; enabled: backend.firstRunDone; onActivated: { pal.open() } }
    Shortcut { sequence: "Ctrl+F"; enabled: backend.firstRunDone; onActivated: { pal.open() } }
    Shortcut { sequence: "F5";     enabled: backend.firstRunDone; onActivated: { backend.requestDevices() } }
    Shortcut { sequence: "Ctrl+1"; enabled: backend.firstRunDone; onActivated: { goPage(0) } }
    Shortcut { sequence: "Ctrl+2"; enabled: backend.firstRunDone; onActivated: { goPage(1) } }
    Shortcut { sequence: "Ctrl+3"; enabled: backend.firstRunDone; onActivated: { goPage(2) } }
    // 2026-10-20 P1：Ctrl+6 = MD3 新版界面骨架（页号 6，与 Ctrl+1..3 同一套心智）
    Shortcut { sequence: "Ctrl+6"; enabled: backend.firstRunDone; onActivated: { goPage(6) } }

    // 选择类（4.7）：锁屏是最高频也最容易误伤的操作，给它两个键不加面板；
    // Ctrl+L 的单键形态和 Ctrl+Shift+L 的全量形态分开，避免"我本来只想锁一台"。
    Shortcut { sequence: "Ctrl+A";     enabled: backend.firstRunDone; onActivated: { root.selectAll() } }
    Shortcut { sequence: "Esc";        enabled: backend.firstRunDone && root.picked.length > 0; onActivated: { root.clearPick() } }
    Shortcut { sequence: "Ctrl+L";     enabled: backend.firstRunDone; onActivated: { root.runOnPicked("lock") } }
    Shortcut { sequence: "Ctrl+Shift+L"; enabled: backend.firstRunDone; onActivated: { root.runOnPicked("") } }

    Component.onCompleted: {
        syncPageLive()   // 启动即归一化拉流（多班墙不预订阅）
        // 2026-10-24 迁移第三刀：旧壳顶条已删，其账户菜单（acctMenu）随之消失；
        // 「去设置」现在由 MainWindow 的 TopBar 头像承担（navActivated("settings")，
        // RBAC 只在那一处判）。原来这行 `acctMenu.goSettings = …` 已无对象可挂。
        // U4：命令面板条目同样从声明派生（surfaces 含 "command" 的那些）。
        // 每条回指声明 id，点击时统一走 root.runActionById —— 与右栏按钮、快捷行
        // **同一条路**，所以权限 / 能力 / 危险确认这三道再也绕不过去。
        pal.cmds = root.palCmds()
    }

    // ── 操作日志的 action → 中文（2026-10-14 加 · UI 反馈 #5）──
    // 右侧「最近回执」流水原来直接显示被控端回执里的英文 action 名
    // （如 remote_control_start），老师看不懂。只做 QML 侧映射，不动 C++ 数据结构；
    // 覆盖常见动作，未覆盖的回落原文（宁可露英文也不硬编错）。
    function actionLabel(a) {
        const m = {
            "lock": "锁屏", "shutdown": "关机", "reboot": "重启",
            "screenshot": "截图", "notify": "通知",
            "set_volume": "设置音量", "volume": "音量",
            "remote_control_start": "远程控制", "remote_control_stop": "停止远程控制",
            "process_list": "软件列表", "list_shortcut_candidates": "软件列表",
            "launch_app": "打开软件", "open_app": "打开软件",
            "camera_list": "摄像头列表", "camera_bind": "绑定摄像头",
            "camera_test": "摄像头测试", "camera_snapshot": "摄像头快照",
            "camera_record_start": "开始录像", "camera_record_stop": "停止录像",
            "camera_stream_start": "摄像头推流", "camera_stream_stop": "停止推流",
            "broadcast": "屏幕广播", "screen": "屏幕广播",
            "voice": "语音对讲", "exam_mode": "考试模式", "exam": "考试模式",
            "multiband": "多班墙", "file_push": "推送文件",
            "terminal_open": "远程终端", "terminal_input": "终端输入",
            "schedule_shutdown": "定时关机", "schedule_reboot": "定时重启",
            "noise_monitor_start": "噪音监测开", "noise_monitor_stop": "噪音监测关",
            "study_mode_start": "自习模式开", "study_mode_stop": "自习模式关",
            "self_update": "自动更新", "ping": "心跳"
        }
        return m[a] !== undefined ? m[a] : a
    }


    // ── 弹窗互斥的唯一入口（U3 起由 UiPanelHost 承担）──────────────────────
    // 以前这里是 14 行 closeAll + 14 行 if-else；现在「有哪些面板」写在声明清单
    // （qml/UiPlugins.js）里，宿主按 id 按需实例化，并保证同一时刻只开一个。
    // 保留同名同签名的包装 ⇒ 全文件 20+ 处调用点（右栏按钮 / 命令面板 / 快捷键）零改动。
    function openOnly(which) {
        // 兼容别名：命令面板里有一条 openOnly("broadcast")，而旧分支表里从来没有
        // "broadcast" ⇒ 点它等于什么都没发生（既有死分支，本轮实测确认）。广播现并进
        // 「管控」面板，故把别名指过去 —— 这是修 bug，不是新增行为。
        var id = (which === "broadcast") ? "control" : which
        return panelHost.open(id)
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
    /** 这个按钮要哪些指令才点得动（[d.a] 或按文案取到的数组）。没声明 = 不参与门控。 */
    /** 全部具备才放行。空数组 = 这条按钮不归门管。
     *  U4：需要哪些指令改问契约（uiReg.capNeeds = 显式 caps 优先、否则用 action 兜底），
     *  本文件里那份 capByLabel 手抄映射已删 —— 它就是"清单复制一份"病根本身。 */
    function capOkAll(p) {
        if (!p) return true
        const need = uiReg.capNeeds(p);
        return need.length === 0 ? true : need.every(capOk);
    }
    /** 权限门控：p 是权限键（"lock"/"reboot"/"terminal_open"/"broadcast"），空 = 谁都能做。
     *  判据一律问 backend（真实角色在那儿），界面不自己记一份"我以为我是管理员"。 */
    function permOk(p) {
        return !p || backend.mayDo(p);
    }

    // 回执流水：概览页的 results 由 onResultReceived 统一灌，这里不设第二个写口。
    // （2026-10-21：原 pushReceipt 定义后全文件无调用，死代码，已删。）

    // ── 概览页快捷操作分发（4.7 效率功能，2026-10-07）──
    // ══ U4：动作的三个投影 + 唯一执行入口 ═══════════════════════════════
    // 改造前「有哪些动作」写在三处（快捷行 model / 右栏 4 组 model / 命令面板 38 条字面量），
    // 执行也分三条路（quickAction 的 if-else 链 / runAction 的 d.f 分支 / 命令面板各自的 run 闭包）。
    // 现在清单只有 qml/ActionDefs.js 一处，执行只有下面 runActionById 一处。

    /** 概览页快捷行（声明里 quick:true；手机端滤掉 phone:false 的）。 */
    function quickList() {
        const all = uiReg.quickActions()
        if (!root.isPhone) return all
        return all.filter(function (p) { return p.phone !== false })
    }
    /** 多选批量条（声明里 bulk:true）。 */
    function bulkList() {
        var out = [], a = uiReg.actionItems()
        for (var i = 0; i < a.length; ++i) if (a[i].bulk) out.push(a[i])
        return out
    }
    /** 右栏分组：[{ name, items, wide }]（wide = 整组都独占整行 ⇒ 竖排，否则 2 列）。 */
    function rightGroups() {
        const names = uiReg.actionGroups()
        var out = []
        for (var i = 0; i < names.length; ++i) {
            const items = uiReg.actionsForGroup(names[i])
            var wide = items.length > 0
            for (var j = 0; j < items.length; ++j) if (!items[j].wide) wide = false
            out.push({ name: names[i], items: items, wide: wide })
        }
        return out
    }
    /** 命令面板条目：注册表给形状，这里补 run（回指声明 id，统一走 runActionById）。 */
    function palCmds() {
        const m = uiReg.commandModel()
        for (var i = 0; i < m.length; ++i) {
            m[i].run = (function (cid) {
                return function () { root.runActionById(cid) }
            })(m[i].pluginId)
        }
        return m
    }

    // ── 唯一执行入口 ──────────────────────────────────────────────────────

    // 回执里的 action 是**动作名**（如 "lock"、"remote_control_start"），
    // manifest 里 send 类记在 `action` 字段、panel/special 类只有 `id`。
    // 这里反查出中文标题供「所有功能必须有反馈」的默认成功 toast 用；
    // 查不到返回 ""（调用方决定不提示，避免英文 action 名裸奔到界面上）。
    function actionTitleOf(actionName) {
        const list = ActionDefs.manifest()
        for (var i = 0; i < list.length; i++) {
            const p = list[i]
            if (p.action === actionName || p.id === actionName)
                return p.title || p.palName || p.quickTitle || ""
        }
        return ""
    }
    // 闸门顺序与改造前一致：角色（看人）→ 能力（看机器）→ 危险确认。
    // 右栏按钮、快捷行、批量条、命令面板**全部**汇到这里 ⇒ 任何一条路都绕不过去。
    // opts.quick = 来自快捷行（那边历史上多一道 quickGuard，见 UiContract 注释）。
    function runActionById(id, opts) {
        const p = uiReg.byId(id)
        const o = opts || {}
        if (!p) { backend.reportUnsupported(String(id), "没有这条动作"); return false }
        const label = p.palName || p.title

        if (!root.permOk(p.role)) { backend.reportDenied(label); return false }
        if (o.quick && p.quickGuard && !root.permOk(p.quickGuard)) {
            backend.reportDenied(label); return false
        }

        const miss = uiReg.capMissing(p)
        if (miss.length > 0) { backend.reportUnsupported(label, miss.join(" / ")); return false }

        if (p.page >= 0) goPage(p.page)
        if (p.danger) {
            // 2026-10-21（审查 #3）：没选设备就别弹一个目标为空的确认框 —— 先明说。
            if (backend.currentUid === "") { root.toast("先在控制页选中一台设备"); return true }
            root.askDanger(p.action, 1, [], backend.currentUid)
            return true
        }

        // 2026-10-25：params 口子。原来是 `{}` 硬编码 —— 被控端一堆带参指令
        // （set_volume 的 value、noise_monitor 的 intervalMs）从管理端发出去全是
        // 默认值。无 params 的条目行为与改造前逐字一致（`|| {}`）。
        if (p.kind === "send") { backend.sendAction(p.action, p.params || {}); return true }
        if (p.kind === "panel") { openOnly(p.panel); return true }
        if (p.kind === "app") return root.runApp(p)

        return root.runSpecial(p.id)
    }

    /** app 类（本地命令，不下发设备）。分支只有这 5 个 —— 声明里的 app 字段已校验。 */
    function runApp(p) {
        if (p.app === "page")     { goPage(parseInt(p.payload, 10) || 0); return true }
        if (p.app === "theme")    { backend.themeMode = parseInt(p.payload, 10) || 0; return true }
        if (p.app === "role")     { backend.setRole(p.payload); return true }
        if (p.app === "login")    { backend.loginWithSite(); return true }
        if (p.app === "external") { backend.openExternal(p.payload); return true }
        backend.reportUnsupported(p.title, "未知的 app 类型：" + p.app)
        return false
    }

    /** 取**批量动作**的展示名（pick-* 系列，供批量 toast / 确认框用）。
     *  为什么单独一个函数：Main.qml:726 已有一个 actionLabel（操作日志的英文→中文
     *  硬编码映射，键是 lock/set_volume 这类动作名）—— QML 里同名函数 = 整页崩溃
     *  （Duplicate method name，2026-10-25 离屏冒烟实证，静态检查全放过）。
     *  两者键域不同不能合并：日志侧键是设备回执里的 action 名，这里键是注册表 id
     *  （pick-volume-mute…），故改名 pickActionLabel 各管一头。
     *  为什么要有它：MainWindow.runBulk 原来是
     *  `(actionId === "pick-lock") ? "锁定桌面" : "远程重启"` 的二元组 —— 只有两个
     *  批量动作时恰好蒙对，加第三个（课堂工具）就会把"全班静音"显示成"远程重启"。
     *  回执上写错动作名 = 骗人，故 label 也收敛到注册表这唯一真源。 */
    function pickActionLabel(id) {
        const p = uiReg.byId(id)
        return (p && p.title) ? p.title : id
    }

    /** special 类（需要多步/条件）。分支体**照搬**改造前各处原实现，行为不变。 */
    function runSpecial(id) {
        switch (id) {
        // ── 需要先拉数据再开面板 / 直接调浮层 ──
        case "software-list":
            backend.sendAction("list_shortcut_candidates")
            backend.sendAction("process_list")
            return true
        case "volume":
            volumeDlg.open(); return true
        case "schedule":
            backend.sendAction("list_schedules"); openOnly("sched"); return true
        case "alarm":
            backend.sendAction("list_alarms"); openOnly("alarm"); return true

        // ── 语音对讲（快捷行是切换语义，命令面板是分开的开 / 停）──
        case "voice-toggle":
            if (backend.speaking) { backend.stopSpeaking(); root.toast("语音已停止") }
            else if (backend.startSpeaking()) { root.toast("正在讲话") }
            else { root.toast("开麦失败：" + backend.speakError) }
            return true
        case "voice-start":
            if (backend.startSpeaking()) { root.toast("正在讲话") }
            else { root.toast("开麦失败：" + backend.speakError) }
            return true
        case "voice-stop":
            backend.stopSpeaking(); root.toast("语音已停止"); return true

        // ── 屏幕广播 ──
        case "screen-toggle":
            if (backend.broadcasting) { backend.stopBroadcast(); root.toast("屏幕广播已停止") }
            else if (backend.startBroadcast()) { root.toast("正在屏幕广播") }
            else { root.toast("广播启动失败") }
            return true
        case "screen-start":
            if (backend.startBroadcast()) { root.toast("正在屏幕广播") }
            else { root.toast("广播启动失败") }
            return true
        case "screen-stop":
            backend.stopBroadcast(); root.toast("屏幕广播已停止"); return true

        // ── 考试模式 ──
        // ⚠️ 原来这段在每个分支里各抄一遍（快捷行 exam / 命令面板 exam-start / exam-stop
        //    三处，uid 判空与 toast 文案逐字相同）。现在开 / 关各一份。
        case "exam-toggle":
        case "exam-start": {
            const uid = backend.currentUid
            if (!uid) { root.toast("先在控制页选中一台设备"); return true }
            backend.sendAction("exam_mode", { "minutes": 0, "blacklist": [] })
            root.toast("考试模式已下发 → " + uid)
            return true
        }
        case "exam-stop": {
            const uid = backend.currentUid
            if (!uid) { root.toast("先在控制页选中一台设备"); return true }
            backend.sendAction("exam_mode_stop", {})
            root.toast("已结束考试 → " + uid)
            return true
        }

        // ── 选择 / 批量（与多选按钮、Ctrl+L 走同一条 runOnPicked）──
        case "select-all":    root.selectAll(); return true
        case "pick-clear":    root.clearPick(); return true
        case "pick-lock":     root.runOnPicked("lock"); return true
        case "pick-lock-all": root.runOnPicked(""); return true
        case "pick-shutdown": root.runOnPicked("shutdown"); return true

        // ── 课堂工具（2026-10-25 · 教室小工具第一刀）──
        // 表驱动：指令名与参数**全部取自声明**（ActionDefs 的 action / params 字段，
        // params 是 UiContract 2026-10-25 新增的 object 字段）。宿主只做"批量执行"
        // 这一件事 —— 每工具各抄一行 runOnPicked 的话，加一个工具要改两处，
        // 漏一处就是静默不一致（U4 立件要治的正是这个病）。
        case "pick-volume-mute":
        case "pick-volume-low":
        case "pick-volume-high":
        case "pick-study-on":
        case "pick-study-off":
        case "pick-noise-on":
        case "pick-noise-off": {
            const q = uiReg.byId(id)
            root.runOnPicked(q.action, q.params)
            return true
        }

        // ── 系统 ──
        case "refresh-devices": backend.requestDevices(); return true
        }
        backend.reportUnsupported(id, "这一条没有动作")
        return false
    }

    // ── 危险操作二次确认（2026-10-12 · UI 评审二.3）──
    /** 必须二次确认的动作。**按动作名判**：lock / reboot / shutdown。 */
    function isDanger(a) { return a === "lock" || a === "reboot" || a === "shutdown" }
    function dangerLabel(a) {
        return a === "lock" ? "锁屏" : a === "reboot" ? "重启" : a === "shutdown" ? "关机" : a
    }
    /** 弹确认框。批量传 list（≥1 台），单台传 1 + 目标 uid。 */
    function askDanger(a, n, list, one) {
        root.dangerAct  = a
        root.dangerN    = n
        root.dangerList = (list && list.length) ? list : []
        root.dangerOne  = one || ""
        dangerDlg.open()
    }
    /** 确认后真下发。批量走 doBatch（原来的循环原样搬过去），单台走 sendActionTo。
     *  2026-10-21（审查 #3）：原来单台分支 sendAction 用的是**当时的 currentUid**，
     *  确认框开着期间用户切设备/后台同步变了 uid，就会发错机器而 toast 还骗人；
     *  现在发给确认时快照的 dangerOne，目标变了就拒绝（不静默发错）。 */
    function execDanger() {
        const a = root.dangerAct
        const one = root.dangerOne
        dangerDlg.close()
        root.dangerAct = ""; root.dangerOne = ""
        if (a === "") return
        if (root.dangerList.length > 0) {
            const l = root.dangerList
            root.dangerList = []
            root.doBatch(a, l)
        } else {
            if (one === "") { toast("目标设备已变，请重新确认"); return }
            backend.sendActionTo(one, a, {})
            toast("已下发「" + root.dangerLabel(a) + "」 → " + one)
        }
    }


    // 广播：原来这段逻辑写死在控制页那颗按钮的 onClicked 里，现在命令面板也要发广播，
    // 抽到这儿两边共用（顺序保持原样：权限 → 内容 → 有没有设备 → 逐台下发）。
    function broadcastAll(text) {
        if (!backend.mayDo("broadcast")) {
            toast("屏幕广播需向全部在线设备下发，需管理员身份");
            return
        }
        const content = (text || "").trim()
        if (content === "") { toast("先填广播内容"); return }
        const devs = backend.devices
        if (!devs || devs.length === 0) { toast("没有在线设备"); return }
        // 2026-10-21（审查 #6）：sendActionTo 显式目标 —— 原来逐台改 currentUid，
        // 广播完主画面被甩到最后一台；改成"发给谁"与"看哪台"分开。
        for (let i = 0; i < devs.length; ++i) {
            backend.sendActionTo(devs[i].uid, "notify", {
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
        bridge.showNote(s, "", "bell", 3000)
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
        // 2026-10-26（P0 灵动岛移除）：告警不再攒给岛浮层，改**实时 toast**，失败不静默。
        root.toast("未完成：" + action + (why ? " —— " + why : ""))
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
    // 2026-10-21（审查 #2）：property var 原地改不触发绑定，rows/计数统一整体重建赋值。
    function batchNote(uid) {
        if (!batch.active) return
        const rows = batch.rows.slice()
        let row = null
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].uid === uid) { row = rows[i]; break }
        if (!row) {
            row = { uid: uid, st: batch.done >= batch.total ? 1 : 0, why: "", late: false }
            rows.push(row)
        }
        row.late = true
        root.batch = { label: batch.label, total: batch.total, done: batch.done, ok: batch.ok,
                       fail: batch.fail, active: batch.active, rows: rows }
        refreshIsland()
    }

    // 超时收口：没回话的按"没成"结账，并且说出来 —— 不假装成功
    function batchTimeoutClose() {
        if (!batch.active) return
        const left = Math.max(0, batch.total - batch.done)
        // rows 里的行也重建（原地改 st 同样不触发绑定）
        const rows = []
        for (let i = 0; i < batch.rows.length; ++i) {
            const r = batch.rows[i]
            rows.push({ uid: r.uid, st: r.st === 0 ? 2 : r.st, why: r.why, late: r.late })
        }
        root.batch = { label: batch.label, total: batch.total, done: batch.total,
                       ok: batch.ok, fail: batch.fail + left, active: false, rows: rows }
        root.clearAlerts()   // 2026-10-21（审查 #8）：收口即清本批告警
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

    // 2026-10-21（审查 #2）：property var 原地改（batch.done++ / row.st=）不触发
    // batchChanged，任何声明式绑定都不会刷新 —— 全部改为**整体重新赋值**（含 rows
    // 里的行重建），过程里不碰旧 batch 对象任何字段。
    function batchStep(uid, ok, why) {
        if (!batch.active) return
        const rows = batch.rows.slice()
        let total = batch.total
        let row = null
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].uid === uid) { row = rows[i]; break }
        if (!row) {
            // 单台下发时这条回执可能先于批量名单落账 —— 补行并把它算进去，别让这台凭空消失
            row = { uid: uid, st: 0, why: "", late: false }
            rows.push(row)
            total = total + 1
        }
        if (row.st !== 0) {
            // 这台已经结过账（离线直判失败 / 已经报过成功），后面回来的都是同一台的二次回执。
            // 只把最新状态补进明细，绝不重复计数 —— 这是"按设备"而不是"按回执"的关键一句。
            row.late = true
            if (!ok) row.why = why || row.why
            root.batch = { label: batch.label, total: total, done: batch.done, ok: batch.ok,
                           fail: batch.fail, active: batch.active, rows: rows }
            refreshIsland()
            return
        }
        row.st = ok ? 1 : 2
        row.why = why || ""
        row.late = false
        const done = batch.done + 1
        const okN = ok ? batch.ok + 1 : batch.ok
        const failN = ok ? batch.fail : batch.fail + 1
        if (!ok) root.pushAlert(uid, batch.label, why || "")
        if (total > 0 && done >= total) {
            // 全成了也得说一句（4.5）："没消息"不该被当成"都成了"
            batchTimeout.stop()
            root.batch = { label: batch.label, total: total, done: done, ok: okN,
                           fail: failN, active: false, rows: rows }
            root.clearAlerts()   // 2026-10-21（审查 #8）：全量收口即清本批告警
            toast(batchSummary())
        } else {
            batchTimeout.restart()      // 还有台在等，把超时往后推
            root.batch = { label: batch.label, total: total, done: done, ok: okN,
                           fail: failN, active: batch.active, rows: rows }
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
            // 范围选择走当前显示的过滤后列表（界面显示的就是它，anchor/索引才对齐）。
            // 2026-10-21（审查 #4）：原来 deviceFilter==="all" 时退回 root.devices ——
            // 而 devices 混着其他班的设备（班级过滤只在 filteredDevices 里），
            // pickAnchor/idx 与屏幕显示列表索引错位，Shift 会框到看不见的机器。
            const list = root.filteredDevices
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
        // 只选**当前显示列表**里在线的：设备表现在含离线台账（见 backend 的说明），
        // 把离线的一起选上，批量下发会给它们记一串注定失败的账，看着像"发失败了"。
        // 2026-10-21（审查 #5）：改用 filteredDevices —— 原来遍历 root.devices 不带
        // 班级过滤，老师选了"高一(3)班"按 Ctrl+A 会把全校在线设备都选上。
        const devs = root.filteredDevices
        const s2 = []
        for (let i = 0; i < devs.length; ++i) {
            if (devs[i].online !== false) s2.push(devs[i].uid)
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
    /** 批量执行入口。params = 该批动作的指令参数（2026-10-25 课堂工具：音量档位）。
     *  不传 = 不发参数，行为与改造前逐字一致。 */
    function runOnPicked(a, params) {
        if (!permOk(a)) {
            backend.reportDenied("锁屏 / 关机")
            return
        }
        const list = []
        if (a === "") {
            // 「全部在线」= 当前显示列表里在线的（filteredDevices 已含班级过滤）。
            // 2026-10-21（审查 #5 同源）：原来遍历 root.devices，选班后 Ctrl+Shift+L
            // 会把全校在线设备锁一遍。
            const devs = root.filteredDevices
            for (let i = 0; i < devs.length; ++i) {
                if (devs[i].online !== false) list.push(devs[i].uid)
            }
        } else {
            for (let i = 0; i < root.picked.length; ++i) list.push(root.picked[i])
        }
        if (list.length === 0) {
            toast("还没有可操作的设备")
            return
        }
        // 危险动作先确认，并把**台数摆在对话框上**（2026-10-12 · UI 评审二.3）。
        // 台数就是这个对话框的全部价值——老师看到"将对 42 台设备执行关机"才会真的看一眼；
        // 只写"确认关机吗"没人会读，点了等于没确认。
        if (root.isDanger(a)) {
            root.askDanger(a, list.length, list, list.length === 1 ? list[0] : "")
            return
        }
        root.doBatch(a, list, params)
    }

    /** 批量下发循环体（从 runOnPicked 原样拆出，行为不变）。
     *  ⚠️ label 改走 dangerLabel()：原来的三元表达式只认 lock/shutdown，
     *     reboot 会原样显示成英文 `reboot已下发 3 台`（顺手一并修，就在本条路径上）。 */
    function doBatch(a, list, params) {
        const label = root.dangerLabel(a)
        const extra = (params && Object.keys(params).length > 0) ? params : null
        root.batchStart(label + (list.length > 1 ? " " + list.length + " 台" : ""), list)
        const dead = []
        let sent = 0
        for (let i = 0; i < list.length; ++i) {
            // 已经掉线的就别发了：发出去也没人回执，只会在灵动岛上挂成一个永不推进的 0/N
            if (!root.isDevOnline(list[i])) { dead.push(list[i]); continue }
            // 2026-10-21（审查 #6）：sendActionTo 显式目标 —— 原来逐台改 currentUid，
            // 批量越大主画面抖得越凶、最后一台被选中；"发给谁"与"看哪台"分开。
            backend.sendActionTo(list[i], a, extra || {})
            sent += 1
        }
        for (let j = 0; j < dead.length; ++j) root.batchStep(dead[j], false, "设备不在线")
        toast(label + "已下发 " + sent + " 台" + (dead.length > 0 ? "，" + dead.length + " 台不在线" : ""))
        root.picked = []
    }

    // ── 危险操作二次确认弹窗（2026-10-12 · UI 评审二.3）──
    // 桌面端此前**完全没有**确认环节：关机/重启/锁屏点下去就直接下发，
    // 而手机端那棵 `qml/mobile/Main.qml`（历史遗留死树）里反倒有一个 confirmDialog。
    // ⚠️ 桌面这颗必须自绘（项目铁律：不用 Qt 原生 Dialog/Button，它们走系统调色板会变白）。
    Popup {
        id: dangerDlg
        anchors.centerIn: Overlay.overlay
        width: 340
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: th.cream
            radius: th.rCard
            border.color: th.stroke
            border.width: 1
        }

        contentItem: Column {
            padding: 16
            spacing: 14

            Row {
                spacing: 8
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: th.err
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "确认" + root.dangerLabel(root.dangerAct)
                    color: th.fg
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
            }
            Text {
                width: parent.width - 32
                wrapMode: Text.Wrap
                color: th.fg3
                font.pixelSize: 12
                text: root.dangerN > 1
                      ? "将对 " + root.dangerN + " 台设备执行「" + root.dangerLabel(root.dangerAct) + "」。执行后无法撤销。"
                      : "将对 " + (root.dangerOne !== "" ? root.dangerOne : "当前选中的设备")
                        + " 执行「" + root.dangerLabel(root.dangerAct) + "」。执行后无法撤销。"
            }
            Row {
                spacing: 10
                anchors.horizontalCenter: parent.horizontalCenter
                Btn {
                    theme: th
                    width: 118
                    text: "取消"
                    onClicked: dangerDlg.close()
                }
                // 危险操作用红色确认键（评审二.3「危险操作用红色或二次确认」—— 两个都给）。
                Rectangle {
                    width: 118; height: 30; radius: th.rCtrl
                    color: th.err
                    Text {
                        anchors.centerIn: parent
                        text: "确认执行"
                        color: "#fff"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.execDanger()
                    }
                }
            }
        }
    }

    // ── 更新日志（需求 #7：把官网已有的「版本历史」能力搬进管理端）──
    // 数据面是 changelogModel（内嵌、与官网同源），离线也能看；官网那份是发布归档唯一真源，这里只读镜像。

    // ── 帮助（需求 #7：把官网帮助能力搬进管理端）──

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
        // 2026-10-21（审查 #1）：引导层必须自己吃住鼠标 —— 遮底 Rectangle 和 Item
        // 本身都不接收鼠标事件，点空白会穿透到下面的页面/标签（"引导还在，人已在
        // 操作后台"）。声明在内容**之前**（z 最低），内部按钮照常可点。
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

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
                                    var cs = ClassSort.byName(backend.classes)
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

                // 步骤 1：登录绑定（复用统一登录卡，与个人中心、设置页同一实现）
                AccountLoginCard {
                    visible: welcomeLayer.step === 1
                    width: parent.width
                }

                // 步骤 2：启动方式（2026-10-09 · 用户清单第②条：引导里就能设自启/快捷）
                Column {
                    visible: welcomeLayer.step === 2
                    width: parent.width; spacing: 12
                    Text { width: parent.width; wrapMode: Text.Wrap
                        text: "开机自动启动？桌面放个图标？（随时可在设置页改）"; color: th.fg3; font.pixelSize: 13 }
                    ToggleRow {
                        width: parent.width; theme: th
                        text: "开机自动启动"
                        note: "登录 Windows 后自动打开（免管理员）"
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

