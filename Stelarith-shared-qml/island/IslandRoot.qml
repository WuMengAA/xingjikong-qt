import QtQuick 2.15
import "."

// 被控端灵动岛 · QML 根组件
//
// 替代 src/island.cpp（935 行纯 QPainter 手绘）的视觉层。
// 系统层（无边框窗口/置顶/穿透/几何动画/生命周期）仍由 C++ IslandOverlay 持有。
//
// ── 接口契约（批次 3 由 IslandBridge 提供，签名不得改）──────────────
//   bridge.title / desc / icon / form / severity / light / idle / expanded
//   bridge.states     多状态列表 [{k,t,d,p}]
//   bridge.actions    [{label, role}]
//   bridge.fireAction(label)   QML → C++ 回调
//   bridge.requestExpand(bool) 手势 → C++
//   bridge.history     [{title,desc,icon,ts,timeStr,fromAdmin}]  历史通知列表
//                      2026-10-13：**后端已按显示序排好** —— 管理端下发(fromAdmin)在前、
//                      本地事件在后（组内保持原时间序）。多于 3 条时面板只显示前 3 条的高度
//                      （histVisibleMax=3，其余靠列表内滚动）。
//   bridge.showHistoryItem(index)             查看/恢复某条历史（点击历史项）
//   bridge.clearHistory()                     清空全部历史
//
    // ── 尺寸模型 ───────────────────────────────────────────
    //   root 用 implicitWidth / implicitHeight 显式给出目标尺寸（预算）。
    //   shell 用 anchors.fill: parent 跟随 root 实际大小。
    //   ⚠️ 2026-10-15 组件1 实证（纠正旧注释）：宿主 QQuickWidget 用
    //      SizeRootObjectToView ⇒ **根随视图**——view 尺寸由 C++
    //      IslandOverlay::geometryForContent/geometryForForm 按内容定
    //      （媒体专用 320/640，通知类内容定宽 ~200 起），root 实际 w/h 由
    //      view 驱动；implicitWidth/Height **只是布局预算，从不直接生效**。
    //      探针实证：--media 收起态 geom=200x56（内容定宽），加媒体宽度
    //      分支后才 320x56；IslandRoot 旧注释「view 按 root implicit 走」错。
    //   ⚠️ 不给 width/height 显式赋值：会被 view 覆盖，且与 Behavior 打架。
    //   ⚠️ 之前试过让 shell 用 anchors.fill + root.implicitHeight 靠内容推导，
    //      循环依赖把 implicitHeight 压到最小、岛被压扁——所以改成显式三段式。
