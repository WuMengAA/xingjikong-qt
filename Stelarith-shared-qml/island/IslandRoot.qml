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
    readonly property var    _states:     (bridgeRef && bridgeRef.states)  ? bridgeRef.states  : []
    readonly property var    _actions:    (bridgeRef && bridgeRef.actions) ? bridgeRef.actions : []
    readonly property var    _history:    (bridgeRef && bridgeRef.history) ? bridgeRef.history : []

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
    readonly property int padH: 14
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
                                  && !root._spectrumActive && !root.mediaIsContent)
        ? Math.min(root.histVisibleMax, root._history.length) * 48 + 32
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
        ? Math.max(100, 320 - (root.padH * 2 + root.headH + root.listH + root.mediaH + root.actH + 12))
        : 0
    // 组件3（2026-10-15）：投稿页占的额外高度。自适应公式与 searchH 同款
    // （展开态硬顶 320，投稿页吃剩余空间：320 - padH*2 - headH(标题已让位)
    // - listH(状态列表已让位) - mediaH - actH - 12 页面自身 topMargin）。
    // 编辑态内容（compose 66 + 计数 ~16 + 按钮 28 + 间距 16）≈126 < 预算，
    // 反馈态为居中 Column 自适应，均低于 searchH 同款预算。
    readonly property int submitH: root.submitPageOn
        ? Math.max(100, 320 - (root.padH * 2 + root.headH + root.listH + root.mediaH + root.actH + 12))
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
                               : (root._expanded ? 640 : 320)
    implicitHeight: root._idle ? root.th.shapeIdle
                               : (root._expanded
                                  ? Math.max(320, root.padH + root.headH + root.listH + root.histH + root.ftH + root.specH + root.mediaH + root.interactH + root.danmakuH + root.searchH + root.submitH + root.actH + root.padH)
                                  : (root._showActionsNow
                                     ? Math.max(root.collapsedH + root.actH + 10, 90)
                                     : root.collapsedH))
    // ⚠️ 不给 width/height 赋值：宿主 QQuickWidget 用 SizeRootObjectToView 时根随视图，
    //    显式写 width: implicitWidth 会被 view 覆盖，且与 Behavior 打架。

    Behavior on width  { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }
    Behavior on height { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }

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

        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on px       { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }
    }

    // ── 底 ──────────────────────────────────────────────────
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: root._idle ? height / 2 : (root._expanded ? 18 : height / 2)
        color: root._idle ? root.th.idleBg : root.th.bg
        border.width: root._idle ? 0 : 1
        border.color: root.th.light ? "#E2E2E2" : "#333333"

        Behavior on radius { NumberAnimation { duration: root.th.durShape } }
        Behavior on color  { ColorAnimation  { duration: 200 } }

        // 严重度：只做顶部细条。
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: root._severity === "" ? 0 : 3
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
    // 活动态 收起 / 展开
    // ══════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: !root._idle
        opacity: root._idle ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 160 } }

        // ── 收起态 ───────────────────────────────────────
        Item {
            id: collapsedRow
            visible: !root._expanded
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
                anchors.right: parent.right
                anchors.rightMargin: 42
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

            Text {
                visible: text !== ""
                anchors.left: csTitle.left
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
                anchors.right: parent.right
                anchors.rightMargin: 48
                anchors.top: parent.top
                anchors.topMargin: 2
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: root.exTitlePx
                font.weight: Font.Bold
                minimumPixelSize: root.th.fsTitleXs
                fontSizeMode: Text.Fit
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
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
                        Behavior on width { NumberAnimation { duration: 120 } }
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
                anchors.top: mediaRow.bottom
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
                        to: -28; duration: 600; easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        target: likeFly; property: "opacity"
                        to: 0.0; duration: 600
                    }
                    onFinished: likeFly.visible = false
                }
                // 组件4：计数回弹（150ms = 75 放大 + 75 缩回；详规口径）
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
        showActions: root._showActionsNow
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
        anchors.left: parent.left
        anchors.leftMargin: root.padH
        anchors.right: parent.right
        anchors.rightMargin: root.padH
        anchors.top: parent.top
        // 2026-10-12 修「历史显示不全」：原写法 headH + 36，但 implicitHeight 预算里
        // 顶部只算了 padH ⇒ 那 36 属于"预算外占位"，把历史面板整体往下顶、越顶越矮。
        // 2026-10-14 补 specH + mediaH：频谱/岛底播放栏开启时历史面板也要让位，
        // 否则与「正在播放」栏重叠（implicitHeight 预算公式里本来就有这两项，
        // 这里不同步就会"预算长了、面板没动"）。
        // 2026-10-15 组件1 补 interactH + danmakuH、组件2 补 searchH、组件3 补
        // submitH：同一套公式（虽然媒体态下本面板整体不可见，公式仍保持同步，
        // 防将来条件变动时错位）。
        // 现在与预算同一套公式：从内容区（标题+描述+状态列表+进度条+频谱+播放栏+互动区+弹幕行+搜索页+投稿页）之下开始。
        anchors.topMargin: root.padH + root.headH + root.listH + root.ftH + root.specH + root.mediaH + root.interactH + root.danmakuH + root.searchH + root.submitH
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.padH
        z: 40

        // 独立容器：比 shell 略深/浅一档，有圆角
        Rectangle {
            id: histContainer
            anchors.fill: parent
            radius: 10
            clip: true
            color: root.th.light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
            border.color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)

            // ── 标题头 ──────────────────────────────
            Item {
                id: histHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 32

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "历史通知"
                    color: root.th.fg
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 68
                    anchors.verticalCenter: parent.verticalCenter
                    text: "(" + root._history.length + ")"
                    color: root.th.sub
                    font.pixelSize: 11
                }

                // 清空按钮（按钮形态）
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: clearTxt.implicitWidth + 16
                    height: 20
                    radius: height / 2
                    color: clearMa.pressed
                        ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.15))
                        : (clearMa.containsMouse
                           ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                           : Qt.rgba(0, 0, 0, 0))

                    Text {
                        id: clearTxt
                        anchors.centerIn: parent
                        text: "清空"
                        color: root.th.sub
                        font.pixelSize: 10
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.bridgeRef && root.bridgeRef.clearHistory)
                                root.bridgeRef.clearHistory()
                        }
                    }
                }

                // 分割线
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)
                }
            }

            // ── 列表区 ──────────────────────────────
            ListView {
                id: historyList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: histHeader.bottom
                anchors.bottom: parent.bottom
                anchors.margins: 2
                clip: true
                model: root._history
                spacing: 2

                delegate: Item {
                    width: historyList.width
                    // 2026-10-12：行高 40→48（配描述 2 行 + 字号放大），与 histH 的 48/条一致
                    height: 48

                    // 每条独立行底色
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 6
                        color: maHov.containsMouse
                            ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                            : (root.th.light ? Qt.rgba(0, 0, 0, 0.02) : Qt.rgba(1, 1, 1, 0.04))
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    SvgIcon {
                        id: hIcon
                        name: modelData.icon || ""
                        tint: root.th.sub
                        px: 15
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        id: hTitle
                        anchors.left: hIcon.right
                        anchors.leftMargin: 6
                        anchors.right: hTime.left
                        anchors.rightMargin: 6
                        anchors.top: parent.top
                        anchors.topMargin: 5
                        text: modelData.title || ""
                        color: root.th.fg
                        // 2026-10-12「字太小」：标题 12→13
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.left: hTitle.left
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.top: hTitle.bottom
                        text: modelData.desc || ""
                        color: root.th.sub
                        // 2026-10-12「字数太少」：描述 10→12，并允许 2 行（原来一行就 … 截掉）
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Text {
                        id: hTime
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.top: parent.top
                        anchors.topMargin: 5
                        text: modelData.timeStr || ""
                        color: root.th.sub
                        // 2026-10-12：时间 9→11
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: maHov
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.bridgeRef && root.bridgeRef.showHistoryItem)
                                root.bridgeRef.showHistoryItem(index)
                        }
                    }
                }
            }
        }
    }
}
