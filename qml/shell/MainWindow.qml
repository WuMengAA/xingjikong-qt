// 星集控管理端 · **全局主框架（App Scaffold 之一：MainWindow）**
//
// 定位（2026-10-22 用户点单「全局统一」+ AGENTS.md §9 全局 UI 构建红线）：
//   本文件是管理端**唯一的应用外壳**——规范里三种全局骨架的第一种
//   **所有业务页面必须长在本骨架里**（经 StackLayout/Loader 挂载），
//   禁止任何页面自绘侧栏 / 顶栏 / 状态条，禁止新建第二个应用外壳。
//
//   横屏：左侧 NavigationDrawer 240（surfaceContainer 底）+ 右侧 64 顶栏 + 内容区
//        （边缘内边距 spL=24）+ 底部 32 状态条；上层 SideSheet（400 右滑）+ Toast。
//   竖屏（2026-10-22 用户点单「竖屏横屏两种布局模式」）：左栏 240 让位给底部
//        BottomNav 80（同一份 navItems 的横排形态），设备网格 4/3/2 列改 1/2 列，
//        S2/S3/S4 双卡改单列堆叠，Toast 抬到底栏上方；SideSheet 宽度自适应。
//        判定用**窗口朝向**（宽<高）而非设备类型：桌面窗拉高也进竖屏，手机旋转自动切。
//
//   从 pages/ 移入 shell/——它是骨架不是业务页；内容为"迁移到设计系统"版：
//
// 本页承载（对应《骨架迁移缺口清单》S 系列，2026-10-22 补齐）：
//   S1 三张统计卡（在线 / 待办 / 离线）
//   S2 云端存储概况（backendRef.storage）
//   S3 动作排行 · 近 7 天（backendRef.actionRanking）
//   S4 最近回执流水（宿主 root.results，最新 8 条）
//   S5 设备筛选 chip（全部 / 在线 / 离线）
//   S6 远控出口 → SideSheet 里「在集控页打开」
//   S7 响应式列数（4 / 3 / 2 列，随内容区宽度）
//
// ── 数据真源（全部沿用，绝不另存一份）───────────────────────────────────
//   班级目录   backendRef.classes   [{code,name}]
//   当前班     backendRef.currentClass / backendRef.setCurrentClass(code)
//   设备表     backendRef.devices     [{uid,name,classCode,online,version,lastSeenAgoSec}]
//   单台帧     image://frames/<uid>?t<tick>（FrameImageProvider）
//   帧订阅     backendRef.subscribeThumbnails(uids) + 信号 thumbnailChanged()
//   动作执行   ctx.window.runActionById(actionId)（权限/能力/二次确认全在宿主）
//
// ⚠️ 刻意**不用** ctx.window.filteredDevices：它叠加了集控页左栏 chip 的状态，
//   老师在那页筛过"离线"再进本页会只剩离线设备（本页有自己的 chip，见 S5）。
//
// ctx 形状与 pages/control/ControlPage.qml 同一份契约（Main.qml 的 _pageCtx）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../shell"              // PreviewConsole（唯一外壳）/ SideSheet / ToastLayer / ReauthDialog（同目录件）
import "../pages/dashboard"     // DashPage（仪表盘三件）
// 2026-10-28 用户裁决「取代」：不再 import "../pages/class" ——
//   旧的单班三栏工作台（ClassDashboard 及其 20 余个子件）已下线，班级中心改为
//   Loader 按路径直接加载 ClassCenterPage.qml，不需要把整个目录注册成类型模块。
import "../island"             // SvgIcon
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton）
import QtShadcn                // 2026-10-29 UI 全量替换 P0：shadcn 组件 + 主题入口