Item {
    id: root

    // ── C 批（2026-10-27）：左右滑动切面 ────────────────────────────────
    // 为什么用 DragHandler 而不是 MouseArea：
    //   DragHandler 只处理"拖动"（超过 dragThreshold 才 active），不碰"单击"
    //   ⇒ 岛内的按钮照常点。MouseArea 方案会把整片区域的点击都吃掉。
    // 为什么放在根 Item 的最前面：PointerHandler 的优先级按"离事件源越近越高"，
    //   根部的 handler 优先级最低，子项里的控件优先接管。
    // ⚠️ 2026-10-27（本批修订）：原来只在**概览面**(faceNow===0)启用，怕媒体进度条 /
    //    设置音量滑块的横拖被抢。其实不用禁面 —— 用 grabPermissions 去掉
    //    "CanTakeOverFromItems" 即可：岛内控件（进度条 / 滑块）是 MouseArea(Item)
    //    先抢到 grab，DragHandler 抢不过 ⇒ 在控件上横拖归控件、不切面；在空白区横拖
    //    没有任何 Item 抢 ⇒ DragHandler 正常接管切到下一面。于是**所有展开面都能左右
    //    滑切换**，且天然不与横拖控件冲突，不需要到处埋"锁"。
    // ⚠️ 不要写 grabPermissions: PointerHandler.CanTakeOverFromNothing ——
    //    这个枚举名在 Qt 6.12 不存在，会被赋成 undefined 并在加载时报
    //    "Unable to assign [undefined] to QFlags<GrabPermission>"（实测踩到）。
    //    正确写法：保留 CanTakeOverFromHandlers*，去掉 CanTakeOverFromItems。
    DragHandler {
        id: faceSwipe
        target: null                      // 只观测手势，不移动任何东西
        enabled: root._expanded          // 所有展开面都允许滑动切面
        dragThreshold: 20                 // 默认 10 太灵敏，讲台上一抖就误判
        // 关键：不抢 Item（MouseArea）已经拿到的 grab —— 控件上的横拖归控件，空白区横拖才切面
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType
                         | PointerHandler.CanTakeOverFromHandlersOfSameType
        property real lastDx: 0
        property real lastDy: 0
        property bool _didActive: false   // 只有真正接管过手势才允许切面（防控件拖拽误触发）
        onTranslationChanged: { lastDx = translation.x; lastDy = translation.y }
        onActiveChanged: {
            if (active) { lastDx = 0; lastDy = 0; _didActive = true; return }
            if (!_didActive) return       // 没接管过（在控件上拖）⇒ 不切面
            _didActive = false
            if (!root._expanded) return
            if (Math.abs(lastDx) < 56) return                    // 手抖不算切面
            if (Math.abs(lastDx) < Math.abs(lastDy) * 1.4) return // 纵向为主，放行
            root.faceStep(lastDx < 0 ? 1 : -1)                    // 左滑 = 下一个面
        }
    }

    // ⚠️⚠️ 属性名**不能**叫 bridge！
    //    QML 的名字解析顺序里，**对象自身的属性**优先于上下文属性（context property）。
    //    写 `property var bridge: typeof bridge !== "undefined" ? bridge : null`
    //    时，右边那个 `bridge` 解析到的是**它自己**（恒 undefined）⇒ 永远 null。
    //    症状极具欺骗性：QML errors=0、加载成功、几何正确，但所有数据都是空/默认值。
    //    （2026-10-11 实测踩到。同族坑：挂子组件时写 `backend: backend` 也是这个。）
    //    正解：起个不冲突的名字。C++ 侧 contextProperty 仍叫 "bridge"。
    readonly property var bridgeRef: (typeof bridge !== "undefined" && bridge !== null) ? bridge : null

    // 标题字号自适应度量器（fitTitlePx 用）。不显示、不参与布局。
    TextMetrics {
        id: tm
        font.family: "Microsoft YaHei UI, PingFang SC, sans-serif"
        font.weight: Font.Bold
    }

    // ── 主题 ────────────────────────────────────────────────
    // ⚠️ 不用 pragma Singleton（需深浅并存），用普通组件 + 两实例
    IslandTheme { id: thLight; light: true }
    IslandTheme { id: thDark;  light: false }
    readonly property var th: root._light ? thLight : thDark

    // ── 从桥取状态 ──────────────────────────────────────────
    // ⚠️⚠️ 绝不用 bridge[k] 动态索引取属性！
    //    QQml 的绑定依赖是**静态**分析的：bridge["title"] 这种写法编译器看不见
    //    "root 依赖 bridge.title"，于是 bridge.changed() 发出来时这些 readonly property
    //    **不会重新求值** —— 它们在组件创建那一刻算一次就锁死。
    //    （2026-10-11 实测踩到：QML 加载成功、errors=0、几何也变，但 title/desc/states
    //      全空，屏幕上只有个空胶囊。原因就是这里。）
    //    正解：每个属性写一条**点访问**绑定；桥把全部 property 的 NOTIFY 都指向同一个
    //    changed() 信号，所以一条信号能让下面全部重读。
    readonly property bool   _idle:       bridgeRef ? bridgeRef.idle        : true
    readonly property bool   _expanded:   bridgeRef ? bridgeRef.expanded    : false
    readonly property bool   _light:      bridgeRef ? bridgeRef.light       : false
    readonly property bool   _interactive:bridgeRef ? bridgeRef.interactive : false
    readonly property string _severity:   bridgeRef ? bridgeRef.severity    : ""
    readonly property string _title:      bridgeRef ? bridgeRef.title       : ""

    // ── 形态（2026-10-17 · #330 形态多样化）──────────────────────
    // "capsule" | "centered" | "fullscreen" | "banner" | "mini"（对齐 C++ IslandOverlay::Form）
    // ⚠️ 必须点访问（bridge.form），不能 bridge["form"] —— 理由见上面 _idle 那段长注释：
    //    QQml 的绑定依赖是静态分析的，动态索引取到的值在组件创建那一刻就锁死。
    readonly property string _formKind: bridgeRef ? (bridgeRef.form || "capsule").toLowerCase()
                                                  : "capsule"
    // 紧凑外框（Banner 通栏 / Mini 角标）：与下方"胶囊那一套内容"**互斥**。
    // 两口子同时画会互相压：胶囊那套假定 56/320 高、左右留 14，塞进 72px 的通栏里
    // 会立刻溢出到窗外（表现为"文字被裁一半"）。
    readonly property bool _compactForm: root._formKind === "banner" || root._formKind === "mini"
    // 大卡片外框（Centered / Fullscreen · 2026-10-17 补）：
    //   这两档此前**没有任何 QML 版式**，落进胶囊分支 ⇒ 520×200 的窗口套
    //   `height/2=100` 的圆角 + 320 宽排版，表现为"一张被拉圆的黑卡、按钮被裁"。
    //   现在它们各有自己的面（IslandCardFace / IslandFullscreenFace），
    //   与胶囊那套内容互斥（同上：两套版式同时画会互相压）。
    readonly property bool _bigForm: root._formKind === "centered"
                                     || root._formKind === "fullscreen"
    // Mini 无描述时收成正圆（C++ geometryForForm 同口径：96 见方 + radius=half）。
    // ⚠️ 判据是 desc 是否为空，与 C++ 的 m_desc.isEmpty() 一一对应 —— 两边不同步
    //    就会出现"窗口是圆的、底色是方牌"这种一眼假。
    readonly property bool _miniOrb: root._formKind === "mini"
                                     && (bridgeRef ? (bridgeRef.desc === "") : true)
    readonly property string _icon:       bridgeRef ? bridgeRef.icon        : ""
    readonly property string _desc:       bridgeRef ? bridgeRef.desc        : ""
    // ── 通知时间（2026-10-17 · 用户「通知写上时间」）──────────────────
    // 以前只有历史列表带时间，正在看的这条不带 —— 老师看到一条通知时不该
    // 还得展开历史才知道它几点来。文本由 C++ 统一格式化（noticeTimeOf）。
    // ⚠️ `bridgeRef.noticeTime` 这种点访问才会被 QQml 静态识别成依赖；写成
    //    bridgeRef["noticeTime"] 会在组件创建那一刻锁死（上面 _idle 长注释同款坑）。
    //   viewer 侧桥没有这个属性 ⇒ undefined ⇒ "" ⇒ 整块时间不显示，退化成旧样子。
    readonly property string _noticeTime: (bridgeRef && bridgeRef.noticeTime) ? bridgeRef.noticeTime : ""
    // ── 通知渠道（2026-10-27 · F 收尾第 2 项）────────────────────────
    // 「课程提醒」这类渠道名由 C++ 一次性注入（island.h setChannelForNext），桥已就绪
    // （IslandBridge::channel），这里只负责**画出来**：不画的话，学生眼里"上课啦"
    // 和老师发的通知长得一模一样，分不清该不该当回事。
    // viewer 侧桥没有这个属性 ⇒ undefined ⇒ "" ⇒ 标签不出现，退化成旧样子。
    readonly property string _channel: (bridgeRef && bridgeRef.channel) ? bridgeRef.channel : ""
    // ── 组件面板（2026-10-27 · G4/G5/G6：日期 / 课表 / 倒计时）──────────
    // 非空 = 展开区画的是**看板**（面板），不是通知详情。
    // 与通知的分工见 island.h showPanel 的注释：通知是事件、面板是看板。
    // ⚠️ 点访问（bridgeRef.panelFace）：下标访问不建立绑定依赖，changed() 刷不新。
    // ⚠️ 只在**展开态**显示：收起态仍是面板自带的 title/desc 摘要 ——
    //    学生把面板推回去，看到的是一条正常的胶囊，不是一块空白。
    readonly property string _panelFace: (bridgeRef && bridgeRef.panelFace) ? bridgeRef.panelFace : ""
    readonly property var    _panelInfo: (bridgeRef && bridgeRef.panelData) ? bridgeRef.panelData : ({})
    readonly property bool   _panelOn: root._panelFace !== "" && root._expanded
    // 灵动岛 6 形态是否激活（C++ 单点裁决）：激活时由 IslandMorph 接管收起态内容
    readonly property bool   _morphOn: bridgeRef ? (bridgeRef.morphMode !== "" && bridgeRef.morphMode !== "none") : false
    // ⚠️ 2026-10-18 · 新通知到来时复位多面切换（face 自动逻辑缺陷修复）
    // 原行为：face / faceUserPick 是**永久 property**，收起、展开、换内容都不复位。
    //   ⇒ 用户上次展开看了媒体面，岛收起后来了条新通知，再展开**仍停在媒体面**，
    //     而用户这次想看的可能是通知本身 —— 上次的选择残留成了新内容的"挡板"。
    // 边界（与 1d95146 的"媒体重启不跳回"不冲突）：
    //   · 同一内容延续（媒体重启）⇒ 保留用户意图（faceUserPick 生效，不跳回）；
    //   · 新内容到来（新通知）⇒ 复位到概览，用户想先看新的。
    // 为什么用 _noticeTime 而不是 _title：媒体也会改 title（顶通知时），但只有
    //   通知才设置 noticeTime（_showNoticeTime 已区分媒体态）⇒ 这是"新通知"的
    //   可靠信号。_noticeTime 从空变非空 = 一条新通知到达。
    on_NoticeTimeChanged: {
        if (root._noticeTime !== "" && root.faceNow !== 0) {
            root.face = 0;
            root.faceUserPick = -1;
        }
    }
    // 媒体占据岛面时标题行被播放条挤用，且"媒体不是通知" ⇒ 不显示通知时间。
    readonly property bool   _showNoticeTime: root._noticeTime !== ""
                                               && !root._mediaPresenting
                                               && root._title !== ""
    readonly property var    _states:     (bridgeRef && bridgeRef.states)  ? bridgeRef.states  : []
    readonly property var    _actions:    (bridgeRef && bridgeRef.actions) ? bridgeRef.actions : []
    readonly property var    _history:    (bridgeRef && bridgeRef.history) ? bridgeRef.history : []
    // ── 历史记录与逻辑（2026-10-17 · 用户点单）────────────────────────
    // 未读条数：桥侧算好（C++ 遍历，别在 QML 里 filter —— history 每次 changed 都重算）
    readonly property int    _unread:     (bridgeRef && bridgeRef.historyUnreadCount) ? bridgeRef.historyUnreadCount : 0
    // 筛选行是否展开。**默认收起**：历史面板高度预算很紧（展开态要钉住 320），
    // 加一行 30px 会挤掉一条历史，所以做成"用户主动要才占高"。
    property bool histFilterOn: false
    readonly property var    _histCats: ["全部", "指令", "告警", "消息", "系统"]

    // ── 文件传输进度（#198，桥 fileTransfer* 属性）──────────
    // ⚠️ 同样是点访问绑定（不是 bridge["x"]），靠 bridge.changed() 一把全 NOTIFY 重读。
    readonly property bool   _ftActive:   bridgeRef ? bridgeRef.fileTransferActive : false
    // ⚠️ QML 没有 qint64 类型！用 real（double）承接 C++ 的 qint64 字节数属性。
    //    之前写 qint64 在 Qt 6.8 下被宽松放过，Qt 6.12 严格化后直接拒载整个 IslandRoot
    //    ⇒ 岛变透明空白（"灵动岛消失"）。2026-10-12 修。
    readonly property real  _ftBytes:    bridgeRef ? bridgeRef.fileTransferBytes  : 0
    readonly property real  _ftTotal:    bridgeRef ? bridgeRef.fileTransferTotal  : 0
    readonly property real   _ftPct:      (root._ftTotal > 0) ? Math.min(1, root._ftBytes / root._ftTotal) : 0

    // ── §6.3 语音频谱（桥 spectrumActive / spectrumBands）──────────
    // 2026-10-13 修 bug「被控端频谱显示到窗口去了」：频谱改由岛自己画。
    // ⚠️ 这条走**独立信号** spectrumChanged（约 50Hz），不是 changed() —— 别在这里
    //    依赖 bridge.changed 的"一把全 NOTIFY"。
    readonly property bool _spectrumActive: bridgeRef ? bridgeRef.spectrumActive : false
    readonly property var  _spectrumBands:  (bridgeRef && bridgeRef.spectrumBands) ? bridgeRef.spectrumBands : []

    // ── P3 播放时段管理：岛底「正在播放」（桥 media* 属性，跟 changed() 走）──
    // ⚠️ 同样点访问绑定（不是 bridge["x"]），靠 changed() 一把全 NOTIFY 重读。
    // ⚠️ qint64 用 real 承接（同 fileTransferBytes 口径，Qt 6.12 严格化）。
    readonly property bool   _mediaActive:    bridgeRef ? bridgeRef.mediaActive    : false
    readonly property string _mediaTitle:     bridgeRef ? bridgeRef.mediaTitle     : ""
    readonly property real   _mediaElapsedMs: bridgeRef ? bridgeRef.mediaElapsedMs : 0
    readonly property real   _mediaTotalMs:   bridgeRef ? bridgeRef.mediaTotalMs   : 0
    readonly property string _mediaNextTitle: bridgeRef ? bridgeRef.mediaNextTitle : ""
    readonly property string _mediaNextCover: bridgeRef ? bridgeRef.mediaNextCover : ""

    // ── 组件1 点歌互动（2026-10-15）─────────────────────────
    // 媒体播放呈现上岛（C++ setMediaSession 内 showIsland）：收起态画播放条、
    // 展开态画互动区 + 弹幕输入行。通知顶掉媒体时 presenting 仍 true（媒体还在播），
    // 但岛面画的是通知 ⇒ 用「标题==媒体标题」判"岛面是媒体"，回落默认通知布局。
    readonly property bool _mediaPresenting: bridgeRef ? bridgeRef.mediaPresenting : false
    readonly property bool mediaIsContent: root._mediaPresenting && root._title === root._mediaTitle

    // ── VoiceHub 联动（#329，2026-10-17）────────────────────────
    // 数据来自**内置 voicehub 模块**（main.cpp 在岛构造后
    // setQmlObjectProperty("voicehubObj", 模块) 注入；viewer 侧没有这个模块
    // ⇒ 属性保持 null）。与 P3 媒体栏的分工：那条画**本机 ffplay 的排期广播**，
    // 这一行画 **VoiceHub 的远程权威广播**（音乐管理员在网页播的那条），
    // 两个来源不同、可同时为真，各画各的、互不覆盖。
    // ⚠️ 不能写成 readonly 直绑 voicehub —— 那样属性在模块注入前就定死成 null；
    //    必须经中间 property var（setProperty 可写、绑定可重算）。
    property var voicehubObj: null
    // 自动化规则引擎（Phase 3 · 2026-10-27）：main.cpp 经 setQmlObjectProperty 注入
    // （同 voicehub 模式）。引擎是纯 QObject，QML 只调其 Q_INVOKABLE / 读 Q_PROPERTY。
    property var automationObj: null
    readonly property var automationRef: root.automationObj
    readonly property bool automationFaceOn: !!root.automationObj
    readonly property var vhRef: root.voicehubObj
    // ⚠️ 2026-10-25（Win7 现场排障）：以下所有 vhRef 守卫一律用**真值**判断
    // （`root.vhRef` / `!root.vhRef`），禁写 `!== null` / `=== null`：
    //   C++ 侧清空（island.cpp setQmlObjectProperty 销毁回调）一旦写出**无效
    //   QVariant**，var 属性读成的是 **undefined 而非 null**（var 语义探针实证，
    //   Qt 6.12）——`undefined !== null` 按严格比较为**真**，守卫被穿透，绑定
    //   重算读 .active/.nextTitle 连报 TypeError（Win7 rc.4 退出日志三连报
    //   同款），且这正是 10-19 远控崩溃链（绑定重算→Repeater→野指针）的入口。
    //   真值判断对 null / undefined / 活对象三态全兼容。行 45 bridgeRef 的
    //   typeof 双保险是同一动机的更早样板。
    readonly property bool vhOn: root._expanded && root.vhRef && root.vhRef.active
        // 搜索/投稿页打开时让位（§七 一次只展开一件事；那两页锚 mediaRow.bottom
        // 且高度预算 searchH/submitH 已按 vhH=0 收敛——vhOn 条件里必须排掉，
        // 否则行与页面在同一块空间里重叠）。
        && !root.searchOn && !root.submitOn
    // 展开态高度预算 vhH 与下方 mediaH 同区块定义（行高 30/44 + 上边距 8）。
    // 第二行（接下来 / 人数）有条件出现 ⇒ 预算跟着条件走，不写死。
    readonly property bool vhLine2: root.vhRef
        && (root.vhRef.nextTitle !== "" || root.vhRef.listeners > 0)
    // 进度文本：在播 "m:ss / m:ss"；暂停冻在 position；无时长只给已播。
    function vhTimeText() {
        if (!root.vhRef) return "";
        const p = Math.floor(root.vhRef.positionSec), d = Math.floor(root.vhRef.durationSec);
        const mm = s => Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60);
        return d > 0 ? mm(p) + " / " + mm(d) : mm(p);
    }
    // 第二行文本：「接下来：xxx · N 人在听」，缺段自动省。
    function vhSubText() {
        if (!root.vhRef) return "";
        const parts = [];
        if (root.vhRef.nextTitle) parts.push("接下来：" + root.vhRef.nextTitle);
        if (root.vhRef.listeners > 0) parts.push(root.vhRef.listeners + " 人在听");
        return parts.join(" · ");
    }

    // ── 多界面（#330，2026-10-17 · 用户点单「灵动岛多界面」1+3）──────────
    // 展开态从"一屏堆叠"升级为**多面切换**（概览/媒体/历史/状态）：
    //   · 面只作用展开态；收起/空闲、搜索/投稿页、有动作按钮时**不开面**
    //     （§七 一次只展开一件事——动作按钮要能点，不能被面板盖住）。
    //   · 概览面 = 现有布局，零改动（面面板只在 face≠0 时满盖内容区）。
    //   · 面的可用性：媒体面要有本机媒体或 VoiceHub 广播；历史面要有历史；
    //     状态面恒在；概览面恒在 ⇒ 至少 2 面才显示切换器。
    //   · faceNow 是**有效面**：选中的面失效（媒体停了/历史清了）自动回概览，
    //     不倒扣 face 本身（用户切换意图保留）。
    property int face: 0
    // ⚠️ 2026-10-18 · 区分「系统回落」与「用户主动选择」（face 混淆缺陷修复）
    // 原实现：faceNow 只在选中面失效时回落 0，但 **face 永不回写** ⇒ 用户主动切到
    // 别的面后，一旦"失效的面"（媒体/历史）再次可用，又会被**记忆拽回**那个面。
    // 例：face=1(媒体) → 媒体停 → 回概览 → 用户切到态(face=3) → 媒体重启 →
    //     原实现 faceNow 又变回 1（因为 face 还是 1），用户意图被违背。
    // 修法：faceUserPick 记录"用户最后一次主动选择"。faceNow 的回落规则改为：
    //   · 用户从未主动选过（faceUserPick==-1）⇒ 选中面失效时回落，但**保留 face**，
    //     等它恢复（系统替我记住"最近看过媒体"）；
    //   · 用户主动选过 ⇒ 失效就**彻底回概览**（尊重用户最后一次选择，不被拽回）。
    property int faceUserPick: -1
    readonly property bool mediaFaceOn: root._mediaActive || root.mediaIsContent
        || (root.vhRef && root.vhRef.active)
    readonly property bool histFaceOn: root._history.length > 0
    readonly property var faceModel: {
        var fs = [{ id: 0, label: "概" }];
        if (root.mediaFaceOn) fs.push({ id: 1, label: "媒" });
        if (root.histFaceOn)  fs.push({ id: 2, label: "史" });
        fs.push({ id: 3, label: "态" });
        // 2026-10-27（H 批）：批注面常驻。用户裁决「这些功能大多是加入到被控端，
        // 而且大部分和灵动岛有关」—— 老师上课点不到管理端，岛才是他的手。
        fs.push({ id: 4, label: "笔" });
        // 2026-10-27（C 批）：设置面常驻（音量双滑块 + 通知黑名单）。
        // 用户裁决「这些功能大多在被控端、且和岛有关」—— 音量/黑名单老师上课当场要调，岛才是他的手。
        fs.push({ id: 5, label: "设" });
        // 2026-10-27（Phase 3）：自动化规则面常驻（被控端 AutomationEngine）。
        // 用户裁决「自动化是被控端能力、主入口在岛」—— 老师在讲台上当场配规则，岛才是他的手。
        if (root.automationFaceOn) fs.push({ id: 6, label: "自" });
        return fs;
    }
    // C 批（2026-10-27）：按 faceModel 的顺序翻一面（左右滑动 & 将来的键盘/遥控都走它）。
    // ⚠️ 必须按 model 顺序而不是 id±1：媒体(1)/历史(2) 是按需出现的，
    //    id 连号不代表它们在 model 里连号（历史关掉时就是 1 → 3）。
    // 到头即停、不循环：循环会"划过头又回到原点"，讲台上没法解释。
    function faceStep(delta) {
        const fs = root.faceModel
        let i = 0
        for (let k = 0; k < fs.length; ++k) {
            if (fs[k].id === root.faceNow) { i = k; break }
        }
        const j = i + delta
        if (j < 0 || j >= fs.length) return false
        root.face = fs[j].id
        root.faceUserPick = fs[j].id      // 记成"用户主动选"，否则媒体/历史复活会把面拽回去
        return true
    }
    readonly property bool facesOn: root._expanded && !root._showActionsNow
        && !root.searchPageOn && !root.submitPageOn && root.faceModel.length >= 2
    readonly property int faceNow: {
        // 用户主动选过：失效面彻底回落概览，且**不再自动恢复**（用户意图优先）。
        if (root.face === 1 && !root.mediaFaceOn && root.faceUserPick >= 0) return 0;
        if (root.face === 2 && !root.histFaceOn && root.faceUserPick >= 0) return 0;
        // 用户从未主动选：系统替我记住最近的面，失效时回落但恢复后自动回来。
        if (root.face === 1 && !root.mediaFaceOn) return 0;
        if (root.face === 2 && !root.histFaceOn) return 0;
        return root.face;
    }
    readonly property bool facePanelOn: root.facesOn && root.faceNow !== 0
    // 媒体面的数据源：本机 ffplay 优先，其次 VoiceHub 远程广播。
    readonly property bool faceUseLocal: root._mediaActive || root.mediaIsContent
    readonly property string faceTitle: root.faceUseLocal ? root._mediaTitle
                                       : (root.vhRef ? root.vhRef.title : "")
    readonly property string faceArtist: root.faceUseLocal ? ""
                                       : (root.vhRef ? root.vhRef.artist : "")
    readonly property real facePos: root.faceUseLocal
        ? (root._mediaElapsedMs / 1000.0) : (root.vhRef ? root.vhRef.positionSec : 0)
    readonly property real faceDur: root.faceUseLocal
        ? (root._mediaTotalMs / 1000.0) : (root.vhRef ? root.vhRef.durationSec : 0)
    readonly property string faceNext: root.faceUseLocal ? root._mediaNextTitle
                                       : (root.vhRef ? root.vhRef.nextTitle : "")
    function faceTimeText() {
        const mm = s => { s = Math.max(0, Math.floor(s)); return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60); };
        return root.faceDur > 0 ? mm(root.facePos) + " / " + mm(root.faceDur) : mm(root.facePos);
    }
    // 状态面的行（岛桥只暴露展示态字段 —— 这里只用桥与模块**已给**的数据，
    // 不新造数据源；版本/连接态在控制面板里看）。
    readonly property var faceStatusRows: {
        const formKey = bridgeRef ? (bridgeRef.form || "Capsule") : "Capsule";
        const formName = formKey === "Centered" ? "居中卡片"
                        : (formKey === "Fullscreen" ? "全屏浮层" : "顶部胶囊");
        const rows = [
            { k: "当前形态", v: formName },
            { k: "媒体", v: root.facePhaseText() },
            { k: "VoiceHub", v: !root.vhRef ? "模块未注入"
                                     : (root.vhRef.active ? "在播：" + root.vhRef.title
                                                          : root.vhRef.statusText) },
            { k: "历史通知", v: root._history.length + " 条" }
        ];
        return rows;
    }
    function facePhaseText() {
        switch (root._mediaPhase) {
        case "starting": return "即将播放";
        case "playing":  return "播放中";
        case "ending":   return "快结束";
        case "ended":    return "已播完";
        case "blocked":  return "被播放时段拦";
        default:         return "无";
        }
    }

    // ── 组件7 播放四态（2026-10-15，用户点单）─────────────────────
    // 相位**由 C++ 单点判定**（口径见 island.h：时长/剩余只在 C++ 算），QML 只按串
    // 切文案/配色 —— 避免"两端各算一遍阈值"这种必然漂移。取值：
    //   "" 无媒体 / "starting" 广播开始 / "playing" 播放中 /
    //   "ending" 快结束 / "ended" 播完 / "blocked" 禁播（被播放时段拦）。
    // demoMediaPhase 是**探针注入**用（与 likeCount 同范式，非空即覆盖桥值）；
    // 生产路径它恒为空串。⚠️ 不能写成 readonly 直绑桥 —— 探针 setProperty 要写得进。
    property string demoMediaPhase: ""
    readonly property string _mediaPhase: root.demoMediaPhase !== ""
        ? root.demoMediaPhase
        : (bridgeRef ? bridgeRef.mediaPhase : "")
    // "没在播"的两态（播完 / 禁播）：岛面仍属媒体家族，但没有进度条/时间。
    readonly property bool _mediaNoProgress: root._mediaPhase === "ended"
        || root._mediaPhase === "blocked"
    readonly property bool _mediaBlocked: root._mediaPhase === "blocked"
    readonly property bool _mediaEnding: root._mediaPhase === "ending"
    readonly property bool _mediaStarting: root._mediaPhase === "starting"

    // 组件5（2026-10-15）：祝福弹幕发送冷却剩余秒数（0=可发）。
    // DanmakuWidget 30s 本机冷却，1Hz 经 bridge 的 blessingCooldownSec
    // 骑 changed() 全量 NOTIFY 推来（与 _mediaElapsedMs 同频率先例）。
    readonly property int _blessingCooldownSec: bridgeRef ? bridgeRef.blessingCooldownSec : 0
    // 互动区可见性：展开态 + 岛面是媒体 + 无动作按钮行（§七：一次只展开一件事）
    // 组件7：播完/禁播不给互动区 —— 这时没得听，"好听/点歌"点了也没意义。
    readonly property bool mediaInteractOn: root._expanded && root.mediaIsContent
        && !root._showActionsNow && !root._mediaNoProgress
    // 点赞计数：组件4 接桥前先用本地属性（探针可 set 演示值）；生产值由桥下发后改绑定。
    property int likeCount: 0

    // ── 组件2 点歌搜索页（2026-10-15）─────────────────────────
    // 「点歌」按钮点击 ⇒ 岛内**原地变形**为搜索页（原则§八：原地变形非页面栈；
    // 同骨架：图标→标题→内容→操作，不新开窗、不走页面栈）。
    // 全部为 QML 本地状态（与 likeCount 同范式）：云端搜索/提交在组件6 接，
    // 现在用占位目录 searchCatalog 本地过滤；探针 setProperty 即可演示五态。
    property bool searchOn: false        // 搜索页打开（点歌按钮→true；返回→false）
    property string searchQuery: ""      // 搜索关键词（输入行双向：textEdited 推进来）
    property int searchPhase: 0          // 0 空态 / 1 骨架 / 2 结果 / 3 无结果 / 4 超额禁用
    property int searchQuotaUsed: 0      // 已点歌数（超额禁用判据；组件6 接云端后改桥绑定）
    property int searchQuotaMax: 3       // 待播上限（详规"超额禁用"；配额口径见归档）
    // 占位曲库（组件6 换云端 /api/songs/search 结果；字段与结果行同构）
    property var searchCatalog: [
        { name: "晴天",     artist: "周杰伦", dur: "04:29" },
        { name: "稻香",     artist: "周杰伦", dur: "03:43" },
        { name: "夜曲",     artist: "周杰伦", dur: "03:46" },
        { name: "平凡之路", artist: "朴树",   dur: "05:02" },
        { name: "光年之外", artist: "邓紫棋", dur: "03:35" }
    ]
    // 本地过滤（组件6 后由云端搜索结果替换本绑定）
    readonly property var searchResults: {
        var q = root.searchQuery.trim()
        if (q.length === 0) return []
        return root.searchCatalog.filter(function (s) {
            return s.name.indexOf(q) >= 0 || s.artist.indexOf(q) >= 0
        })
    }
    // 搜索页总可见性：展开态互动区在 + 搜索页打开（§七：一次只展开一件事，
    // 打开搜索页时互动区/弹幕行让位，interactH/danmakuH 同步归零）。
    // 组件3：与投稿页互斥（searchPageOn 判 !submitOn；入口侧也互相关闭）。
    readonly property bool searchPageOn: root.mediaInteractOn && root.searchOn && !root.submitOn
    readonly property bool searchQuotaFull: root.searchQuotaUsed >= root.searchQuotaMax
    // 岛面不再是媒体（被通知顶掉/媒体结束）时收起搜索页，回来不残留半开状态。
    // 组件3：投稿页同步收起（同一信号两个 handler 会撞 qmllint，合并至此）。
    onMediaIsContentChanged: {
        if (!root.mediaIsContent) {
            root.searchOn = false
            root.submitOn = false
        }
    }

    // ── 组件3 投稿页（2026-10-15）─────────────────────────
    // 入口：danmakuRow 输入 pill / 发送 pill 点击 ⇒ 岛内**原地变形**为投稿页
    // （原则§八：同骨架 图标→标题→内容→操作；不新开窗、不走页面栈）。
    // 四态 submitPhase：0 编辑（50 字计数/超限禁用）/ 1 提交中 / 2 成功(✓) /
    // 3 失败(✗)。2026-10-15 组件5：投稿接**真实发送** ——
    // bridgeRef.sendBlessing(text) → IslandOverlay → DanmakuWidget
    // （学生不可信源规则：30 字截断/敏感词过滤/30s 冷却；结果码 0/1/2/3）。
    // 码 0 ⇒ 态1 + Timer 400ms（云端 ACK 延迟模拟，组件6 接端点后删 Timer）；
    // 码非 0 ⇒ 直接态3，submitFailCode 驱动失败文案。
    // 草稿双向同步：开页携入 danmakuInput.text；返回写回弹幕行；成功「完成」
    // 清空两处（草稿已消费，不回写）。
    property bool submitOn: false        // 投稿页打开（输入/发送 pill→true；返回→false）
    property string submitText: ""       // 投稿文字（与 danmakuInput.text 开页时同步）
    property int submitPhase: 0          // 0 编辑 / 1 提交中 / 2 成功 / 3 失败
    property int submitFailCode: 0       // 态3 失败原因（1 敏感词/2 冷却中/3 内容无效）
    readonly property int submitMax: 50          // 50 字上限（CJK 一字一计）
    readonly property int submitLen: root.submitText.length
    readonly property bool submitEmpty: root.submitLen === 0
    // 超限 ⇒ 计数红 + 「已超出 N 字」+ 投稿钮禁用；**不硬截断**
    // （maximumLength 会使超限态不可达，禁用态无从呈现）。
    readonly property bool submitOver: root.submitLen > root.submitMax
    // 投稿页总可见性：展开态互动区在 + 投稿页打开 + 搜索页未开（同 searchPage
    // 范式，§七 一次只展开一件事）。
    readonly property bool submitPageOn: root.mediaInteractOn && root.submitOn && !root.searchOn

    // 组件5：真实投稿（投稿钮 + 重试钮共用一条路）。失败路径由 sendBlessing
    // 的**真实结果码**驱动（不再有 simfail 开关）；成功路径的 400ms 是云端
    // ACK 延迟模拟（组件6 接端点后删 Timer，改由 ACK 回调切态）。
    function doSubmit() {
        var code = (root.bridgeRef && root.bridgeRef.sendBlessing)
                 ? root.bridgeRef.sendBlessing(root.submitText) : 3
        if (code === 0) {
            root.submitPhase = 1          // 提交中
            submitDebounce.restart()      // 400ms 后出成功态
        } else {
            root.submitFailCode = code    // 1 敏感词 / 2 冷却中 / 3 内容无效
            root.submitPhase = 3
        }
    }

    // ── 组件4 点赞动效与快速反馈（2026-10-15）──────────────────
    // 详规裁决（设计文档 §二 表第 4 行）：**单击式快捷**——双击飘心需 250ms
    // 判别延迟，教室场景是明显卡顿 ⇒ 默认不做双击判定，双击飘心留作增强备选。
    // 动效两件套（详规原文）：飘心 600ms 上飘淡出 + 计数回弹 150ms。
    // 快速反馈一次制：点过「好听」即禁用（Item.enabled 内置传导置灰，
    // 组件2 已实证机制），防刷；云端点赞/反馈端点在组件6。
    property bool likeOn: false        // 快速反馈一次制：点过「好听」后为 true
    property real likePop: 1.0         // 计数回弹缩放（1.0 ⇄ 1.35，150ms 两段）

    // ── 尺寸（显式，不靠隐式推导）───────────────────────────
    readonly property int padH: 16
    // 展开态"标题 + 描述"占的头部高度（自适应字号后标题可达 64px，2 行 128px）。
    // 给列表/历史面板定位当 top 锚点，避免压到标题。
    readonly property int headH: root._expanded
        // ⚠️ 传输中 exDesc 被隐藏（改由进度条呈现），不能再把它的高度算进来，
        //    否则头部多算一块空白、把下面的历史区顶出去。
        // 组件2：搜索页打开时大标题让位（歌名在 mediaRow 已有，§七 一次只展开
        //    一件事）⇒ exTitle.height 不计入。组件3：投稿页同款让位。
        ? Math.max(46, 10 + ((root.searchPageOn || root.submitPageOn) ? 0 : exTitle.height) + 6 + (root._ftActive ? 0 : exDesc.height))
        : 0
    readonly property int listH: (root._expanded && stList.rows > 0
                                  && !root.searchPageOn && !root.submitPageOn)
        ? stList.implicitHeight + 8 : 0
    // 历史面板高度：展开态 + 有历史 + 无活跃按钮时才给空间。
    // 2026-10-12 修「展开后太矮、历史显示不全」：每条 44→48px、封顶 200→300px，
    // 头部按 histHeader 实际高度 32px 算（原来是拍脑袋的 36，与面板实高不一致）。
    // 2026-10-13 用户「看不见历史通知 → 默认折叠 3 条以上，保留 3 条显示」：
    // 面板高度按**最多 3 条**算（不再随条数一路涨到 300）—— 多于 3 条靠列表内滚动看，
    // 面板本身稳在 3 条的高度，展开态总高才钉得住 320。
    readonly property int histVisibleMax: 3
    // 2026-10-13：语音对讲中（频谱态）不显示历史面板 —— 频谱区与历史区都在展开态，
    // 同屏会挤，且对讲本身就是临时的。收起频谱后历史照常出来。
    readonly property int histH: (root._expanded && root._history.length > 0 && !root._showActionsNow
                                  && !root._spectrumActive && !root.mediaIsContent
                                  && !root.facePanelOn   // #330：面面板打开时不占位
                                  && !root._panelOn)     // 2026-10-27：组件面板打开时不占位
        // 2026-10-17：筛选行展开时多占 30px（用户主动开的，接受高度变化；
        // 收起时预算与旧版完全一致，不回归「历史显示不全」）。
        ? Math.min(root.histVisibleMax, root._history.length) * 48 + 32
          + (root.histFilterOn ? 30 : 0)
        : 0
    readonly property int actH:  (root._actions.length > 0) ? 34 : 0
    // 文件传输进度条占的额外高度（展开态 + 传输中才给）
    readonly property int ftH: (root._expanded && root._ftActive) ? 46 : 0
    // §6.3 语音频谱区占的额外高度（展开态 + 对讲中才给；= 34 高 + 12 上边距）
    readonly property int specH: (root._expanded && root._spectrumActive) ? 46 : 0
    // P3 播放时段管理：岛底「正在播放」栏占的额外高度（展开态 + 媒体在播才给；
    // = 行高 34 + 上边距 12，与 specH 同口径 —— 2026-10-14 修：原值 40 少算 6px，
    // 内容比预算高，叠历史面板时底部会被压 6px）
    // 组件7：播完（ended）/禁播（blocked）时 m_mediaOn 已是 false（_mediaActive=false），
    // 但这一行还要画（讲"播完了"/"为什么没播"）⇒ 预算跟着 mediaIsContent 兜住。
    readonly property int mediaH: (root._expanded
                                   && (root._mediaActive || root.mediaIsContent)) ? 46 : 0
    // VoiceHub 联动（#329）：远程广播行占的额外高度（展开态 + 有远程广播才给；
    // 行高 30（单行）/ 44（带第二行）+ 上边距 8；vhOn/vhLine2 定义见上方 vhRef 区块）。
    readonly property int vhH: root.vhOn ? (root.vhLine2 ? 52 : 38) : 0
    // 组件1（2026-10-15）：互动区 / 弹幕输入行占的额外高度（展开态 + 岛面是媒体才给）。
    // 与 historyPanel 的 topMargin 同一套公式（预算与锚点必须同步，见 2026-10-14 注释）。
    // 组件2：搜索页打开时两行让位（§七 一次只展开一件事）⇒ 条件加 !searchOn。
    // 组件3：投稿页打开时同样让位 ⇒ 条件加 !submitOn。
    readonly property int interactH: (root.mediaInteractOn && !root.searchOn && !root.submitOn) ? 46 : 0
    readonly property int danmakuH:   (root.mediaInteractOn && !root.searchOn && !root.submitOn) ? 36 : 0
    // 组件2（2026-10-15）：点歌搜索页占的额外高度。**自适应预算**：展开态硬顶
    // 320（C++ geometryForForm 媒体分支 640×320），搜索页只能吃剩余空间 ——
    // 320 - padH*2 - headH(标题已让位) - listH(状态列表已让位) - mediaH - actH
    // - 12(页面自身 topMargin)。搜索页开时大标题/状态列表都清零高度让位
    // （§七 一次只展开一件事；歌名在 mediaRow 仍有）。
    // 探针实测收敛值 188：写死 160 曾溢出 59px 裁底部（2026-10-15 实测 379）。
    readonly property int searchH: root.searchPageOn
        ? Math.max(100, 320 - (root.padH * 2 + root.headH + root.listH + root.mediaH + root.vhH + root.actH + 12))
        : 0
    // 组件3（2026-10-15）：投稿页占的额外高度。自适应公式与 searchH 同款
    // （展开态硬顶 320，投稿页吃剩余空间：320 - padH*2 - headH(标题已让位)
    // - listH(状态列表已让位) - mediaH - vhH - actH - 12 页面自身 topMargin）。
    // 编辑态内容（compose 66 + 计数 ~16 + 按钮 28 + 间距 16）≈126 < 预算，
    // 反馈态为居中 Column 自适应，均低于 searchH 同款预算。
    readonly property int submitH: root.submitPageOn
        ? Math.max(100, 320 - (root.padH * 2 + root.headH + root.listH + root.mediaH + root.vhH + root.actH + 12))
        : 0
    readonly property int collapsedH: root.th.shapeCollapsed

    // 有动作按钮时：胶囊也走"加高"逻辑（Centered/Fullscreen 本来就高）
    readonly property bool _showActionsNow: root._actions.length > 0 && !root._idle

    // ── 展开态标题自适应字号（15 → 64px，按字数收敛）──────────
    // "自适应大小"：短标题用超大字（大数字/关键提示），长标题降级到可读。
    // 宽按当前形态（idle 不看这里；expanded=400，collapsed=320）给 2 行预算。
    // 纯计算 + TextMetrics 度量，不依赖任何隐式布局。
    function fitTitlePx(t) {
        var n = t ? t.length : 0
        var fs = (n >= 33) ? root.th.fsTitleXs
              : (n >= 28) ? root.th.fsTitleSm
              : (n >= 17) ? root.th.fsTitleMd
              : (n >= 9)  ? root.th.fsTitleLg
              : (n >= 5)  ? root.th.fsTitleXl
              :             root.th.fsTitleXxl
        if (n === 0) return fs
        var w = (root._expanded ? 640 : 320) - root.padH * 2 - 48
        // 从 fs 往下探，找到"单行或两行能进宽"的最大字号
        for (; fs >= root.th.fsTitleXs; fs -= 2) {
            tm.font.pixelSize = fs
            tm.text = t
            if (tm.width <= w) return fs   // 单行能进
            // 两行预算 ≈ 2 * 平均行宽；粗略用 n*0.6*fs 估算字符宽
            var twoLine = (n * 0.55 * fs) / 2
            if (twoLine <= w) return fs
        }
        return root.th.fsTitleXs
    }

    // ── 组件5：禁用态控件穿透守卫（2026-10-15）──────────────────
    // 递归找命中点下**最深的可见 item**（childAt 只查直接子级；按钮在
    // expandedCol → submitPage → Row 三层以下，必须递归）。
    function deepestItemAt(item, x, y) {
        if (!item) return null
        var c = item.childAt(x, y)
        if (!c) return item
        var p = c.mapFromItem(item, x, y)
        var deep = root.deepestItemAt(c, p.x, p.y)
        return deep ? deep : item
    }

    // 命中点是否落在一个**被禁用**的控件上（沿祖先链找 enabled===false）。
    // 场景：禁用钮（Item.enabled=false）不吃事件，点击穿透到 gestureArea
    // （铺满全岛、z:-1）把岛点收起 —— 组件5 的 30s 冷却让投稿钮常规进入
    // 禁用态后，这个穿透从"偶发"变"常态"（点一下冷却中的投稿钮=岛被收起）。
    // ⚠️ 为什么不在 ActionButton 里加吸收 MouseArea：QML 的 enabled 沿
    //    祖先链生效（组件2「超额禁用」置灰即靠这条传导），禁用根的子孙
    //    MouseArea 同样收不到事件，吸收垫建在组件内是死的 —— 只能在事件
    //    汇聚点（gestureArea）拦。
    function hitDisabledControl(mx, my) {
        var it = root.deepestItemAt(root, mx, my)
        while (it) {
            if (it !== root && it.enabled === false) return true
            it = it.parent
        }
        return false
    }

    // #198：体积文案（与 C++ humanBytes 同 1024 进制，只到 GB）
    function fxHumanBytes(n) {
        if (n < 1024) return String(n) + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
        if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + " MB"
        return (n / 1024 / 1024 / 1024).toFixed(2) + " GB"
    }
    // P3：毫秒 → "mm:ss"（播放底栏时长）
    function fmtMediaTime(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" : "") + m + ":" + (r < 10 ? "0" : "") + r
    }
    // 组件7：播放条左下文案（四态口径集中在这里，收起态/展开态共用一份，
    // 免得两处各写一遍各错一处）。空串 = 不显示。
    function mediaLeftText() {
        if (root._mediaStarting) return "即将播放"
        if (root._mediaEnding)
            return "剩余 " + root.fmtMediaTime(Math.max(0, root._mediaTotalMs - root._mediaElapsedMs))
        if (root._mediaPhase === "ended") return "播放结束"
        if (root._mediaBlocked) return ""      // 标题本身已是"当前时段不开放播放"
        return root.fmtMediaTime(root._mediaElapsedMs)
             + (root._mediaTotalMs > 0 ? " / " + root.fmtMediaTime(root._mediaTotalMs) : "")
    }
    // 组件7：快结束/播完的告警色（琥珀）。深浅两套，避免浅色下看不清。
    function mediaWarnColor() { return root.th.light ? "#B26A00" : "#E0A030" }
    // ⚠️ 2026-10-14 修 Binding loop：exTitlePx 原来是**绑定** root.fitTitlePx(bridgeRef.title)，
    //   而 fitTitlePx 内部既**读** tm.width（TextMetrics）又**写** tm.font.pixelSize/tm.text
    //   —— tm.width 随写入变化 ⇒ 触发 exTitlePx 重算 ⇒ 又写 tm ⇒ QML 依赖图成环
    //   （冒烟日志实测：`Binding loop detected for property "exTitlePx"`，标题字号可能停更）。
    //   修法：exTitlePx 退为**命令式**属性（无绑定表达式⇒不进依赖图），由 _title/_expanded
    //   变化时显式重算；fitTitlePx 对 tm 的写就再也流不回属性系统。
    property int exTitlePx: root.th ? root.th.fsTitleXxl : 64
    function refreshTitlePx() { root.exTitlePx = root.fitTitlePx(root._title) }
    on_TitleChanged:   root.refreshTitlePx()
    on_ExpandedChanged: root.refreshTitlePx()
    Component.onCompleted: root.refreshTitlePx()   // 创建时桥可能已带 title，兜底算一次

    // 2026-10-13 用户「界面太小了放大到 640x320」：展开态 400×自适应 → **640**（宽）× 高度下限 **320**。
    // ⚠️ 宽度模型（2026-10-15 组件1 实证，纠正本条旧注释的错误认知）：
    //    宿主 QQuickWidget 用 SizeRootObjectToView ⇒ **根随视图**——岛宽由 C++
    //    IslandOverlay::geometryForContent/geometryForForm 按内容定（媒体专用 320/640，
    //    通知类内容定宽 ~200 起），QML 的 implicitWidth **只是布局预算、从不直接生效**
    //    （IslandRoot.qml 头部原注释「view 按 root implicit 走」与实际行为相反）。
    //    探针实证：--media 收起态 geom=200x56（内容定宽），加媒体宽度分支后才 320x56。
    //    高度取 max(320, 内容预算)：内容比 320 高时不裁掉，正常就稳在 320（正好放 3 条历史）。
    implicitWidth: root._idle ? root.th.shapeIdle * 3    // 4:3 比例 (44*3=132)
                               : (root._expanded ? 680 : 320)
    implicitHeight: root._idle ? root.th.shapeIdle
                               : (root._expanded
                                  ? Math.max(320, root.padH + root.headH + root.listH + root.histH + root.ftH + root.specH + root.mediaH + root.vhH + root.interactH + root.danmakuH + root.searchH + root.submitH + root.actH + root.padH)
                                  : (root._showActionsNow
                                     ? Math.max(root.collapsedH + root.actH + 10, 90)
                                     : root.collapsedH))
    // ⚠️ 不给 width/height 赋值：宿主 QQuickWidget 用 SizeRootObjectToView 时根随视图，
    //    显式写 width: implicitWidth 会被 view 覆盖，且与 Behavior 打架。

    // 2026-10-27（手感优化）：收起与展开**分开用曲线**，不再共用一个 OutBack。
    //   原来两者都是 `Easing.OutBack` —— 该曲线默认 overshoot = 1.70158，
    //   在 width/height 上意味着**过冲 70%**：胶囊先涨过头再弹回来，观感"笨"；
    //   而且展开/收起同为 380ms，收拢时还在慢慢回弹，显得拖沓。
    //   更麻烦的是它卡：width 与 height 各挂一条 Behavior，动画期间每帧都要
    //   重算整条布局链（尺寸 → 圆角 → 内部 face 的隐式尺寸），低端机直接掉帧。
    //   现在：
    //     · 展开 → OutBack，但 overshoot 压到 1.15（留物理感，去掉"弹过头"）；
    //     · 收起 → OutCubic 且更短（240ms）：收拢要干脆，不该"弹一下才回去"；
    //     · overshoot 对 OutCubic 无效，给 1.0 只为写法统一，不会被读到。
    Behavior on width {
        NumberAnimation {
            duration: root._expanded ? root.th.durLong : 240
            easing.type: root._expanded ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: root._expanded ? 1.15 : 1.0
        }
    }
    Behavior on height {
        NumberAnimation {
            duration: root._expanded ? root.th.durLong : 240
            easing.type: root._expanded ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: root._expanded ? 1.15 : 1.0
        }
    }

    // ══════════════════════════════════════════════════════════
    // 手势 MouseArea（⚠️ 必须在最底层，否则吃掉按钮点击）
    // ══════════════════════════════════════════════════════════
    // ⚠️⚠️ 2026-10-11 修 bug「确认按钮点不到」：
    //    原来 MouseArea 声明在**最后**（z 层最高），ActionButton 的 MouseArea 在它下面
    //    ⇒ 点击被根 MouseArea 吞掉，ActionButton 永远收不到 onClicked。
    //    修法：把 MouseArea 移到**最前面**（z 最低），让上面的按钮先收到事件。
    //    Rectangle 不吃鼠标事件，所以空白区的点击自然穿透到这里。
    MouseArea {
        id: gestureArea
        anchors.fill: parent
        z: -1   // 确保在所有内容之下
        enabled: root._interactive || !root._idle
        acceptedButtons: Qt.LeftButton
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        // 函数式签名（qmllint 建议）：mouse 形参显式传入，不靠隐式参数名
        onClicked: function(mouse) {
            // 有动作按钮时不做展开/收起（避免点按钮周围误触展开）
            if (root._showActionsNow) return
            // 组件5：点到禁用态控件上不切换（穿透会误把岛收起，见
            // hitDisabledControl 注释）
            if (root.hitDisabledControl(mouse.x, mouse.y)) return
            if (root.bridgeRef && root.bridgeRef.requestExpand)
                root.bridgeRef.requestExpand(!root._expanded)
        }
    }

    // ══════════════════════════════════════════════════════════
    // 鲸鱼娘（2026-10-11 · dwh 移植）—— 右下角，所有形态都在
    // ══════════════════════════════════════════════════════════
    WhaleMascot {
        id: whale
        theme: root.th
        // #330 形态多样化：紧凑外框里不画鲸鱼 ——
        //   角标只有 96/112px，鲸鱼 28px 会直接盖住标题；
        //   通栏里鲸鱼在右下角，会压住右侧那段描述文字。
        // #330：紧凑外框不画；2026-10-17：居中卡片也不画 —— 卡片内容居中，
        // 右下角挂一只鲸鱼会在 520×200 里显得"贴了张贴纸"。全屏保留（大屏右下角合适）。
        // 大卡片档（Centered/Fullscreen）也不画：全屏会冒一颗标题气泡（鲸鱼对新通知
        // 自动 speak），与正文重复；520×200 的卡片右下角挂吉祥物也像贴纸。
        // 2026-10-27（G4/G5/G6）：**面板态也不画** —— 面板是看板（课表/倒计时），
        // 右下角一只鲸鱼 + 一句标题气泡（"今天"/"今日课表"）纯属重复信息，
        // 还会压住课表最后一行（640×320 里第 7~8 节正好在右下）。
        visible: !root._compactForm && !root._bigForm && !root._panelOn
        // 空闲态鲸鱼正常大小（28px），底层扩大了能放下
        px: root._idle ? 28 : (root._expanded ? 36 : 28)
        bridgeRef: root.bridgeRef
        showWhale: true
        anchors.right: parent.right
        anchors.rightMargin: root._idle ? 6 : 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root._idle ? 2 : 4
        opacity: root._idle ? 0.7 : 1.0
        z: 100

        // 2026-10-18：透明度过渡走 durMid 令牌（原硬编码 200，值相同、改为可调）
        Behavior on opacity { NumberAnimation { duration: root.th ? root.th.durMid : 200 } }
        // 图标尺寸同尺寸动画口径（OutBack 过冲压到 1.15），避免图标"跳一下"
        Behavior on px {
            NumberAnimation {
                duration: root._expanded ? root.th.durLong : 240
                easing.type: root._expanded ? Easing.OutBack : Easing.OutCubic
                easing.overshoot: root._expanded ? 1.15 : 1.0
            }
        }
    }

    // ── 底 ──────────────────────────────────────────────────
    // 2026-10-17：矩形 → **G2 圆角容器**（IslandSurface）。
    //   `Rectangle.radius` 画的是圆弧：曲率在"角 → 直边"的接缝处从 1/r 突跳到 0，
    //   大圆角/大面积上能看出一道折。G2（squircle）让曲率连续过渡，角是"长出来的"。
    //   实现见 IslandSurface.qml（Canvas 超椭圆；QtQuick.Shapes 在被控端不可用）。
    IslandSurface {
        id: shell
        anchors.fill: parent
        // 圆角六档（#330 形态多样化 + 2026-10-17 补 Centered/Fullscreen 两档）
        //   空闲半胶囊   → 正圆（height/2）
        //   Mini 正圆档  → 正圆（无描述时收成角标，与 C++ geometryForForm 同口径）
        //   Mini 方牌档  → 28（比胶囊的 18 更"卡片"，但比卡片小）
        //   Banner 通栏  → 20（与 Centered 卡片同族，横幅不该比卡片更"圆"）
        //   Centered     → 24（520×200 的卡片，24 ≈ 12% 短边，iOS 卡片的手感）
        //   Fullscreen   → 28（铺满屏的大面板，再大就"像被啃掉角"）
        //   胶囊收起/展开 → height/2 / 18（原样不动）
        radius: root._idle ? height / 2
                            : (root._formKind === "mini"
                               ? (root._miniOrb ? height / 2 : 28)
                               : (root._formKind === "banner"
                                  ? 20
                                  : (root._formKind === "centered"
                                     ? 24
                                     : (root._formKind === "fullscreen"
                                        ? 28
                                        : (root._expanded ? 18 : height / 2)))))
        // 底色：全屏档按 severity 换底（与 C++ paintBigForm 的 full 分支同口径 ——
        //   urgent 红 / inform 琥珀 / remind 绿，alpha 210/205 也是手绘版的值）。
        //   "双绘 parity" 铁律：改这里必须同步 island.cpp，否则 QML 挂掉退回手绘时
        //   同一条通知会换一个颜色，老师会以为发了两条。
        //   Centered 卡片**不**随 severity 换底（只靠顶部细条 + 图标色）；手绘版同样只有 full 换底。
        fillColor: {
            if (root._idle) return root.th.idleBg
            if (root._formKind === "fullscreen" && root._severity !== "")
                return root.th.severityColor(root._severity)
            return root.th.bg
        }
        strokeWidth: root._idle ? 0 : 1
        strokeColor: root.th.light ? "#E2E2E2" : "#333333"

        // 圆角此前**没有缓动**（线性 380ms）—— 与带曲线的尺寸动画叠在一起时，
        // 圆角和尺寸各走各的，收起的最后一截会看到"角是直的、边还在动"的生硬感。
        Behavior on radius {
            NumberAnimation {
                duration: root._expanded ? root.th.durLong : 240
                easing.type: root._expanded ? Easing.OutBack : Easing.OutCubic
                easing.overshoot: root._expanded ? 1.15 : 1.0
            }
        }

        // 严重度：只做顶部细条。
        // ⚠️ 左右必须内缩到 radius 之外：细条是**直角**，铺满宽度时会在圆角段
        //    切出一条直线（观感是"角上贴了个方片"）。内缩后整条落在直边上。
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: shell.radius
            anchors.rightMargin: shell.radius
            anchors.topMargin: 1
            height: root._severity === "" ? 0 : 3
            radius: height / 2
            visible: root._severity !== ""
            color: root.th.severityColor(root._severity)
        }
    }

    // ══════════════════════════════════════════════════════════
    // 空闲态 —— 已抽为 island/IslandIdleFace.qml（D 档拆分·第一块）
    // ══════════════════════════════════════════════════════════
    // 只搬不改：几何/锚/字号/动效逐字保留（验证＝diff-island-tree.mjs 零差异）。
    // ⚠️ 实例化处必须**显式传依赖** —— 隐式作用域不会跟着搬进独立文件。
    IslandIdleFace {
        id: idleFace
        anchors.fill: parent
        th: root.th
        idle: root._idle
    }

    // ══════════════════════════════════════════════════════════
    // 紧凑外框（#330 形态多样化 · 2026-10-17）
    // 两个面都 `anchors.fill: parent`：窗口几何已由 C++ 按形态算死，
    // 面只负责排版（不参与尺寸推导）。互斥由 form 串判定。
    // ⚠️ 必须显式传 th/icon/title/desc —— 隐式作用域不跟着搬进独立文件。
    // ══════════════════════════════════════════════════════════
    IslandBannerFace {
        id: bannerFace
        anchors.fill: parent
        th: root.th
        icon: root._icon
        title: root._title
        desc: root._desc
        time: root._showNoticeTime ? root._noticeTime : ""
        visible: root._formKind === "banner"
    }

    IslandMiniFace {
        id: miniFace
        anchors.fill: parent
        th: root.th
        icon: root._icon
        title: root._title
        desc: root._desc
        visible: root._formKind === "mini"
    }

    // ══════════════════════════════════════════════════════════
    // 大卡片外框（2026-10-17 补 · Centered / Fullscreen）
    // 之前这两档没有版式，落进胶囊分支 ⇒ 圆角/排版/按钮全部错位
    //（用户反馈"很多通知样式不符合审美"的根因）。现在各给一套面，
    // 与胶囊内容互斥；配色仍以主题令牌为准，不再由 QPainter 自己算。
    // ══════════════════════════════════════════════════════════
    IslandCardFace {
        id: cardFace
        anchors.fill: parent
        th: root.th
        icon: root._icon
        title: root._title
        desc: root._desc
        severity: root._severity
        actions: root._actions
        bridgeRef: root.bridgeRef
        time: root._showNoticeTime ? root._noticeTime : ""
        visible: root._formKind === "centered"
    }

    IslandFullscreenFace {
        id: fullFace
        anchors.fill: parent
        th: root.th
        icon: root._icon
        title: root._title
        desc: root._desc
        severity: root._severity
        actions: root._actions
        bridgeRef: root.bridgeRef
        time: root._showNoticeTime ? root._noticeTime : ""
        visible: root._formKind === "fullscreen"
    }

    // ══════════════════════════════════════════════════════════
    // 组件面板（2026-10-27 · G4/G5/G6）—— 日期 / 课表 / 倒计时
    // ══════════════════════════════════════════════════════════
    // 只在展开态显示：收起态用面板自带的 title/desc 摘要（不是空板子）。
    // 与下面"活动态"互斥：面板在时，通知那一套内容整体让位 —— 两套版式同时画
    // 会互相压（同 banner/mini 与胶囊的互斥理由）。
    // ⚠️ 宿主只是 Loader 壳子，三枚面各自独立文件、按需加载（项目铁律丙）。
    IslandPanelFace {
        id: panelFaceItem
        anchors.fill: parent
        th: root.th
        face: root._panelFace
        info: root._panelInfo
        visible: root._panelOn
    }

    // ══════════════════════════════════════════════════════════
    // 活动态 收起 / 展开
    // ══════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        // #330：紧凑外框（Banner/Mini）有自己的面，胶囊那套内容整体让位
        visible: !root._idle && !root._compactForm && !root._bigForm && !root._panelOn
        opacity: root._idle ? 0 : 1
        // 2026-10-18：走 durShort 令牌（原硬编码 160）
        Behavior on opacity { NumberAnimation { duration: root.th ? root.th.durShort : 160 } }

        // ── 灵动岛 6 形态叠加层（2026-10-27 收口蓝图）──
        // 仅在 C++ 裁决出形态（morphMode !== none）时接管收起态胶囊内容；
        // 与既有通知胶囊互斥（collapsedRow 在其激活时让位），互不打架。
        IslandMorph {
            id: morphLayer
            anchors.fill: parent
            z: 5
            visible: root._morphOn
        }

        // ── 收起态 ───────────────────────────────────────
        Item {
            id: collapsedRow
            visible: !root._expanded && !root._morphOn
            anchors.fill: parent

            SvgIcon {
                id: csIcon
                name: bridgeRef ? bridgeRef.icon : ""
                tint: root.th.fg
                px: 22
                anchors.left: parent.left
                anchors.leftMargin: 14
                // 组件1：岛面是媒体时整体上移，给底部时间行/进度条让位
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.mediaIsContent ? -8 : 0
            }

            Text {
                id: csTitle
                anchors.left: csIcon.right
                anchors.leftMargin: 12
                // 2026-10-17「通知写上时间」：右上角让出时间的位置（时间本身贴鲸鱼左缘）。
                // 媒体态不显示时间 ⇒ 回落到原来的 42（给鲸鱼让位）。
                anchors.right: root._showNoticeTime ? csTime.left : parent.right
                anchors.rightMargin: root._showNoticeTime ? 6 : 42
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.mediaIsContent ? -8 : 0
                // 组件1：岛面是媒体时字号降到 13（长曲名不截断）。
                // ⚠️ 不加「正在播放：」前缀：岛宽由 C++ 按内容定（收起态 ~200 起，
                //    geometryForContent），前缀 ~78px 会把曲名挤成省略号；
                //    "在播放"由音乐图标 + 进度条 + 时间文字表达，不缺这一句。
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: root.mediaIsContent ? 13 : root.th.fsTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            // 通知时间：与标题同一基线，贴鲸鱼左缘（右侧 42 是鲸鱼的领地）。
            // 用 10px + sub 色：它是"辅助信息"，不能和标题抢视线。
            Text {
                id: csTime
                visible: root._showNoticeTime
                anchors.right: parent.right
                anchors.rightMargin: 46
                anchors.verticalCenter: csTitle.verticalCenter
                text: root._noticeTime
                color: root.th.sub
                // 2026-10-17：10px 在 332 胶囊里和鲸鱼糊成一片（实测截图），回到 11
                font.pixelSize: 11
            }

            // 渠道标签（2026-10-27 · F 收尾第 2 项）：落在**描述行首**，与描述同一行。
            // 为什么不放标题行：标题是主角，抢占宽度会把它压成省略号（岛宽由 C++ 按内容定）；
            // 描述行本来就有一条空位，"渠道 + 内容"读起来正是「课程提醒：数学 · 08:00」。
            // ⚠️ 媒体态不标（媒体不是通知，标了反而误读）；媒体态本来就占满这行。
            IslandChannelChip {
                id: csChannel
                visible: !root.mediaIsContent && root._channel !== ""
                th: root.th
                text: root._channel
                anchors.left: csTitle.left
                anchors.top: csTitle.bottom
                anchors.topMargin: 1
            }

            Text {
                visible: text !== ""
                // 有渠道标签时描述让位到标签右侧（6px）；无标签时逐字回到旧版式
                anchors.left: csChannel.visible ? csChannel.right : csTitle.left
                anchors.leftMargin: csChannel.visible ? 6 : 0
                anchors.right: parent.right
                anchors.rightMargin: 42
                anchors.top: csTitle.bottom
                anchors.topMargin: 1
                text: bridgeRef ? bridgeRef.desc : ""
                color: root.th.sub
                font.pixelSize: root.th.fsNote
                elide: Text.ElideRight
            }

            // ── 组件1：收起态媒体播放条（2026-10-15）────────────
            // 岛面是媒体时（mediaIsContent）补三样：底部 3px 进度条 / 已播-总长 /
            // 「点按展开互动」提示。图标+标题用上面 csIcon/csTitle 的 -8px 让位。
            // 提示用纯文字 —— ↑ 是符号字形，教室机字体不全会渲染成豆腐块。
            // 组件7（2026-10-15）四态：进度条与左下文案随 _mediaPhase 变 ——
            //   广播开始「即将播放」/ 播放中「已播 / 总长」/ 快结束「剩余 mm:ss」
            //   （进度条转琥珀）/ 播完「播放结束」（无进度条）/ 禁播（无进度条 +
            //   标题即提示语；图标由 C++ showIsland 给的 alert 驱动）。
            Item {
                id: mediaPlayBar
                visible: root.mediaIsContent
                anchors.fill: parent

                // 进度条（右缘停在鲸鱼左缘：鲸鱼占右下 ~36px）
                // 组件7：播完/禁播没有进度可画 ⇒ 整条撤下（留白交给左下文案）。
                Rectangle {
                    visible: !root._mediaNoProgress
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 40
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    height: 3
                    radius: 1.5
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)
                    clip: true

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: root._mediaTotalMs > 0
                            ? Math.max(0, parent.width * Math.min(1, root._mediaElapsedMs / root._mediaTotalMs))
                            : 0
                        radius: 1.5
                        // 组件7：快结束 → 进度条转琥珀（"要完了"的预警）；其余走强调色。
                        color: root._mediaEnding ? root.mediaWarnColor() : root.th.accent
                    }
                }

                // 左下：四态文案（口径在 mediaLeftText()，展开态 mediaRow 共用）
                Text {
                    // ⚠️ 2026-10-15 组件7 顺带修复（探针日志实锤，非本次引入的改动）：
                    //    原写 `anchors.left: csTitle.left`，但本 Text 在 mediaPlayBar 内、
                    //    csTitle 在 collapsedRow 内 ⇒ 对它是"父的兄弟"，QML 报
                    //    "Cannot anchor to an item that isn't a parent or sibling" 并
                    //    **丢弃该锚**。改绝对值：csIcon.left(14) + 图标宽 22 +
                    //    csTitle 的 leftMargin(12) = 48（与 csTitle 左缘逐字对齐）。
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    visible: text !== ""
                    text: root.mediaLeftText()
                    color: root._mediaEnding ? root.mediaWarnColor() : root.th.sub
                    font.pixelSize: 11
                }

                // 右下：展开互动提示
                // ⚠️ 宽度阈值：岛宽由 C++ 按内容定（~200 起），窄岛下本提示会与
                //    左下时间文字 x 重叠（实测 200 宽：时间 48..113 vs 提示 98..158）
                //    ⇒ 放不下就撤，保时间文字（进度条+图标已够表意）。
                // 组件7：快结束时改述"即将结束"；播完/禁播撤下（没得听）。
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 42
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    text: root._mediaEnding ? "即将结束" : "点按展开互动"
                    color: root._mediaEnding ? root.mediaWarnColor() : root.th.accent
                    font.pixelSize: 10
                    visible: parent.width >= 230 && !root._mediaNoProgress
                }
            }
        }

        // ── 展开态 ───────────────────────────────────────
        Item {
            id: expandedCol
            visible: root._expanded
            anchors.fill: parent
            anchors.margins: root.padH

            // 展开态渠道标签（2026-10-27 · F 收尾第 2 项）：标题**正上方**一行。
            // ⚠️ 必须有条件地让位：exTitle 的 topMargin 跟着它切（2 ↔ 26）。
            //    不用 visible:false 的"幽灵占位"——锚链对不可见 item 依然起效
            //    （stList 那条注释记的坑同源），否则无渠道的通知会被顶下去 22px。
            IslandChannelChip {
                id: exChannel
                visible: root._channel !== ""
                th: root.th
                text: root._channel
                anchors.left: parent.left
                anchors.top: parent.top
            }

            Text {
                id: exTitle
                // 组件2：搜索页打开时隐藏大标题并**清零高度**——visible:false 不减
                // 几何，锚链（exDesc/stList/mediaRow…）照旧从它底部起算会把下面
                // 全顶下去（2026-10-15 实测：隐藏不零高 = 搜索页底边被裁 35px）。
                // 歌名在 mediaRow（"正在播放 晴天 - 周杰伦"）仍有，不丢信息。
                // 组件3：投稿页同款让位。
                visible: !root.searchPageOn && !root.submitPageOn
                height: (root.searchPageOn || root.submitPageOn) ? 0 : implicitHeight
                anchors.left: parent.left
                // 2026-10-17：右上角给时间让位（展开态看详情时更需要知道几点来的）
                anchors.right: root._showNoticeTime ? exTime.left : parent.right
                anchors.rightMargin: root._showNoticeTime ? 8 : 48
                anchors.top: parent.top
                // 2026-10-27：有渠道标签时下移 24（标签 16 + 8 间距）；无标签回到 2
                anchors.topMargin: exChannel.visible ? 24 : 2
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                // #330：多界面开面时标题**压到 20px 单行** —— 面面板要吃内容区，
                // 128px 的自适应大字标题会把面板挤成一条缝（2026-10-17 截图实证）。
                font.pixelSize: root.facesOn ? Math.min(root.exTitlePx, 20) : root.exTitlePx
                font.weight: Font.Bold
                minimumPixelSize: root.th.fsTitleXs
                fontSizeMode: Text.Fit
                wrapMode: Text.WrapAnywhere
                maximumLineCount: root.facesOn ? 1 : 3
                elide: Text.ElideRight
            }

            // 展开态通知时间：贴标题**首行**基线（不是整个多行标题块的垂直中心，
            // 否则标题折成 3 行时时间会漂到中间，看着像副标题）。
            Text {
                id: exTime
                visible: root._showNoticeTime
                anchors.right: parent.right
                anchors.rightMargin: 48
                anchors.top: exTitle.top
                anchors.topMargin: 3
                text: root._noticeTime
                color: root.th.sub
                font.pixelSize: 11
            }

            // 描述：Markdown 分行（**粗** / # 标题 / - 列表 / \n）
            // 用 RichText 直接吃桥预渲染的 descHtml；多行、超宽自动回退省略。
            Text {
                id: exDesc
                anchors.left: exTitle.left
                anchors.right: exTitle.right
                anchors.top: exTitle.bottom
                anchors.topMargin: 6
                visible: !root._ftActive   // #198：传输中隐藏 stale 的 "0%" 描述，改由进度条呈现
                text: bridgeRef ? bridgeRef.descHtml : ""
                textFormat: Text.RichText
                color: root.th.sub
                // 2026-10-12 用户反馈「字数太少太小」：正文 13→15px，
                // 行数上限 5→10（原来长通知第 6 行起就被 … 吃掉）。
                font.pixelSize: root.th.fsBody + 2
                font.family: "Microsoft YaHei UI, PingFang SC, sans-serif"
                wrapMode: Text.WordWrap
                maximumLineCount: 10
                elide: Text.ElideRight
                lineHeight: 1.32
            }

            StateList {
                id: stList
                // 组件2：搜索页打开时隐藏状态列表（§七 一次只展开一件事）。
                // ⚠️ 同样必须**清零高度**：ftRow/specRow/mediaRow 锚 stList.bottom，
                //    隐藏不零高 = 45px 幽灵占位把播放栏/搜索页整体顶下去（锚链对
                //    不可见 item 依然起效——2026-10-15 实测）。
                // 组件3：投稿页同款让位。
                visible: !root.searchPageOn && !root.submitPageOn
                height: (root.searchPageOn || root.submitPageOn) ? 0 : stList.implicitHeight
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: exDesc.bottom
                anchors.topMargin: 8
                theme: root.th
                items: root._states
            }

            // ── #198 文件接收进度条（展开态，传输中可见）──────
            // 标题已是"正在接收 <name>"（C++ 设的），这里只补实时进度。
            Item {
                id: ftRow
                visible: root._ftActive
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: stList.bottom
                anchors.topMargin: root._ftActive ? 12 : 0
                height: visible ? 40 : 0

                // 进度槽
                Rectangle {
                    id: ftTrack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 8
                    radius: 4
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)
                    clip: true

                    // 已收部分（宽度随 _ftPct 实时变化）
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.max(0, parent.width * root._ftPct)
                        radius: 4
                        color: root.th.accent
                        // 2026-10-18：文件传输进度条走 durXShort（原硬编码 120）
                        Behavior on width { NumberAnimation { duration: root.th ? root.th.durXShort : 120 } }
                    }
                }

                // 左：实时百分比 + 已收/总体积
                Text {
                    anchors.left: parent.left
                    anchors.top: ftTrack.bottom
                    anchors.topMargin: 5
                    text: (root._ftTotal > 0
                           ? Math.round(root._ftPct * 100) + "% · "
                             + root.fxHumanBytes(root._ftBytes) + " / " + root.fxHumanBytes(root._ftTotal)
                           : root.fxHumanBytes(root._ftBytes) + " · 大小未知")
                    color: root.th.sub
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                // 右：状态标
                Text {
                    anchors.right: parent.right
                    anchors.top: ftTrack.bottom
                    anchors.topMargin: 5
                    text: "接收中"
                    color: root.th.accent
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }

            // ── §6.3 语音频谱（展开态 · 对讲中）────────────────────────
            // 2026-10-13 修 bug「被控端频谱显示到窗口去了」：原实现是 C++ 里 `new
            // SpectrumWidget()`（**无 parent**）+ show() ⇒ 桌面 (20,20) 弹独立顶层窗口。
            // 规格 §6.3 要求「语音对讲 → 岛展开；展开态显示 16 位频谱」⇒ 现在由岛自己画。
            // 数据走独立信号 spectrumChanged（~50Hz），不依赖 changed()。
            Item {
                id: specRow
                visible: root._expanded && root._spectrumActive
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: ftRow.bottom
                anchors.topMargin: visible ? 12 : 0
                height: visible ? 34 : 0

                Text {
                    id: specLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // ⚠️ 不用 "♪" 之类的符号字形：教室机字体不全时会渲染成豆腐块。
                    //    中文一定有（岛里其它文案全是中文）。
                    text: "语音"
                    color: root.th.sub
                    font.pixelSize: 11
                }

                Row {
                    id: specBarsRow
                    anchors.left: specLabel.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    spacing: 3

                    Repeater {
                        id: specBars
                        // 长度由 C++ 给（kSpectrumBands = 16）；空数组时不渲染。
                        model: root._spectrumBands
                        delegate: Item {
                            // 等分剩余宽度（给 label + 间隙留位）
                            width: Math.max(2, (specBarsRow.width - (specBars.count - 1) * 3)
                                               / Math.max(1, specBars.count))
                            height: specBarsRow.height
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: Math.max(2, parent.height
                                                    * (typeof modelData === "number" ? modelData : 0))
                                radius: 1.5
                                color: root.th.accent
                                // ⚠️ 2026-10-18「统一动画时长为主题令牌」的**例外**：
                                //    频谱条每帧（~50Hz）都在变，60ms 是"平滑但不拖影"的
                                //    调优点；套 durInstant(90) 会让柱子互相追赶、视觉发糊。
                                //    高频信号的过渡时长**不该**跟 UI 动效 token 走。
                                Behavior on height { NumberAnimation { duration: 60 } }
                            }
                        }
                    }
                }
            }

            // ── P3 播放时段管理：岛底「正在播放」栏（展开态 + 媒体在播）──
            // ⚠️ 2026-10-14 修布局 bug：本栏原在**根级**（expandedCol 的兄弟），却锚
            //    expandedCol 内 specRow.bottom —— 跨父级锚被 QML 静默丢弃，整栏落到
            //    y=0（岛顶）。探针 _island_qml_probe --media 全子树几何转储实测：
            //    同父级 left/right 锚正常（w=236），唯 top 锚失效（y=0）。
            //    修法＝移进 expandedCol 做 specRow 的**同父级兄弟**，与 stList/ftRow/
            //    specRow 同一锚定范式（root 级的行只锚 parent 或用预算公式，见
            //    actionRow / historyPanel，都是刻意避开跨父级锚的）。
            Item {
                id: mediaRow
                // 组件7：播完（ended）/禁播（blocked）时 _mediaActive 已是 false
                // （m_mediaOn 关掉了），但这一行还要画 —— 它负责讲"播完了"/
                // "为什么没播" ⇒ 用 mediaIsContent 兜住。
                visible: root._expanded && (root._mediaActive || root.mediaIsContent)
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: specRow.bottom
                anchors.topMargin: visible ? 12 : 0
                height: visible ? 34 : 0

                // ▶ 是符号字形，教室机字体不全会 tofu（specRow 注释里有同样教训）
                //    ⇒ 用矢量 play 图标，不依赖任何字体覆盖。
                // 组件7：禁播换 alert（C++ showIsland 传的就是 alert，这里只是显式化）。
                SvgIcon {
                    id: mediaPlayIcon
                    name: root._mediaBlocked ? "alert" : "play"
                    tint: root.th.fg
                    px: 12
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: mediaTitleTxt
                    anchors.left: mediaPlayIcon.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    // 组件7：禁播时标题位就是"为什么没播"（C++ 把 reason 写进 mediaTitle）。
                    text: root._mediaTitle || "正在播放"
                    color: root.th.fg
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    // 有下一首：右栏占 40%；无下一首：给右侧时长让位
                    // （implicitWidth 实测 ~65px + 图标/间隙；原写法直接 parent.width
                    //   会让长标题跑到时长文字下面）
                    width: root._mediaNextTitle
                        ? parent.width * 0.6
                        : Math.max(40, parent.width - mediaTimeTxt.implicitWidth - 30)
                }

                Text {
                    id: mediaTimeTxt
                    // 组件7：有四态文案要看时优先显示（快结束/播完/禁播比"下一首是谁"
                    // 更即时）；常态下仍让位给"下一首"（详规痛点点名项）。
                    visible: root._mediaNextTitle === "" || root._mediaEnding
                             || root._mediaStarting || root._mediaNoProgress
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.mediaLeftText()
                    color: root._mediaEnding ? root.mediaWarnColor() : root.th.sub
                    font.pixelSize: 11
                }

                // 下一首 + 封面小演示（图1 痛点点名项）
                // ⚠️ 2026-10-14 修：原写法 Row 不定宽 + 子 Text width 绑 Row.width
                //    ⇒ 互相依赖，整组塌成 0x0（探针实测 QQuickRow w=0 h=0，
                //   "下一首：稻香"完全不显示）。改绝对锚：文本左起、右界停在封面
                //    左缘；封面右锚定，两边无相互依赖。
                Item {
                    id: mediaNext
                    // 组件7：快结束/广播开始/播完/禁播时让位给四态文案（mediaTimeTxt）。
                    visible: root._mediaNextTitle !== "" && !root._mediaEnding
                             && !root._mediaStarting && !root._mediaNoProgress
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.4
                    height: parent.height

                    Text {
                        id: mediaNextTxt
                        anchors.left: parent.left
                        anchors.right: mediaNextCover.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "下一首：" + root._mediaNextTitle
                        color: root.th.sub
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        id: mediaNextCover
                        visible: root._mediaNextCover !== ""
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 28; radius: 4
                        color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.12)
                        clip: true
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            source: (root._mediaNextCover.indexOf("://") >= 0)
                                    ? root._mediaNextCover
                                    : (root._mediaNextCover ? "file:///" + root._mediaNextCover : "")
                        }
                    }
                }
            }

            // ── VoiceHub 联动（#329）：远程广播「正在播放」行（展开态）────
            // 与 P3 mediaRow **同父级兄弟**（expandedCol 内），锚 mediaRow.bottom
            // —— 跨父级锚会被 QML 静默丢弃（2026-10-14 mediaRow 踩坑固化：top 锚
            // 失效整栏落 y=0）。mediaRow 不可见时其 height=0，锚链照常成立
            // （锚链对不可见 item 依然起效，隐藏靠 height:0 清零）。
            // 布局学 mediaRow：标题左起弹性、右侧绝对锚时间文本，两边无相互依赖
            // （2026-10-14 的 Row 互依塌 0x0 教训）。第二行「接下来/人数」有条件
            // 出现，vhH 预算跟着 vhLine2 走（见上方属性区）。
            Item {
                id: vhRow
                visible: root.vhOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: mediaRow.bottom
                anchors.topMargin: visible ? 8 : 0
                height: visible ? (root.vhLine2 ? 44 : 30) : 0

                // radio 图标（资源库有 radio.svg；不用 ♪ 符号字形，教室机字体
                // 不全会 tofu —— specRow/mediaRow 同款教训）。
                SvgIcon {
                    id: vhIcon
                    name: "radio"
                    tint: root.th.accent
                    px: 12
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 9
                }

                Text {
                    id: vhTitleTxt
                    anchors.left: vhIcon.right
                    anchors.leftMargin: 6
                    anchors.right: vhTimeTxt.left
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    height: 18
                    verticalAlignment: Text.AlignVCenter
                    text: {
                        if (!root.vhRef) return "";
                        const t = root.vhRef.title || "正在播放";
                        return root.vhRef.artist ? t + " — " + root.vhRef.artist : t;
                    }
                    color: root.th.fg
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    id: vhTimeTxt
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 18
                    verticalAlignment: Text.AlignVCenter
                    text: root.vhTimeText()
                    color: root.th.sub
                    font.pixelSize: 11
                }

                Text {
                    id: vhSubTxt
                    visible: root.vhLine2
                    anchors.left: vhIcon.right
                    anchors.leftMargin: 6
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.top: vhTitleTxt.bottom
                    anchors.topMargin: 2
                    height: 14
                    verticalAlignment: Text.AlignVCenter
                    text: root.vhSubText()
                    color: root.th.sub
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            // ── 组件1：互动区（点赞计数 + 好听/点歌，2026-10-15）────────
            // 点歌互动详规的被控端互动骨架：媒体占据岛面时，展开态在播放栏之下
            // 给互动入口。好听反馈组件4 已接线（单击点赞 + 飘心 600ms + 计数
            // 回弹 150ms + 一次制）；点歌入口组件2 已接线（原地变形开搜索页，
            // 本行随之让位）；点赞计数生产值组件6 接桥后改绑定。
            // ⚠️ 与 mediaRow **同父级兄弟**：跨父级锚会被 QML 静默丢弃
            //    （2026-10-14 mediaRow 踩过——top 锚失效整栏落 y=0，见上方注释）。
            // 组件2：搜索页打开时本行隐藏（§七 一次只展开一件事）。
            Item {
                id: interactRow
                // 组件2：搜索页打开时本行隐藏（§七 一次只展开一件事）。
                // 组件3：投稿页打开时同样让位（入口互斥：开投稿页前会关搜索页）。
                visible: root.mediaInteractOn && !root.searchOn && !root.submitOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: root.padH + 40   // 与 actionRow 同款让位（鲸鱼 z:100 占右下）
                // 2026-10-17（#329）：锚点从 mediaRow.bottom 改到 vhRow.bottom ——
                // VoiceHub 行插在两者之间，预算公式（mediaH+vhH+interactH）与锚链
                // 必须同序；vhRow 不可见时 height=0，锚链照常成立。
                anchors.top: vhRow.bottom
                anchors.topMargin: visible ? 12 : 0
                height: visible ? 46 : 0

                // 左：点赞图标 + 计数
                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    SvgIcon {
                        name: "heart"
                        tint: root.th.accent
                        px: 16
                    }

                    Text {
                        id: likeCountText
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(root.likeCount)
                        color: root.th.fg
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        // 组件4：计数回弹（likePop 1.0⇄1.35，150ms 两段动画）
                        scale: root.likePop
                        transformOrigin: Item.Center
                    }
                }

                // 组件4：飘心——点「好听」时从 heart 图标处飘出，600ms 上飘淡出。
                // ⚠️ 双层结构：外层只做**定位**（锚 interactRow 同父级安全位——
                //    heart 图标就在左 Row 首位 x=0，若直接锚 Row 内 heart 是跨
                //    父级锚会被静默丢弃，2026-10-14 教训）；内层 likeFly **不锚、
                //    y 自由**——动画（PropertyAnimation on y）与探针注入都要写 y，
                //    锚在 y 上会被锚链重置（2026-10-15 实测：verticalCenter 锚下
                //    注入 y=-14 被重置回 14，动画同样会被抗击）。
                // 动画用 QML 内置 ParallelAnimation（不引 Timeline/第三方）：
                // y 0→-28 + opacity 1→0，结束自动隐藏（可见时也不占布局——
                // 动画属性不参与高度预算，interactH 恒 46）。
                Item {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    Item {
                        id: likeFly
                        objectName: "likeFly"   // 组件4：探针按名注入/读取飘心中途态
                        visible: false
                        width: 18
                        height: 18
                        z: 10
                        SvgIcon {
                            anchors.fill: parent
                            name: "heart"
                            tint: root.th.accent
                            px: 18
                        }
                    }
                }
                ParallelAnimation {
                    id: likeFlyAnim
                    running: false
                    PropertyAnimation {
                        target: likeFly; property: "y"
                        // 2026-10-18：飞心时长走 durXLong 令牌（原硬编码 600）
                        to: -28; duration: root.th ? root.th.durXLong : 600
                        easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        target: likeFly; property: "opacity"
                        to: 0.0; duration: root.th ? root.th.durXLong : 600
                    }
                    onFinished: likeFly.visible = false
                }
                // 组件4：计数回弹（150ms = 75 放大 + 75 缩回；详规口径）
                // ⚠️ 2026-10-18 令牌化例外：75ms 是"放大/缩回"两段对称弹性，
                //    合成 150ms 语义；令牌阶梯里没有 75 这一档，强行套 durInstant(90)
                //    会变成 180ms，回弹变拖沓、失去"Q 弹"手感。
                SequentialAnimation {
                    id: likePopAnim
                    running: false
                    NumberAnimation {
                        target: root; property: "likePop"
                        to: 1.35; duration: 75; easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        target: root; property: "likePop"
                        to: 1.0; duration: 75
                    }
                }
                function fireLike() {
                    root.likeCount += 1
                    root.likeOn = true
                    likePopAnim.restart()          // 计数回弹 150ms
                    likeFly.y = 0                  // 飘心归位再放（重触发安全）
                    likeFly.opacity = 1
                    likeFly.visible = true
                    likeFlyAnim.restart()          // 飘心 600ms
                    // 组件6：点赞旁路上报云端（fire-and-forget，成败不影响本地动效；
                    // 云端限额/汇总见 interact.js，桥未注入时静默跳过）
                    if (root.bridgeRef) root.bridgeRef.reportFeedback("like", 1)
                }

                // 右：好听（secondary）+ 点歌（primary）
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    ActionButton {
                        theme: root.th
                        objectName: "abtn_like"   // 组件4：探针按名直达派发合成点击
                        // 一次制：点过 ⇒ 文案「已好听」+ enabled 传导置灰
                        label: root.likeOn ? "已好听" : "好听"
                        role: "secondary"
                        enabled: !root.likeOn
                        onClicked: interactRow.fireLike()
                    }

                    ActionButton {
                        theme: root.th
                        label: "点歌"
                        role: "primary"
                        // 组件2：配额满（超额禁用）时置灰，点了也只能看到超额提示页
                        enabled: !root.searchQuotaFull
                        onClicked: {
                            // 组件2：岛内原地变形 —— 开搜索页（interactRow/danmakuRow
                            // 同步让位，高度预算 searchH 顶上，见尺寸区）。
                            // 组件3：与投稿页互斥（§七 一次只展开一件事）⇒ 先关投稿页。
                            root.submitOn = false
                            root.searchOn = true
                            // 配额满 ⇒ 直接落超额禁用态；否则有历史关键词就发起一轮
                            // （骨架 → 防抖后出结果/无结果）。
                            if (root.searchQuotaFull)
                                root.searchPhase = 4
                            else if (root.searchQuery.trim().length > 0)
                                root.searchPhase = 1
                            songSearchInput.forceActiveFocus()
                        }
                    }
                }
            }

            // ── 组件1：祝福弹幕输入行（2026-10-15）────────────────────
            // 岛内只做输入与发送入口；滚动浮层是组件5 的**独立全屏浮层**
            // （原则§一：一次只显示一条，弹幕不塞进岛）。
            // 输入用自绘紧凑款（Rectangle + TextInput）—— 工程铁律不用 Qt TextField；
            // 右缘让位与 interactRow 对齐（padH + 40，鲸鱼占右下 ~36px）。
            // 组件2：搜索页打开时本行让位（与 interactRow 同条件）。
            // 组件3：投稿页打开时同样让位；输入 pill / 发送 pill 点击 ⇒ 开投稿页
            // （携带当前草稿；提交反馈 ✓/✗ 需页面级呈现，直连云端提交留组件5/6）。
            Item {
                id: danmakuRow
                visible: root.mediaInteractOn && !root.searchOn && !root.submitOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: root.padH + 40
                anchors.top: interactRow.bottom
                anchors.topMargin: visible ? 10 : 0
                height: visible ? 36 : 0

                // 左：输入 pill
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: danmakuSend.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    height: 32
                    radius: 16
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10)
                    border.width: 1
                    border.color: root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)

                    TextInput {
                        id: danmakuInput
                        objectName: "danmakuInput"   // 组件3：探针按名注入草稿（验证入口携带）
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        color: root.th.fg
                        font.pixelSize: 12
                        selectionColor: root.th.accent
                        clip: true

                        // TextInput 无原生 placeholder：空态叠一条同式样文本
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: "说句祝福…"
                            color: root.th.sub
                            font.pixelSize: 12
                            visible: danmakuInput.text.length === 0 && !danmakuInput.activeFocus
                        }
                    }

                    // 组件3：输入 pill 整体可点 ⇒ 原地变形开投稿页（携带当前草稿）。
                    // MouseArea 声明在 TextInput 之后 = 覆盖其上：pill 即投稿入口，
                    // 本行 TextInput 退为「草稿箱」（探针注入/投稿页返回写回展示）。
                    // objectName 供探针按名直达派发合成点击（不用内部 id）。
                    MouseArea {
                        id: danmakuPillMa
                        objectName: "danmakuPillMa"
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 组件3：开投稿页，携带当前草稿；与搜索页互斥
                            root.searchOn = false
                            root.submitText = danmakuInput.text
                            root.submitOn = true
                        }
                    }
                }

                // 右：发送 pill
                Rectangle {
                    id: danmakuSend
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    height: 32
                    radius: 16
                    color: sendMa.pressed
                        ? Qt.darker(root.th.accent, 1.15)
                        : (sendMa.containsMouse ? Qt.lighter(root.th.accent, 1.1) : root.th.accent)

                    Text {
                        anchors.centerIn: parent
                        text: "发送"
                        // 浅色主题 accent 深 ⇒ 白字；深色主题 accent 浅 ⇒ 黑字
                        color: root.th.light ? "#FFFFFF" : "#161616"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: sendMa
                        objectName: "danmakuSendMa"   // 组件3：探针按名直达派发合成点击
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 组件3：发送 ⇒ 原地变形开投稿页（携带文字）——
                            // 提交反馈(✓/✗)是详规保留项、需页面级呈现；
                            // 直连云端提交（danmaku 端点）留组件5/6。
                            // 行为差异（相对组件1/2 的 TODO 直连提交）：本处不再
                            // 本地清空即了事，改为开页走四态流程。
                            root.searchOn = false
                            root.submitText = danmakuInput.text
                            root.submitOn = true
                        }
                    }
                }
            }

            // ── 组件2：点歌搜索页（2026-10-15）────────────────────
            // 原地变形：interactRow 的「点歌」→ 本页（互动区/弹幕行同步让位，
            // 高度预算 searchH 顶上）。五态：0 空态(热门占位) / 1 骨架 /
            // 2 结果 / 3 无结果 / 4 超额禁用（配额满）。
            // 数据全本地占位：searchCatalog 过滤（组件6 换云端 /api/songs/search
            // + 提交端点）；探针 setProperty(searchPhase/searchQuery) 即可演示五态。
            // ⚠️ 与 mediaRow/interactRow/danmakuRow **同父级兄弟**（expandedCol 内），
            //    锚 mediaRow.bottom —— 跨父级锚会被静默丢弃（2026-10-14 踩坑固化）。
            // ⚠️ 右缘让位鲸鱼（z:100 占右下 ~36px）：rightMargin = padH + 40，
            //    与 interactRow/actionRow 同款。
            Item {
                id: searchPage
                visible: root.searchPageOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: root.padH + 40
                anchors.top: mediaRow.bottom
                anchors.topMargin: visible ? 12 : 0
                height: visible ? root.searchH : 0

                // ── 搜索行：返回 + 输入 pill + 清空（高 34）────────
                Item {
                    id: searchBar
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 34

                    // 返回（chevron-right 旋转 180° = 左向箭头；图标库无
                    // arrow-left，不引第三方图标，旋转现成矢量即合规）
                    Rectangle {
                        id: searchBack
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: 16
                        color: backMa.pressed
                            ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.15))
                            : (backMa.containsMouse
                               ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                               : Qt.rgba(0, 0, 0, 0))

                        SvgIcon {
                            anchors.centerIn: parent
                            name: "chevron-right"
                            rotation: 180
                            tint: root.th.fg
                            px: 16
                        }

                        MouseArea {
                            id: backMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.searchOn = false   // 原地变形逆操作：回互动区
                        }
                    }

                    // 输入 pill（自绘 Rectangle + TextInput，铁律不用 Qt TextField）
                    Rectangle {
                        anchors.left: searchBack.right
                        anchors.leftMargin: 8
                        anchors.right: searchClear.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        height: 32
                        radius: 16
                        color: root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10)
                        border.width: 1
                        border.color: songSearchInput.activeFocus
                            ? root.th.accent
                            : (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14))

                        SvgIcon {
                            id: searchIcon
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: "search"
                            tint: root.th.sub
                            px: 13
                        }

                        TextInput {
                            id: songSearchInput
                            objectName: "songSearchInput"   // 组件2：探针按名直达同步演示关键词
                            anchors.left: searchIcon.right
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.th.fg
                            font.pixelSize: 12
                            selectionColor: root.th.accent
                            clip: true

                            // 占位（TextInput 无原生 placeholder，同 danmakuInput 范式）
                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: "搜索歌曲或歌手"
                                color: root.th.sub
                                font.pixelSize: 12
                                visible: songSearchInput.text.length === 0 && !songSearchInput.activeFocus
                            }

                            onTextEdited: {
                                root.searchQuery = text
                                if (text.trim().length === 0) {
                                    root.searchPhase = 0
                                } else {
                                    root.searchPhase = 1        // 骨架
                                    searchDebounce.restart()    // 防抖 400ms 出结果/无结果
                                }
                            }
                            // 回车即搜（无云端前走同一防抖路，本地目录过滤）
                            onAccepted: {
                                if (root.searchQuery.trim().length === 0) { root.searchPhase = 0; return }
                                root.searchPhase = 1
                                searchDebounce.restart()
                            }
                        }
                    }

                    // 清空（x）
                    Rectangle {
                        id: searchClear
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24; height: 24; radius: 12
                        visible: songSearchInput.text.length > 0
                        color: searchClearMa.pressed
                            ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.15))
                            : (searchClearMa.containsMouse
                               ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                               : Qt.rgba(0, 0, 0, 0))

                        SvgIcon {
                            anchors.centerIn: parent
                            name: "x"
                            tint: root.th.sub
                            px: 12
                        }

                        MouseArea {
                            id: searchClearMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                songSearchInput.text = ""
                                root.searchQuery = ""
                                root.searchPhase = 0
                                songSearchInput.forceActiveFocus()
                            }
                        }
                    }
                }

                // 防抖：输入停 400ms 后按本地占位目录出结果（组件6 换云端请求）
                Timer {
                    id: searchDebounce
                    interval: 400
                    onTriggered: {
                        if (root.searchQuery.trim().length === 0) root.searchPhase = 0
                        else root.searchPhase = (root.searchResults.length > 0) ? 2 : 3
                    }
                }

                // ── 内容区（高度自适应 = searchH - 34 - 6；五态互斥）────
                Item {
                    id: searchBody
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: searchBar.bottom
                    anchors.topMargin: 6
                    height: Math.max(60, root.searchH - 40)

                    // 态0 空态：热门占位（组件6 接云端热榜；点击=直接点歌）
                    Item {
                        anchors.fill: parent
                        visible: root.searchPhase === 0
                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 2
                            Text {
                                text: "热门歌曲"
                                color: root.th.sub
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: 3
                                delegate: Rectangle {
                                    width: searchBody.width
                                    height: 30
                                    radius: 8
                                    color: hotMa.containsMouse
                                        ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                                        : Qt.rgba(0, 0, 0, 0)
                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        Text {
                                            text: index + 1
                                            color: root.th.sub
                                            font.pixelSize: 11
                                            width: 10
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root.searchCatalog[index].name + " - " + root.searchCatalog[index].artist
                                            color: root.th.fg
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            width: searchBody.width - 110
                                        }
                                    }
                                    Text {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "占位"
                                        color: root.th.sub
                                        font.pixelSize: 10
                                    }
                                    MouseArea {
                                        id: hotMa
                                        objectName: "abtn_hotrow"   // 组件6：探针按名直达（热门=直接点歌上报）
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            // 组件6：热门=直接点歌（与结果行同口径：
                                            // 上报云端 + 计配额；本地反馈即时，上报 fire-and-forget）
                                            var s = root.searchCatalog[index]
                                            if (root.bridgeRef)
                                                root.bridgeRef.submitSongRequest(s.name, s.artist, "hot")
                                            root.searchQuotaUsed += 1
                                            if (root.searchQuotaFull) root.searchPhase = 4
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 态1 骨架（灰条占位，等"搜索"返回）
                    Item {
                        anchors.fill: parent
                        visible: root.searchPhase === 1
                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 10
                            Repeater {
                                model: 3
                                delegate: Rectangle {
                                    width: searchBody.width * (index === 1 ? 0.85 : 0.7)
                                    height: 14
                                    radius: 7
                                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.12)
                                }
                            }
                            Rectangle {
                                width: searchBody.width * 0.4
                                height: 10
                                radius: 5
                                color: root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.08)
                            }
                        }
                    }

                    // 态2 结果（行内「点歌」小按钮；配额满置灰禁点）
                    Item {
                        anchors.fill: parent
                        visible: root.searchPhase === 2
                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 4
                            Text {
                                text: "找到 " + root.searchResults.length + " 首"
                                color: root.th.sub
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: root.searchResults
                                delegate: Rectangle {
                                    width: searchBody.width
                                    height: 30
                                    radius: 8
                                    color: resMa.containsMouse
                                        ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                                        : Qt.rgba(0, 0, 0, 0)

                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        SvgIcon {
                                            name: "music"
                                            tint: root.th.sub
                                            px: 13
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name + " - " + modelData.artist
                                            color: root.th.fg
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            width: searchBody.width - 132
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.dur
                                            color: root.th.sub
                                            font.pixelSize: 10
                                        }
                                    }

                                    // 行内「点歌」小按钮（自绘，禁用的用 Item.enabled
                                    // 传导 + 透明度置灰，与 danmakuSend 同范式）
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 44; height: 22; radius: 11
                                        opacity: root.searchQuotaFull ? 0.4 : 1
                                        color: reqMa.pressed
                                            ? Qt.darker(root.th.accent, 1.15)
                                            : (reqMa.containsMouse ? Qt.lighter(root.th.accent, 1.1) : root.th.accent)

                                        Text {
                                            anchors.centerIn: parent
                                            text: "点歌"
                                            color: root.th.light ? "#FFFFFF" : "#161616"
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }

                                        MouseArea {
                                            id: reqMa
                                            objectName: "abtn_songrow"   // 组件6：探针按名直达（点歌上报）
                                            anchors.fill: parent
                                            hoverEnabled: !root.searchQuotaFull
                                            enabled: !root.searchQuotaFull
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                // 组件6：上报云端 /api/song_requests（fire-and-forget，
                                                // 成败不影响本地配额反馈；桥未注入时静默跳过）
                                                if (root.bridgeRef)
                                                    root.bridgeRef.submitSongRequest(modelData.name, modelData.artist, "search")
                                                root.searchQuotaUsed += 1
                                                if (root.searchQuotaFull) root.searchPhase = 4
                                            }
                                        }
                                    }

                                    // 行悬停高亮（z:-1 垫底，不抢按钮事件）
                                    MouseArea {
                                        id: resMa
                                        anchors.fill: parent
                                        z: -1
                                        hoverEnabled: true
                                    }
                                }
                            }
                        }
                    }

                    // 态3 无结果
                    Item {
                        anchors.fill: parent
                        visible: root.searchPhase === 3
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            SvgIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "search"
                                tint: root.th.sub
                                px: 26
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "没有找到相关歌曲"
                                color: root.th.fg
                                font.pixelSize: 12
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "换个关键词试试"
                                color: root.th.sub
                                font.pixelSize: 11
                            }
                        }
                    }

                    // 态4 超额禁用（配额满：只告知 + 返回，不给提交入口）
                    Item {
                        anchors.fill: parent
                        visible: root.searchPhase === 4
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            SvgIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "ban"
                                tint: root.th.accent
                                px: 26
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "点歌已达上限（" + root.searchQuotaMax + " 首）"
                                color: root.th.fg
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "明天再来，或先听大家点的歌"
                                color: root.th.sub
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // ── 组件3：投稿页（2026-10-15）──────────────────────
            // 入口：danmakuRow 输入 pill / 发送 pill ⇒ 原地变形（同 searchPage
            // 范式：expandedCol 内、锚 mediaRow.bottom、rightMargin 让位鲸鱼、
            // 高度自适应 submitH；interactRow/danmakuRow 同步让位）。
            // 四态（submitPhase，互斥）：0 编辑（compose box + 50 字计数 +
            // 超限禁用）/ 1 提交中 / 2 成功(✓) / 3 失败(✗ 重试)。
            // 组件5（2026-10-15）：提交接**真实发送**（doSubmit →
            // bridgeRef.sendBlessing → DanmakuWidget）；成功路径保留 400ms
            // Timer 作云端 ACK 延迟模拟（组件6 删），失败路径由真实结果码驱动。
            Item {
                id: submitPage
                visible: root.submitPageOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: root.padH + 40
                anchors.top: mediaRow.bottom
                anchors.topMargin: visible ? 12 : 0
                height: visible ? root.submitH : 0

                // ── 态0 编辑：compose box + 计数行 + 返回/投稿 ──
                Item {
                    anchors.fill: parent
                    visible: root.submitPhase === 0

                    Column {
                        anchors.fill: parent
                        spacing: 8

                        // compose box（自绘 Rectangle + TextInput，铁律不用 TextField；
                        // 单行 + clip：50 字在 66px 盒内水平滚动，不撑高预算）
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 66
                            radius: 12
                            color: root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10)
                            border.width: 1
                            border.color: submitInput.activeFocus
                                ? root.th.accent
                                : (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14))

                            TextInput {
                                id: submitInput
                                objectName: "submitInput"   // 组件3：探针按名注入草稿
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                color: root.th.fg
                                font.pixelSize: 12
                                selectionColor: root.th.accent
                                clip: true
                                // 双向绑定：开页携入的 submitText 显示在此，用户编辑
                                // 经 onTextEdited 回流（程序性 text 变化不触发
                                // onTextEdited ⇒ 不成环）。
                                // 50 字上限不硬截断（maximumLength 会使超限态不可达）。
                                text: root.submitText
                                onTextEdited: root.submitText = text

                                // 占位（TextInput 无原生 placeholder，同 danmakuInput 范式）
                                Text {
                                    anchors.fill: parent
                                    verticalAlignment: Text.AlignVCenter
                                    text: "写下你的祝福…"
                                    color: root.th.sub
                                    font.pixelSize: 12
                                    visible: submitInput.text.length === 0 && !submitInput.activeFocus
                                }
                            }
                        }

                        // 计数行：N/50；超限 ⇒ 红计数 + 「已超出 N 字」；
                        // 组件5：30s 冷却中 ⇒ 「Ns 后可重发」（投稿钮同步禁用）
                        Row {
                            anchors.left: parent.left
                            spacing: 8

                            Text {
                                text: root.submitLen + "/" + root.submitMax
                                color: root.submitOver ? root.th.severityUrgent : root.th.sub
                                font.pixelSize: 11
                            }
                            Text {
                                visible: root.submitOver
                                text: "已超出 " + (root.submitLen - root.submitMax) + " 字"
                                color: root.th.severityUrgent
                                font.pixelSize: 11
                            }
                            Text {
                                visible: root._blessingCooldownSec > 0
                                text: root._blessingCooldownSec + "s 后可重发"
                                color: root.th.sub
                                font.pixelSize: 11
                            }
                        }

                        // 按钮行：返回（草稿写回弹幕行）+ 投稿（空/超限禁用）
                        Row {
                            anchors.right: parent.right
                            spacing: 8

                            ActionButton {
                                theme: root.th
                                objectName: "abtn_backEdit"
                                label: "返回"
                                role: "secondary"
                                onClicked: {
                                    // 草稿双向同步：页内文字写回弹幕输入行再收起
                                    danmakuInput.text = root.submitText
                                    root.submitOn = false
                                }
                            }
                            ActionButton {
                                theme: root.th
                                objectName: "abtn_submit"
                                label: "投稿"
                                role: "primary"
                                // 空内容 / 超 50 字 / 30s 冷却中 ⇒ 禁用
                                // （Item.enabled 传导置灰；冷却中点到它由
                                // gestureArea 的 hitDisabledControl 吞掉，
                                // 不会穿透把岛收起）
                                enabled: !root.submitEmpty && !root.submitOver
                                         && root._blessingCooldownSec === 0
                                onClicked: root.doSubmit()
                            }
                        }
                    }
                }

                // ── 态1 提交中 ──
                Item {
                    anchors.fill: parent
                    visible: root.submitPhase === 1
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        SvgIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: "send"
                            tint: root.th.accent
                            px: 26
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "正在提交…"
                            color: root.th.fg
                            font.pixelSize: 12
                        }
                    }
                }

                // ── 态2 成功（✓ 绿；severityRemind 令牌，不造新色）──
                Item {
                    anchors.fill: parent
                    visible: root.submitPhase === 2
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        SvgIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: "circle-check"
                            tint: root.th.severityRemind
                            px: 26
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "投稿成功"
                            color: root.th.fg
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "弹幕将在播放中飘过"
                            color: root.th.sub
                            font.pixelSize: 11
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 8
                            topPadding: 4
                            ActionButton {
                                theme: root.th
                                objectName: "abtn_rewrite"
                                label: "再写一条"
                                role: "secondary"
                                onClicked: {
                                    // 草稿已消费：只清页内编辑区（输入行文字由
                                    // 「返回」写回或「完成」清空时覆盖，不留残留）
                                    root.submitText = ""
                                    root.submitPhase = 0
                                }
                            }
                            ActionButton {
                                theme: root.th
                                objectName: "abtn_done"
                                label: "完成"
                                role: "primary"
                                onClicked: {
                                    // 清空两处草稿 + 收起，回互动区
                                    danmakuInput.text = ""
                                    root.submitText = ""
                                    root.submitOn = false
                                }
                            }
                        }
                    }
                }

                // ── 态3 失败（✗ 红；severityUrgent 令牌）──
                Item {
                    anchors.fill: parent
                    visible: root.submitPhase === 3
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        SvgIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: "x"
                            tint: root.th.severityUrgent
                            px: 26
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "投稿失败"
                            color: root.th.fg
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            // 组件5：失败文案按真实结果码动态（旧 simfail 只有一种）
                            text: {
                                switch (root.submitFailCode) {
                                case 1: return "内容包含敏感词，请修改后再试"
                                case 2: return "发送太频繁，" + root._blessingCooldownSec + "s 后可重试"
                                case 3: return "内容为空，写点什么再发送"
                                default: return "网络开了小差，稍后再试"
                                }
                            }
                            color: root.th.sub
                            font.pixelSize: 11
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 8
                            topPadding: 4
                            ActionButton {
                                theme: root.th
                                objectName: "abtn_backFail"
                                label: "返回修改"
                                role: "secondary"
                                onClicked: {
                                    // 文字保留回编辑态，用户可改后重投
                                    root.submitPhase = 0
                                }
                            }
                            ActionButton {
                                theme: root.th
                                objectName: "abtn_retry"
                                label: "重试"
                                role: "primary"
                                // 与投稿钮同一条真实发送链路（doSubmit）：
                                // 冷却没结束就重试 ⇒ 拿码 2 回落态3，不是假成功
                                onClicked: root.doSubmit()
                            }
                        }
                    }
                }

                // 提交模拟：真实发送成功后 400ms 出成功态（云端 ACK 延迟模拟）。
                // 失败路径已由 sendBlessing 真实结果码驱动（态3），不需要开关。
                // 组件6 接云端投稿端点后，由 ACK 回调直接改 submitPhase，本 Timer 删除。
                Timer {
                    id: submitDebounce
                    interval: 400
                    onTriggered: root.submitPhase = 2
                }
            }
            // ── 多界面切换器（#330）：展开态右上竖排面签 ──────────────
            // 位置吃标题右缘的 48px 让位带（exTitle.rightMargin: 48），不压标题；
            // 每个面签 22×16 单字（概/媒/史/态）—— 教室机 emoji/符号会 tofu，
            // 面签用汉字是既有纪律（底导航图标同款）。点击切面，当前面高亮。
            Column {
                id: faceTabs
                visible: root.facesOn
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 2
                spacing: 4

                Repeater {
                    model: root.faceModel
                    delegate: Rectangle {
                        required property var modelData
                        // ── 2026-10-18 · MD3 适配（用户裁决：仅控件/元素，透明度尽量不变）──
                        // 面签是岛内最小的交互控件，按 MD3 规范对齐：
                        //   形状：radius 5（非标）→ 网格对齐，配合 MD3 extraSmall(4) 档的观感
                        //   状态层：hover/press 叠加 onSurface 色（MD3 state layer 口径 0.08/0.12）
                        //   文字：10px 在 MD3 labelSmall(11) 之下，提到 11 保证教室机可读
                        //   反馈：压下轻微收缩（90ms，跟手），选中态加轻微"亮起"过渡
                        // ⚠️ 底色透明度**沿用原值**（浅 0.07 / 深 0.10），不按 MD3 的 0.12 改。
                        id: faceTab
                        width: 22
                        height: 16
                        // 形状：5 → 4（对齐 MD3 extraSmall 档；22×16 的小块上 4 与 5 观感差极小，
                        //       但值落在规范档位上，日后统一迁移不再需要"这个 5 是哪来的"）
                        radius: 4
                        property bool hov: tabMa.containsMouse
                        property bool down: tabMa.pressed
                        color: root.faceNow === modelData.id
                               ? root.th.accent
                               : (root.th.light ? Qt.rgba(0, 0, 0, 0.07) : Qt.rgba(1, 1, 1, 0.10))
                        // MD3 状态层：hover 8% / press 12%（叠在容器色之上，不改容器本身的透明度）
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: root.th.light ? "#161616" : "#FAFAFA"
                            opacity: faceTab.down ? 0.12 : (faceTab.hov ? 0.08 : 0)
                            Behavior on opacity {
                                NumberAnimation { duration: root.th ? root.th.durXShort : 120 }
                            }
                        }
                        scale: faceTab.down ? 0.94 : 1.0
                        Behavior on scale {
                            NumberAnimation { duration: root.th ? root.th.durInstant : 90; easing.type: Easing.OutQuad }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            // 10 → 11（MD3 labelSmall 下限，教室机可读性）
                            font.pixelSize: 11
                            color: root.faceNow === modelData.id
                                   ? (root.th.light ? "#FFFFFF" : "#101014")
                                   : root.th.sub
                        }
                        MouseArea {
                            id: tabMa
                            anchors.fill: parent
                            hoverEnabled: true
                            // ⚠️ 2026-10-18：用户主动选面 ⇒ 记录 faceUserPick。
                            //    否则 faceNow 无法区分"系统回落"和"用户切换"，
                            //    媒体/历史恢复时会违背用户意图自动跳回（见上面注释）。
                            onClicked: { root.face = modelData.id; root.faceUserPick = modelData.id; }
                        }
                    }
                }
            }

            // ── 多界面面板（#330）：非概览面满盖内容区 ────────────────
            // 与 expandedCol 内的行**同父级**（跨父级锚会被 QML 静默丢弃，见
            // mediaRow 的 2026-10-14 注释）。盖住堆叠内容与历史面板（z 60 > 40），
            // 低于鲸鱼（z 100）。面板自带不透明底：面切换是"换一屏"，不是叠加。
            Item {
                id: facePanel
                visible: root.facePanelOn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: root.headH
                anchors.bottom: parent.bottom
                z: 60

                Rectangle {
                    anchors.fill: parent
                    radius: 12
                    color: root.th.bg
                    border.width: 1
                    border.color: root.th.light ? "#E2E2E2" : "#333333"
                }

                // ── 面 1：媒体（本机播放优先，其次 VoiceHub 远程广播）──
                Item {
                    anchors.fill: parent
                    anchors.margins: 10
                    visible: root.faceNow === 1

                    SvgIcon {
                        id: faceMediaIcon
                        name: "music"
                        tint: root.th.accent
                        px: 18
                        anchors.left: parent.left
                        anchors.top: parent.top
                    }
                    // 来源标签：让人一眼分清"本机在播"和"VoiceHub 远程广播"
                    Text {
                        anchors.left: faceMediaIcon.right
                        anchors.leftMargin: 6
                        anchors.verticalCenter: faceMediaIcon.verticalCenter
                        text: root.faceUseLocal ? "本机播放" : "VoiceHub"
                        color: root.th.sub
                        font.pixelSize: 10
                    }
                    Text {
                        id: faceMediaTitle
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: faceMediaIcon.bottom
                        anchors.topMargin: 6
                        text: root.faceTitle || "正在播放"
                        color: root.th.fg
                        font.pixelSize: 19
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        id: faceMediaArtist
                        visible: root.faceArtist !== ""
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: faceMediaTitle.bottom
                        anchors.topMargin: 2
                        text: root.faceArtist
                        color: root.th.sub
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    // 进度条（本地外推值；轨道 4px + 填充按 position/duration）
                    Rectangle {
                        id: faceBar
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: faceTime.top
                        anchors.bottomMargin: 4
                        height: 4
                        radius: 2
                        color: root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)
                        Rectangle {
                            width: root.faceDur > 0
                                   ? Math.max(0, Math.min(1, root.facePos / root.faceDur)) * parent.width
                                   : 0
                            height: parent.height
                            radius: 2
                            color: root.th.accent
                        }
                    }
                    Text {
                        id: faceTime
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: faceNextTxt.visible ? faceNextTxt.top : parent.bottom
                        anchors.bottomMargin: faceNextTxt.visible ? 2 : 0
                        text: root.faceTimeText()
                        color: root.th.sub
                        font.pixelSize: 11
                    }
                    Text {
                        id: faceNextTxt
                        visible: root.faceNext !== ""
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: 44   // 让位鲸鱼（右下 36px + 余量）
                        anchors.bottom: parent.bottom
                        text: "接下来：" + root.faceNext
                        color: root.th.sub
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // ── 面 2：历史（紧凑预览，最多 4 条 + 余量提示）────────────
                // 2026-10-27 抽离为 IslandHistoryFace（统一布局规范 Phase 1）：
                // 复用 IslandListRow，消除"各面手搓 48px 行"的漂移；右侧让位鲸鱼 44px。
                IslandHistoryFace {
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.rightMargin: 50   // 10 基础 + 40 余量给右下角鲸鱼（z:100）
                    visible: root.faceNow === 2
                    th: root.th
                    history: root._history
                    bridgeRef: root.bridgeRef
                }

                // ── 面 3：状态（桥/模块已给的展示态，不新造数据源）──────
                Item {
                    anchors.fill: parent
                    anchors.margins: 10
                    visible: root.faceNow === 3

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 8
                        Repeater {
                            model: root.faceStatusRows
                            delegate: Item {
                                required property var modelData
                                width: parent.width
                                height: 22
                                Text {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.k
                                    color: root.th.sub
                                    font.pixelSize: 12
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.v
                                    color: root.th.fg
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                // ── 面 4：批注 / 白板（2026-10-27 · H 批）──────────────
                // 用户裁决：这类"老师当场要用"的能力放被控端，且长在岛上。
                // 本面只做显示 + 转发，真状态在 AnnotateDomain 里（见 IslandAnnotateFace 头注）。
                Item {
                    anchors.fill: parent
                    anchors.margins: 10
                    visible: root.faceNow === 4

                    IslandAnnotateFace {
                        anchors.fill: parent
                        th: root.th
                        bridge: root.bridgeRef
                    }
                }

                // ── 面 5：设置（2026-10-27 · C 批）──────────────────
                // 用户裁决：音量/黑名单这类"老师当场要调"的能力放被控端，且长在岛上。
                // 本面只做显示 + 拖动跟手 + 转发，真值在 C++ 域 / QSettings（见 IslandSettingsFace 头注）。
                Item {
                    anchors.fill: parent
                    // 比其它面多留 4px 内边距：本面有贴边滑块，要避开 IslandSurface 的圆角裁剪边。
                    anchors.margins: 14
                    visible: root.faceNow === 5

                    IslandSettingsFace {
                        anchors.fill: parent
                        th: root.th
                        bridge: root.bridgeRef
                    }
                }

                // ── 面 6：自动化规则（2026-10-27 · Phase 3）────────────
                // 用户裁决：自动化是被控端能力、主入口在岛；老师在讲台上当场配规则。
                Item {
                    anchors.fill: parent
                    anchors.margins: 10
                    visible: root.faceNow === 6

                    IslandAutomationFace {
                        anchors.fill: parent
                        th: root.th
                        automationRef: root.automationRef
                        bridgeRef: root.bridgeRef
                    }
                }
            }
        }
    }
    // ══════════════════════════════════════════════════════════
    // 动作按钮 —— 已抽为 island/IslandActionRow.qml（D 档拆分·第二块）
    // ══════════════════════════════════════════════════════════
    // 根级的行**只锚 parent**（范式见 D 档归档：展开区内的行才做同父级兄弟互锚）。
    // ⚠️ 锚定刻意留在实例化处，让这件事在 IslandRoot 里一眼可见。
    IslandActionRow {
        id: actionRow
        anchors.left: parent.left
        anchors.leftMargin: root.padH
        anchors.right: parent.right
        anchors.rightMargin: root.padH + 40   // 给鲸鱼让位
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        th: root.th
        // 大卡片（Centered/Fullscreen）由自己的面画按钮 ⇒ 根级动作行让位，
        // 否则同一组按钮会在卡片里 + 窗口底部各画一份。
        showActions: root._showActionsNow && !root._bigForm
        actions: root._actions
        bridgeRef: root.bridgeRef
    }

    // ══════════════════════════════════════════════════════════
    // 历史通知面板（展开态下滑出，独立容器 + 视觉隔离）
    // ══════════════════════════════════════════════════════════
    // 2026-10-11 修观感问题：
    //   用户反馈"历史通知没有专门隔离"——原来只是裸 ListView 浮在同一背景上，
    //   跟当前通知没有任何分界。现在做**独立容器**：
    //   · 独立卡片底色（比 shell 背景略深/浅，有区分度）
    //   · 标题头"历史通知" + 条数 + 清空按钮（按钮形态，不是裸文字）
    //   · 分割线把标题区与列表区隔开
    //   · 每条历史有独立行底色（hover 加深）
    //   · 面板从标题/描述区**下方**开始，不覆盖标题
    Item {
        id: historyPanel
        visible: root._expanded && root._history.length > 0 && !root._showActionsNow
                 && !root._spectrumActive   // 2026-10-13：对讲频谱态不显示历史（见 histH 注释）
                 && !root.mediaIsContent    // 2026-10-15 组件1：媒体占据岛面时让位互动区/弹幕行
                 // #330：多界面的面面板打开时整个让位 —— z 只在**同一父级**内比较，
                 // 本面板是根级、面面板在 expandedCol 里 ⇒ 不隐藏就会盖在面面板上
                 // （2026-10-17 截图实证：面 2 的历史行与旧历史面板交错重叠）。
                 && !root.facePanelOn
                 // 2026-10-27（G4/G5/G6）：组件面板打开时同样让位 —— 生产实测
                 //   （prod-panel-date 截图）面板内容与历史列表叠在一起：历史面板是
                 //   **根级** Item，藏在 expandedCol 里的"活动态让位"管不到它。
                 && !root._panelOn
        anchors.left: parent.left
        anchors.leftMargin: root.padH
        anchors.right: parent.right
        anchors.rightMargin: root.padH + 44   // 让位右下角鲸鱼（z:100，~44px 占右下），避免压住列表
        anchors.top: parent.top
        // 2026-10-12 修「历史显示不全」：原写法 headH + 36，但 implicitHeight 预算里
        // 顶部只算了 padH ⇒ 那 36 属于"预算外占位"，把历史面板整体往下顶、越顶越矮。
        // 2026-10-14 补 specH + mediaH：频谱/岛底播放栏开启时历史面板也要让位，
        // 否则与「正在播放」栏重叠（implicitHeight 预算公式里本来就有这两项，
        // 这里不同步就会"预算长了、面板没动"）。
        // 2026-10-15 组件1 补 interactH + danmakuH、组件2 补 searchH、组件3 补
        // submitH：同一套公式（虽然媒体态下本面板整体不可见，公式仍保持同步，
        // 防将来条件变动时错位）。
        // 2026-10-17 补 vhH：VoiceHub 远程广播行同款让位（#329）。
        // 现在与预算同一套公式：从内容区（标题+描述+状态列表+进度条+频谱+播放栏+互动区+弹幕行+搜索页+投稿页）之下开始。
        anchors.topMargin: root.padH + root.headH + root.listH + root.ftH + root.specH + root.mediaH + root.vhH + root.interactH + root.danmakuH + root.searchH + root.submitH
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.padH
        z: 40

        // 独立容器：比 shell 略深/浅一档，有圆角
        IslandCard {
            id: histContainer
            anchors.fill: parent
            th: root.th
            pad: 0
            radius: root.th.rCard
            clipContent: true

            // ── 标题头 ──────────────────────────────
            // ── 标题头（2026-10-27 · 统一布局规范：IslandFaceHeader）──────
            IslandFaceHeader {
                id: histHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                th: root.th
                title: "历史通知"
                showDot: root._unread > 0
                subtitle: root._unread > 0
                          ? ("(" + root._history.length + ") · " + root._unread + " 未读")
                          : ("(" + root._history.length + ")")

                // 工具按钮组：筛选 / 已读 / 清空（走 header 尾部操作槽）
                Row {
                    // 走 header 的 trailing 槽（trailArea 已钉在右缘、零宽），
                    // 必须自钉右缘，否则会落在 trailArea 原点（右缘外侧）被裁掉。
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    spacing: 5

                    Repeater {
                        model: [
                            { t: "筛选", act: "filter" },
                            { t: "已读", act: "read"   },
                            { t: "清空", act: "clear"  }
                        ]
                        delegate: Rectangle {
                            width: btnTxt.implicitWidth + 14
                            height: 20
                            radius: height / 2
                            visible: modelData.act !== "read" || root._unread > 0
                            color: (modelData.act === "filter" && root.histFilterOn)
                                   ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.16))
                                   : (btnMa.pressed
                                      ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.15))
                                      : (btnMa.containsMouse
                                         ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                                         : Qt.rgba(0, 0, 0, 0)))
                            Text {
                                id: btnTxt
                                anchors.centerIn: parent
                                text: modelData.t
                                color: (modelData.act === "filter" && root.histFilterOn)
                                       ? root.th.fg : root.th.sub
                                font.pixelSize: 10
                            }
                            MouseArea {
                                id: btnMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!root.bridgeRef) return
                                    if (modelData.act === "filter") {
                                        root.histFilterOn = !root.histFilterOn
                                        if (!root.histFilterOn) {
                                            if (root.bridgeRef.setHistoryCategoryFilter)
                                                root.bridgeRef.setHistoryCategoryFilter("")
                                            if (root.bridgeRef.setHistoryKeyword)
                                                root.bridgeRef.setHistoryKeyword("")
                                        }
                                    } else if (modelData.act === "read") {
                                        if (root.bridgeRef.markAllHistoryRead)
                                            root.bridgeRef.markAllHistoryRead()
                                    } else {
                                        if (root.bridgeRef.clearHistory)
                                            root.bridgeRef.clearHistory()
                                        histToast.show("已清空历史")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 分割线（独立，不在 header 组件内）
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 31
                height: 1
                color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)
            }

            // ── 筛选行（2026-10-17，默认收起）──────────────────────
            // 左：搜索框（TextInput，被控端没有 QtQuick Controls，不能用 TextField）
            // 右：分类 chip（全部/指令/告警/消息/系统）
            Item {
                id: histFilterRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: histHeader.bottom
                height: root.histFilterOn ? 30 : 0   // 收起时**占用归零**（锚链对不可见 item 仍起效）
                visible: root.histFilterOn
                clip: true

                // 搜索框
                // ⚠️ 宽度是算过的：面板宽 304（展开态 332 − 两侧 padH），
                //    搜索框 92 + 左右 margin 16 = 108，剩 196；
                //    5 个 chip × (2 字 20 + padding 10) = 150，加 4 段 spacing 16 = 166 < 196，
                //    不会把最后那个"系统"挤出裁剪区（第一版 118 宽 + padding 14 会溢出 20px）。
                Rectangle {
                    id: histSearchBox
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 92
                    height: 22
                    radius: 11
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.07)
                    border.width: 1
                    border.color: histInput.activeFocus
                                  ? root.th.accent
                                  : (root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10))

                    TextInput {
                        id: histInput
                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.th.fg
                        font.pixelSize: 11
                        selectByMouse: true
                        clip: true
                        // 输入即筛（C++ 侧过滤，见 island_bridge.h historyKeyword 注释）
                        onTextChanged: {
                            if (root.bridgeRef && root.bridgeRef.setHistoryKeyword)
                                root.bridgeRef.setHistoryKeyword(text)
                        }
                        Text {
                            // placeholder：不用 anchors.fill（会和 verticalCenter 冲突告警），
                            // 改成与输入框同左同右、垂直居中即可。
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: histInput.text.length === 0 && !histInput.activeFocus
                            text: "搜索内容"
                            color: root.th.sub
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }

                // 分类 chip 行
                Row {
                    anchors.left: histSearchBox.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Repeater {
                        model: root._histCats
                        delegate: Rectangle {
                            width: catTxt.implicitWidth + 10
                            height: 20
                            radius: height / 2
                            // ⚠️ 属性名不能叫 `on` —— QML 关键字（on<Signal> 语法），
                            //    用了就是整文件解析失败（岛直接透明）。
                            readonly property bool selOn: {
                                const f = root.bridgeRef ? root.bridgeRef.historyCategoryFilter : ""
                                return (modelData === "全部") ? (f === "" || f === "全部")
                                                              : (f === modelData)
                            }
                            color: selOn
                                   ? (root.th.light ? Qt.rgba(0, 0, 0, 0.12) : Qt.rgba(1, 1, 1, 0.18))
                                   : (catMa.containsMouse
                                      ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                                      : Qt.rgba(0, 0, 0, 0))

                            Text {
                                id: catTxt
                                anchors.centerIn: parent
                                text: modelData
                                color: parent.selOn ? root.th.fg : root.th.sub
                                font.pixelSize: 10
                            }

                            MouseArea {
                                id: catMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.bridgeRef && root.bridgeRef.setHistoryCategoryFilter)
                                        root.bridgeRef.setHistoryCategoryFilter(modelData === "全部" ? "" : modelData)
                                }
                            }
                        }
                    }
                }
            }

            // ── 列表区 ──────────────────────────────
            ListView {
                id: historyList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: histFilterRow.bottom
                anchors.bottom: parent.bottom
                anchors.margins: 2
                clip: true
                model: root._history
                spacing: 2

                // 2026-10-17：按"今天/昨天/M月d日"分段（分组串在 C++ 归档时就算好，
                // 不让 QML 现算日期 —— 见 island.h HistoryItem 注释）。
                section.property: "dateGroup"
                section.delegate: IslandSectionLabel {
                    width: historyList.width
                    th: root.th
                    text: section
                }

                delegate: IslandListRow {
                    width: historyList.width
                    th: root.th
                    icon: modelData.icon || ""
                    title: modelData.title || ""
                    desc: modelData.desc || ""
                    time: modelData.timeStr || ""
                    category: modelData.category || ""
                    level: modelData.level || ""
                    unread: modelData.read !== true
                    onClicked: {
                        if (!root.bridgeRef) return
                        if (root.bridgeRef.showHistoryById && modelData.id)
                            root.bridgeRef.showHistoryById(modelData.id)
                        else if (root.bridgeRef.showHistoryItem)
                            root.bridgeRef.showHistoryItem(index)
                    }
                }
            }

            // 清空/已读反馈（取代此前"清完无反应"）—— MD3 Snackbar 风，跨深浅可读
            IslandToast {
                id: histToast
                th: root.th
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
            }
        }
    }
}