Item {
    id: shell

    // QtShadcn 主题实例。库要求「窗口内先放一个 QtShadcnTheme」——组件从它取
    // 语义 token（颜色/圆角/间距），没有它组件会取不到色值。
    // ⚠️ id 用 shadcnTheme：本文件与子件里 `theme` 另有含义（命名纪律，避自引用）。
    QtShadcnTheme { id: shadcnTheme }

    // ── P1 Token 对齐（2026-10-29）───────────────────────────────────────
    // 把品牌主色与明暗同步给 QtShadcn 的 ThemeManager，让 shadcn 组件与
    // ⚠️ ThemeManager 只开放 `primary` 一个覆写口（其余 token 只读，
    //    见 third_party/qtshadcn/src/core/thememanager.h）⇒ 只对齐主色 + 明暗，
    //    **不去改三方 C++ 的 rebuildTokens()**（那属于在别人地基上叠补丁，换版本必丢）。
    Binding { target: ThemeManager; property: "mode"; value: Theme.dark ? "dark" : "light" }
    Binding { target: ThemeManager; property: "primary"; value: Theme.dark ? "#FAFAFA" : "#18181B" }

    property var ctx
    // ── 后端转发（2026-11-01）──
    // ⚠️ 为什么不能直接写 `backendRef`：Main.qml 里 `backendRef` 是**全局 context property**，
    //    但 MainWindow 是 Main.qml 的子项 —— 若 MainWindow 自己（或其子件）声明了同名
    //    `backendRef` 属性，QML 非限定名会先查自身 ⇒ 自绑定 ⇒ "Binding loop detected for
    //    property backendRef"，随后该子树里所有 backendRef.xxx 全变 undefined（2026-10-10 实测，
    //    Main.qml:227-231 留了转发名 backendRef 就是为这个）。
    //    本轮加的 PreviewConsole 声明了后端属性 ⇒ 必须走转发名，不能用裸 backendRef。
    //    （顺带修正：MainWindow 里既有的裸 `backendRef` 引用实测为 null，故一并改走 backendRef。）
    property var backendRef: null

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // ── 骨架结构尺寸（AGENTS.md §9.2：令牌未覆盖的骨架尺寸以命名常量集中声明）──
    readonly property int statusBarH: 32      // 底部状态条高
    readonly property int toastBottomMargin: 28  // Toast 距底（横屏）
    readonly property int navBarH: 80

    // ── 竖横屏自适应（2026-10-22 用户点单）─────────────────────────────
    // 判定用**窗口朝向**（宽<高 = 竖屏），不用设备类型：
    //   · 桌面窗拉高成竖条形也进竖屏布局（合理：窄窗放 240 侧栏太挤）；
    //   · 手机旋转时 QML 绑定自动重算，无需重启；
    //   · 两种模式共用 navItems / 业务页 / 数据真源，只换导航位置与网格列数。
    readonly property bool portrait: shell.width < shell.height

    // ── 侧栏菜单（文案走教育 B 端规范）────────────────────────────────
    // icon 用 lucide 图标名（随 exe 打包），不用 emoji —— emoji 在本机会渲染成方块。
    // 2026-10-24 旧界面迁移：**「运行监控」从"跳旧壳 page 3"改成第 4 个 section**。
    //   原来是 goPage(3) 把整页甩给旧壳——旧壳那页有自己的底部标签栏和背景，
    //   点进去像"出了这个软件"。现在 MonitorPage 直接内嵌在内容区（同一个 TopBar /
    //   同一个状态条），旧壳 page 3 保留但仍可用（同一份 QML，靠 backFn 区分出口）。
    /** 设备集控面向管理员（用户裁决：班级页管本班设备、集控页是管理员视角）。
     *  只在**服务端下发并锁定**的受限身份（teacher）下隐藏入口 —— 判定用 `roleLocked`：
     *  它只在 applyServerRole() 里置真，全权连接（静态令牌/旧票）置假。
     *  2026-10-26 裁决：本地手动选的身份**不挡**。它表达的是"我此刻用什么视角看这台机器"，
     *    拿它砍页面入口会出现"切个身份功能就没了"的困惑；真正的权限事实是服务端下发的那个。
     *  未登录 / 全权令牌 ⇒ roleLocked 恒假 ⇒ 运维视角不受影响。 */
    readonly property bool canControlPage: !backendRef
        || !(backendRef.roleLocked === true && backendRef.role === "teacher")

    /** 身份在停留期间变了（例如在集控页上登录了一个老师账号）⇒ 立刻退回仪表盘。
     *  只隐藏菜单不够：section 已经是 control 的话页面还挂在那儿。 */
    onCanControlPageChanged: if (!shell.canControlPage && shell.section === "control") shell.section = "dash"

    /** 菜单全集：加页只改这一处。 */
    readonly property var navItemsAll: [
        { key: "dash",     icon: "monitor",     label: "仪表盘" },
        // 2026-10-28 用户裁决「取代」：全局班级阵列（ClassCenterPage）取代旧的单班三栏
        // 工作台（ClassDashboard / ClassDeviceGrid / ClassRightRail…共 20 余件，已下线）。
        // 原来的 key "classes" 也一并收归此处 —— 班级中心成为**唯一**班级入口。
        // 图标名必须对照 resources/icons/*.svg 实体（写不存在的名字会静默空白）：
        // 本工程**没有** "grid" 与 "users"，只有 "layout-grid"。
        { key: "classes",  icon: "layout-grid", label: "班级中心" },
        { key: "control",  icon: "layout-grid", label: "设备集控" },
        { key: "monitor",  icon: "eye",          label: "运行监控" },
        { key: "settings", icon: "settings",    label: "系统设置" }
    ]

    /** 侧栏菜单：按身份裁剪（过滤，不是另抄一份表 —— 否则加页又会漏一边）。 */
    readonly property var navItems: navItemsAll.filter(function (it) {
        return it.key !== "control" || shell.canControlPage
    })

    /** 当前栏目：dash / classes / control / monitor / settings。 */
    property string section: "dash"

    /** 侧滑面板展示哪台设备（"" = 关闭）。 */
    property string openUid: ""

    /** S5 设备筛选：all / online / offline。 */
    property string filterMode: "all"

    /**
     * 缩略帧订阅上限：UI 取前 9 台（沿用「多班墙最多 9 台」的既有口径）；
     * 后端另有 LRU 32 台硬上限兜底 —— 超出只保留最近订阅的那批。
     */
    readonly property int thumbLimit: 9

    /**
     * 选中班级 —— **唯一入口**（2026-10-28 用户裁决「取代」后接管
     * ClassDashboard.activateClass 的唯一职责，旧三栏已下线）。
     *
     * 三件事缺一不可：
     *   ① 后端双写：`setCurrentClass`（旧筛选，仪表盘/集控用它）+ `subscribeClass`
     *      （新班级域 typed 服务）。只写其中一个就会出现「网格空着不说话」。；
     *   ② 视图切到班级中心段（section 键沿用 "classes"，不新造键）；
     *   ③ 让控制器打开该班抽屉（C++ 侧 openClass 里做设备/事件/在线率重算）。
     */
    function pickClass(code) {
        shell.section = "classes"
        if (typeof backendRef !== "undefined" && backendRef) {
            backendRef.setCurrentClass(code)
            if (backendRef.classService) backendRef.classService.subscribeClass(code)
        }
        if (typeof classCenter !== "undefined" && classCenter) classCenter.openClass(code)
    }

    /** 模块二（2026-10-28 蓝图）：按身份决定**初始落点**。
     *  蓝图原话：「提取管理员中的"班级面板"，作为班主任/老师登录后的独立主页；
     *  不再从多班级列表中新建或跳转，直接沉浸式展示本班面板，省去老师的筛选成本。」
     *
     *  落哪一栏由 C++ 的 `backendRef.homeSection` 给（teacher⇒classes / admin⇒dash），
     *  QML 里不写 `role === "teacher"` 这种身份判断。
     *
     *  只种一次：用户手动切走之后不再抢回来 —— 否则每次数据刷新都把老师弹回首页。
     */
    property bool __homeSeeded: false
    function seedHomeSection() {
        if (__homeSeeded) return
        if (typeof backendRef === "undefined" || !backendRef) return
        const home = backendRef.homeSection
        if (!home || home.length === 0) return
        __homeSeeded = true
        shell.section = home
    }

    /** 本班设备：按班级码过滤；currentClass 为空 = 「全部班级」⇒ 不过滤（理由见文件头）。 */
    function classDevices() {
        const ds = (backendRef && backendRef.devices) ? backendRef.devices : []
        const cc = backendRef.currentClass || ""
        if (cc === "") return ds.slice()        // 全部班级：显示全部设备，不是"什么都没"
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            if (!ds[i]) continue
            if ((ds[i].classCode || "") === cc) out.push(ds[i])
        }
        return out
    }

    /** S1：计数（读 backendRef.storage 的待办；未拉到前显示 ——，不假装是 0）。 */
    function stats() {
        const ds = shell.classDevices()
        var on = 0
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) on++
        const st = (backendRef && backendRef.storage) ? backendRef.storage : ({})
        const pend = (st.pendingInstructions === undefined) ? "—" : st.pendingInstructions
        return { total: ds.length, on: on, off: ds.length - on, pending: pend }
    }

    /** 当前栏目在 StackLayout 里的下标（顺序与 navItems 一致）。 */
    readonly property int sectionIndex: {
        // 顺序与 navItemsAll 一致（① dash ② 班级中心 ③ 集控 ④ 监控 ⑤ 设置 ⑥ 个人中心）。
        // "classcenter" 是模块一时的旧键，保留兼容（老代码可能还在写它），同一段。
        if (shell.section === "classes") return 1
        if (shell.section === "classcenter") return 1
        if (shell.section === "control") return 2
        if (shell.section === "monitor") return 3
        if (shell.section === "settings") return 4
        if (shell.section === "account") return 5
        return 0
    }

    /** 顶栏标题（随栏目变，避免四个栏目都写「设备监控」）。 */
    readonly property string sectionTitle: {
        if (shell.section === "classes") return "班级中心"
        if (shell.section === "classcenter") return "班级中心"
        if (shell.section === "control") return "设备集控"
        if (shell.section === "monitor") return "运行监控"
        if (shell.section === "settings") return "系统设置"
        if (shell.section === "account") return "个人中心"
        return "设备监控"
    }

    /** 跳到某个栏目（业务页想"去集控页"时用这个，别再 goPage 切旧壳）。 */
    function openSection(key) { shell.navActivated(key) }

    /** 导航激活（Sidebar 与 BottomNav 共用，别处不许再写一份分支逻辑）：
     *   系统设置先过 RBAC 门禁（H5：受限身份不切栏、弹页级提示）。
     *   2026-10-24：「运行监控」不再跳旧壳 page 3，改为本页 section。 */
    function navActivated(key) {
        if (key === "settings" && (typeof perms === "undefined" || !perms.can("system:settings"))) {
            shell.toast("当前身份不能进系统设置", true)
            return
        }
        // 2026-10-25：集控页 = 多班/全校视角，只给管理员。老师仍能在班级看板里
        // 管本班设备（功能不削，只换入口），所以这里连菜单项一起隐藏，双保险。
        if (key === "control" && !shell.canControlPage) {
            shell.toast("设备集控面向管理员；本班设备的管控在班级看板里", true)
            return
        }
        shell.section = key
    }

    /** 把"当前班全部在线设备"塞进宿主的 picked。返回台数（0 = 没东西可发）。 */
    function pickAllOnline() {
        const ds = shell.classDevices()
        var u = []
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) u.push(ds[i].uid)
        if (shell.ctx) shell.ctx.window.picked = u
        return u.length
    }
    /** 批量动作：把"当前班全部在线设备"塞进宿主的 picked，再走唯一执行入口。
     *  ⚠️ actionId 必须是 **pick 系列批量 id**（pick-lock / pick-shutdown / pick-volume-* …）：
     *    单台 id（lock/reboot）在 runActionById 的 danger 分支只看 backendRef.currentUid，
     *    批量会被「先在控制页选中一台设备」拦截，或只锁那一台。
     *  2026-10-25：label 改问宿主要（pickActionLabel）—— 原来是
     *  `(actionId === "pick-lock") ? "锁定桌面" : "远程重启"`，两个动作时恰好蒙对，
     *  课堂工具加进来后会把"全班静音"显示成"远程重启"（骗人的回执）。 */
    function runBulk(actionId) {
        if (!shell.ctx) return
        const n = shell.pickAllOnline()
        if (n === 0) { shell.toast("当前班没有在线设备，无法批量下发", true); return }
        const label = shell.ctx.window.pickActionLabel
                       ? shell.ctx.window.pickActionLabel(actionId) : actionId
        shell.ctx.window.runActionById(actionId)
        shell.toast("已对 " + n + " 台在线设备下发「" + label + "」")
    }

    // ── H3 页级 Toast（骨架页自绘，不依赖灵动岛）─────────────────────
    // ⚠️ 灵动岛浮层在 page 6 被隐藏（Main.qml:1299），root.toast() 走 bridge.showNote
    //    → 灵动岛，这个页面上根本看不见。本地动作反馈必须自绘。
    // 2026-10-29：Toast 视觉抽到 shell/ToastLayer.qml（Theme 令牌），
    //    这里只保留函数入口 —— 语义不变：常规/错误两态配色在组件内。
    function toast(msg, isErr) {
        toastLayer.show(msg, isErr)
    }

    // ── H4 状态条文案（现算：设备总数 / 在线数 + 当前班 + 正在查看设备）──
    function _statusText() {
        const s = shell.stats()
        let cls = "全部班级"
        if (backendRef.currentClass) {
            cls = backendRef.currentClass
            const cs = (backendRef.classes) ? backendRef.classes : []
            for (var c = 0; c < cs.length; ++c)
                if (cs[c] && cs[c].code === backendRef.currentClass) { cls = cs[c].name || cs[c].code; break }
        }
        var txt = "班级 " + cls + " · " + s.total + " 台 · " + s.on + " 台在线"
        if (shell.openUid !== "") {
            const ds = shell.classDevices()
            for (var j = 0; j < ds.length; ++j)
                if (ds[j].uid === shell.openUid) { txt += " · 正在查看 " + (ds[j].name || ds[j].uid); break }
        }
        return txt
    }

    // 2026-10-29：骨架全屏底迁 Theme.color.background（页面最底，内容面板用 surface 分层）
    Rectangle { anchors.fill: parent; color: Theme.color.background }

    // ══ 唯一应用外壳（2026-11-01 转正）══════════════════════════════
    // 用户看过五个栏目全部真实业务内容后点单转正 ⇒ PreviewConsole 成为**唯一**外壳，
    // 经典外壳（shell/{Sidebar,TopBar,ActionBar,BottomNav,StatusBar}.qml）已退役删除。
    // 版面来自设计稿 qml/layout/DesktopConsole.qml；真功能（班级下拉双写 / 登录 /
    // 头像进个人中心 / 状态条 / 竖屏底栏）全部在 PreviewConsole 里接住。
    // 上层浮层（SideSheet / ToastLayer / ReauthDialog）与本件无关，仍在根节点上。
    PreviewConsole {
        id: shellUI
        anchors.fill: parent
        ctx: shell.ctx
        backendRef: shell.backendRef
        host: shell
        navItems: shell.navItems
        section: shell.section
        sectionIndex: shell.sectionIndex
        sectionTitle: shell.sectionTitle
        sectionSubtitle: shell.stats().total + " 台 · " + shell.stats().on + " 台在线"
        accountTitle: backendRef ? backendRef.roleText : ""
        accountSub: backendRef ? backendRef.accountName : ""
        accountAvatarText: backendRef ? backendRef.accountAvatarText : ""
        currentClass: backendRef ? backendRef.currentClass : ""
        devices: shell.classDevices()
        stats: shell.stats()
        portrait: shell.portrait
        filterMode: shell.filterMode
        openUid: shell.openUid
        statusLine: shell._statusText()
        statusBarH: shell.statusBarH
        navBarH: shell.navBarH
        toastFn: function (msg, isErr) { shell.toast(msg, isErr) }
        onNavActivated: function (key) { shell.navActivated(key) }
        // 头像 = 账户入口，落点**个人中心**（2026-10-25 裁决：系统设置受 system:settings
        // 门禁，teacher 点自己头像会被挡回来，等于个人中心根本进不去）。
        onAvatarClicked: function () { shell.navActivated("account") }
        // 未登录态「登录」（H8）→ 走站点 OAuth 授权换票。
        onLoginRequested: function () {
            shell.toast("正在打开站点登录…")
            if (backendRef) backendRef.loginWithSite()
        }
        // 班级切换：**双写唯一入口** shell.pickClass（2026-10-23 #472 裁决：
        // 旧筛选 setCurrentClass + 新班级域 subscribeClass，别处不许再写第二份）。
        onClassPicked: function (code) { shell.pickClass(code) }
        onDeviceActivated: function (uid) {
            shell.openUid = uid
            if (uid !== "" && backendRef) backendRef.currentUid = uid
        }
    }

    // ══ Z 轴上层：侧滑详情面板 + 50% 黑遮罩 ══
    SideSheet {
        ctx: shell.ctx
        opened: shell.openUid !== ""
        device: (function () {
            const ds = shell.classDevices()
            for (var i = 0; i < ds.length; ++i)
                if (ds[i].uid === shell.openUid) return ds[i]
            return null
        })()
        onClosed: shell.openUid = ""
    }

    // ── H3 Toast 浮层（最上层，z 高于 SideSheet；视觉在 shell/ToastLayer.qml）──
    ToastLayer {
        id: toastLayer
        portrait: shell.portrait
        navBarH: shell.navBarH
        statusBarH: shell.statusBarH
        bottomMargin: shell.toastBottomMargin
    }

    // ── 重认证弹窗（2026-10-24 安全门）──────────────────────────────────
    // 敏感操作被云端 REAUTH_REQUIRED 拦（长时间未验证密码）：连接**不断**，
    // 只提示"该再验一次密码了"→ 点「重新验证」走 backendRef.loginWithSite()
    // （OAuth 授权流程 = 站点验证密码）→ 新票据到位后一切自动恢复。
    // 2026-10-29：视觉抽到 shell/ReauthDialog.qml（Theme 令牌 + AppButton），
    //    backendRef 信号在组件内部连，宿主无需再写 Connections。
    ReauthDialog {}

    // 模块二（2026-10-28）：身份变了（站点登录换票 / 服务端下发锁定身份）⇒ 重新按身份落点。
    // 这里**允许重种**：登录是显著事件，「登录完直接看到本班」正是蓝图要的效果。
    Connections {
        target: backendRef
        function onRoleChanged() {
            shell.__homeSeeded = false
            shell.seedHomeSection()
        }
    }

    // ── 帧订阅：进页面订本班前 N 台在线设备，离开时退订 ────────────────
    // ⚠️ 为什么必须退订：云端按订阅推帧，不退订 = 后台一直拉流。
    function syncThumbs() {
        const ds = shell.classDevices()
        var u = []
        for (var i = 0; i < ds.length && u.length < shell.thumbLimit; ++i)
            if (ds[i].online !== false && ds[i].uid !== shell.openUid) u.push(ds[i].uid)
        backendRef.subscribeThumbnails(u)
    }

    Connections {
        target: backendRef
        function onDevicesChanged() {
            shell.syncThumbs()
        }
    }
    Connections {
        target: shell
        function onOpenUidChanged() { shell.syncThumbs() }
    }

    /** 栏目变化 = 拉流开关（2026-10-24 迁移第三刀）：集控/监控要看单台大画面，
     *  其余栏目不看。原先这个开关写在 Main.qml 的 syncPageLive 里按 page 判定，
     *  但 page 已收敛为 6，只有本件知道当前栏目 ⇒ 责任搬到这里。 */
    onSectionChanged: {
        if (backendRef)
            backendRef.setScreenActive(shell.section === "control" || shell.section === "monitor")
    }

    Component.onCompleted: {
        // 模块二（2026-10-28）：先定初始落点 —— 老师进本班面板、管理员进仪表盘。
        // 必须排在下面依赖 section 的动作之前，否则首帧的拉流开关会按错的栏目判一次。
        shell.seedHomeSection()
        shell.syncThumbs()
        // S3：进页面拉一次排行（云端 instructions 近 7 天；失败静默，界面自己会显示空态）
        if (backendRef) backendRef.fetchActionRanking()
        // 把栏目跳转通道交给宿主的 goPage（老页号兼容层）：Main.qml 的 goPage(idx)
        // 与各处 `ctx.window.goPage(1)` 最终都落到这里，RBAC 也只在这里判一次。
        if (shell.ctx && shell.ctx.window) {
            shell.ctx.window.sectionHook = function (key) { shell.navActivated(key) }
        }
        if (backendRef)
            backendRef.setScreenActive(shell.section === "control" || shell.section === "monitor")
    }
    Component.onDestruction: backendRef.subscribeThumbnails([])
}
