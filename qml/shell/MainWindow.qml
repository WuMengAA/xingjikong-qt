// 星集控管理端 · **全局主框架（App Scaffold 之一：MainWindow）**
//
// 定位（2026-10-22 用户点单「全局统一」+ AGENTS.md §9 全局 UI 构建红线）：
//   本文件是管理端**唯一的应用外壳**——规范里三种全局骨架的第一种
//   （主框架 / 弹窗遮罩 Md3Dialog+SideSheet / OOBE 独立窗口）。
//   **所有业务页面必须长在本骨架里**（经 StackLayout/Loader 挂载），
//   禁止任何页面自绘侧栏 / 顶栏 / 状态条，禁止新建第二个应用外壳。
//
// 结构（规范口径，全部尺寸走命名常量或 Md3 令牌，零硬编码）：
//   横屏：左侧 NavigationDrawer 240（surfaceContainer 底）+ 右侧 64 顶栏 + 内容区
//        （边缘内边距 spL=24）+ 底部 32 状态条；上层 SideSheet（400 右滑）+ Toast。
//   竖屏（2026-10-22 用户点单「竖屏横屏两种布局模式」）：左栏 240 让位给底部
//        BottomNav 80（同一份 navItems 的横排形态），设备网格 4/3/2 列改 1/2 列，
//        S2/S3/S4 双卡改单列堆叠，Toast 抬到底栏上方；SideSheet 宽度自适应。
//        判定用**窗口朝向**（宽<高）而非设备类型：桌面窗拉高也进竖屏，手机旋转自动切。
//
// 沿革：本文件原为 pages/Md3Shell.qml（2026-10-22 更名为 MainWindow.qml 并
//   从 pages/ 移入 shell/——它是骨架不是业务页；内容为"迁移到设计系统"版：
//   手绘统计卡/排行卡/存储卡 → Md3Card，手绘筛选 chip → Md3Chip，
//   Toast 配色 → inverse 令牌，字号/圆角/间距全部走 Md3 令牌）。
//
// 本页承载（对应《骨架迁移缺口清单》S 系列，2026-10-22 补齐）：
//   S1 三张统计卡（在线 / 待办 / 离线）
//   S2 云端存储概况（backend.storage）
//   S3 动作排行 · 近 7 天（backend.actionRanking）
//   S4 最近回执流水（宿主 root.results，最新 8 条）
//   S5 设备筛选 chip（全部 / 在线 / 离线）
//   S6 远控出口 → SideSheet 里「在集控页打开」
//   S7 响应式列数（4 / 3 / 2 列，随内容区宽度）
//
// ── 数据真源（全部沿用，绝不另存一份）───────────────────────────────────
//   班级目录   backend.classes   [{code,name}]
//   当前班     backend.currentClass / backend.setCurrentClass(code)
//   设备表     backend.devices     [{uid,name,classCode,online,version,lastSeenAgoSec}]
//   单台帧     image://frames/<uid>?t<tick>（FrameImageProvider）
//   帧订阅     backend.subscribeThumbnails(uids) + 信号 thumbnailChanged()
//   动作执行   ctx.window.runActionById(actionId)（权限/能力/二次确认全在宿主）
//
// ⚠️ 刻意**不用** ctx.window.filteredDevices：它叠加了集控页左栏 chip 的状态，
//   老师在那页筛过"离线"再进本页会只剩离线设备（本页有自己的 chip，见 S5）。
//
// ctx 形状与 pages/control/ControlPage.qml 同一份契约（Main.qml 的 _pageCtx）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/md3"     // Md3Card / Md3Chip / Md3Btn（全局组件库，AGENTS.md §9.3）
import "../shell"              // Sidebar / TopBar / ActionBar / DeviceCard / SideSheet（同目录骨架件）
import "../pages/dashboard"     // DashPage（仪表盘三件）
// 2026-10-28 用户裁决「取代」：不再 import "../pages/class" ——
//   旧的单班三栏工作台（ClassDashboard 及其 20 余个子件）已下线，班级中心改为
//   Loader 按路径直接加载 ClassCenterPage.qml，不需要把整个目录注册成类型模块。
import "../island"             // SvgIcon
import "../components/theme"   // Md3 令牌（pragma Singleton）
import QtShadcn                // 2026-10-29 UI 全量替换 P0：shadcn 组件 + 主题入口

Item {
    id: shell

    // QtShadcn 主题实例。库要求「窗口内先放一个 QtShadcnTheme」——组件从它取
    // 语义 token（颜色/圆角/间距），没有它组件会取不到色值。
    // ⚠️ id 用 shadcnTheme：本文件与子件里 `theme` 另有含义（命名纪律，避自引用）。
    // 过渡期：Md3 令牌（th/tok）与 shadcn token 并存，P1 做 Token 对齐时统一。
    QtShadcnTheme { id: shadcnTheme }

    // ── P1 Token 对齐（2026-10-29）───────────────────────────────────────
    // 把品牌主色与明暗同步给 QtShadcn 的 ThemeManager，让 shadcn 组件与
    // 现有 Md3 令牌**观感一致**，而不是各说各话。
    // ⚠️ ThemeManager 只开放 `primary` 一个覆写口（其余 token 只读，
    //    见 third_party/qtshadcn/src/core/thememanager.h）⇒ 只对齐主色 + 明暗，
    //    **不去改三方 C++ 的 rebuildTokens()**（那属于在别人地基上叠补丁，换版本必丢）。
    Binding { target: ThemeManager; property: "mode"; value: Md3.dark ? "dark" : "light" }
    Binding { target: ThemeManager; property: "primary"; value: Md3.dark ? "#D0BCFF" : "#6750A4" }

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // ── 骨架结构尺寸（AGENTS.md §9.2：令牌未覆盖的骨架尺寸以命名常量集中声明）──
    readonly property int statusBarH: 32      // 底部状态条高
    readonly property int toastBottomMargin: 28  // Toast 距底（横屏）
    readonly property int navBarH: 80         // 竖屏底部导航高（MD3 bottom navigation）

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
    readonly property bool canControlPage: !backend
        || !(backend.roleLocked === true && backend.role === "teacher")

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
        if (typeof backend !== "undefined" && backend) {
            backend.setCurrentClass(code)
            if (backend.classService) backend.classService.subscribeClass(code)
        }
        if (typeof classCenter !== "undefined" && classCenter) classCenter.openClass(code)
    }

    /** 模块二（2026-10-28 蓝图）：按身份决定**初始落点**。
     *  蓝图原话：「提取管理员中的"班级面板"，作为班主任/老师登录后的独立主页；
     *  不再从多班级列表中新建或跳转，直接沉浸式展示本班面板，省去老师的筛选成本。」
     *
     *  落哪一栏由 C++ 的 `backend.homeSection` 给（teacher⇒classes / admin⇒dash），
     *  QML 里不写 `role === "teacher"` 这种身份判断。
     *
     *  只种一次：用户手动切走之后不再抢回来 —— 否则每次数据刷新都把老师弹回首页。
     */
    property bool __homeSeeded: false
    function seedHomeSection() {
        if (__homeSeeded) return
        if (typeof backend === "undefined" || !backend) return
        const home = backend.homeSection
        if (!home || home.length === 0) return
        __homeSeeded = true
        shell.section = home
    }

    /** 本班设备：按班级码过滤；currentClass 为空 = 「全部班级」⇒ 不过滤（理由见文件头）。 */
    function classDevices() {
        const ds = (backend && backend.devices) ? backend.devices : []
        const cc = backend.currentClass || ""
        if (cc === "") return ds.slice()        // 全部班级：显示全部设备，不是"什么都没"
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            if (!ds[i]) continue
            if ((ds[i].classCode || "") === cc) out.push(ds[i])
        }
        return out
    }

    /** S1：计数（读 backend.storage 的待办；未拉到前显示 ——，不假装是 0）。 */
    function stats() {
        const ds = shell.classDevices()
        var on = 0
        for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) on++
        const st = (backend && backend.storage) ? backend.storage : ({})
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
     *    单台 id（lock/reboot）在 runActionById 的 danger 分支只看 backend.currentUid，
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
    // 配色走令牌：常规 = inverseSurface/inverseOn（MD3 Snackbar 口径，2026-10-22 前是
    // 硬编码 #322F35/#FFFFFF）；错误 = error 底 + 白字。
    property bool toastErr: false
    function toast(msg, isErr) {
        if (!msg) return
        toastText.text = msg
        shell.toastErr = (isErr === true)
        toastBox.color = shell.toastErr
                         ? (shell.th ? shell.th.cError : "#B3261E")
                         : (shell.th ? shell.th.cInverseSurf : "#313033")
        toastLayer.visible = true
        toastHide.restart()
    }

    // ── H4 状态条文案（现算：设备总数 / 在线数 + 当前班 + 正在查看设备）──
    function _statusText() {
        const s = shell.stats()
        let cls = "全部班级"
        if (backend.currentClass) {
            cls = backend.currentClass
            const cs = (backend.classes) ? backend.classes : []
            for (var c = 0; c < cs.length; ++c)
                if (cs[c] && cs[c].code === backend.currentClass) { cls = cs[c].name || cs[c].code; break }
        }
        var txt = "班级 " + cls + " · " + s.total + " 台 · " + s.on + " 台在线"
        if (shell.openUid !== "") {
            const ds = shell.classDevices()
            for (var j = 0; j < ds.length; ++j)
                if (ds[j].uid === shell.openUid) { txt += " · 正在查看 " + (ds[j].name || ds[j].uid); break }
        }
        return txt
    }

    Rectangle { anchors.fill: parent; color: shell.th ? shell.th.cSurface : "#FFFBFE" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ══ 左侧全局导航（240px · 2026-10-28 蓝图还原）══
            // 蓝图的手写稿里，左侧是**功能菜单**：品牌「星集控」+ 账号卡 + 栏目列表。
            // 原来这里挂的是 ClassNav（"以班为单位"的入口，班级列表在上、系统组在下）——
            // 按用户裁决「取代」，班级选择已收进班级中心的网格阵列（点卡片开抽屉），
            // 左栏不再重复一份班级列表，改回蓝图的菜单形态（Sidebar，之前零引用）。
            // 竖屏让位给 BottomNav（visible:false 的项不占布局空间）。
            Sidebar {
                id: nav
                visible: !shell.portrait
                ctx: shell.ctx
                items: shell.navItems
                current: shell.section
                // 账号卡文案全部取自后端（取首字 / 角色→中文都在 C++，QML 不做映射）：
                //   avatarText  = 账号名首字，没登录时回落到角色首字（管/师）
                //   accountTitle= 角色中文名（管理员 / 老师）
                //   accountSub  = 账号名（没登录就是空串，不编一个假学校名出来）
                avatarText: backend ? backend.accountAvatarText : ""
                accountTitle: backend ? backend.roleText : ""
                accountSub: backend ? backend.accountName : ""
                onActivated: function (key) { shell.navActivated(key) }
            }

            // ══ 主内容区（Surface）══
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    id: mainCol
                    // 内容区边缘内边距 = spacing.lg(24)（AGENTS.md §9.2）
                    anchors.leftMargin: shell.th ? shell.th.spL : 24
                    anchors.rightMargin: shell.th ? shell.th.spL : 24
                    spacing: 0

                    // ── 顶部导航栏（64px）──
                    TopBar {
                        id: topBar
                        ctx: shell.ctx
                        title: shell.sectionTitle
                        // ⚠️ 班级列表与当前班**不在这里传参** —— TopBar 内部直接读
                        //    backend.classes / backend.currentClass。曾经多传不存在的属性，
                        //    结果 `Type Md3Shell unavailable` 整页起不来（qmllint 0 Error）。
                        // 2026-10-23 #472：班级选择必须**双写**（旧筛选 setCurrentClass +
                        // 新班级域 subscribeClass）。2026-10-28 裁决「取代」后，双写收进
                        // shell.pickClass() 这个唯一入口，ClassDashboard 已下线。
                        onClassPicked: function (code) { shell.pickClass(code) }
                        // 头像 = 账户入口。2026-10-25：落点从「系统设置」改成「个人中心」——
                        //   系统设置受 system:settings 门禁（teacher 无此权限），老师点自己头像
                        //   会被一句"当前身份不能进系统设置"挡回来，等于个人中心根本进不去。
                        //   个人中心不设门禁（任何人都该能看自己的账号与退出登录）。
                        onAvatarClicked: function () { shell.navActivated("account") }
                        onLoginRequested: {
                            shell.toast("正在打开站点登录…")
                            if (backend) backend.loginWithSite()
                        }
                    }

                    // ── 操作区（距顶部 spacing.lg=24）──
                    ActionBar {
                        Layout.fillWidth: true
                        Layout.topMargin: shell.th ? shell.th.spL : 24
                        ctx: shell.ctx
                        portrait: shell.portrait     // 竖屏：多一个「返回经典界面」出口
                        onLockRequested: shell.runBulk("pick-lock")
                        onRebootRequested: shell.runBulk("pick-shutdown")
                        onClassicRequested: {
                            // H7：竖屏侧栏被底栏取代，出口随 ActionBar 可见
                            // （横屏该出口在 Sidebar 底部，两条路都通向 page 1）。
                            if (shell.ctx) shell.ctx.window.goPage(1)
                        }
                    }

                    // ── 内容区（距操作区 spacing.md=16）──
                    StackLayout {
                        id: mainStack
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: shell.th ? shell.th.spM : 16
                        Layout.bottomMargin: shell.th ? shell.th.spM : 16
                        currentIndex: shell.sectionIndex


                        // ① 仪表盘（S1/S2/S3/S4/S5/S7 全在 pages/dashboard/）
                        // 抽件原因：本文件已到登记上限（710 行），内联 270 行不允许再长。
                        DashPage {
                            host: shell.ctx ? shell.ctx.window : null
                            devices: shell.classDevices()
                            stats: shell.stats()
                            portrait: shell.portrait
                            filterMode: shell.filterMode
                            openUid: shell.openUid
                            toastFn: function (msg, isErr) { shell.toast(msg, isErr) }
                            onFilterChanged: function (m) { shell.filterMode = m }
                            onDeviceActivated: function (uid) {
                                shell.openUid = uid
                                // H1：同时写宿主「看哪台」真源（SideSheet 与危险动作都取它）
                                if (uid !== "" && backend) backend.currentUid = uid
                            }
                        }

                        // ② 班级中心（2026-10-28 模块一 · 用户裁决「取代」）
                        //    原先这里是 ClassDashboard 单班三栏工作台（左身份/中网格/右动作），
                        //    已按裁决整体下线；班级域数据仍走 backend.classService（未变）。
                        //    本页是**纯声明式**：数据、几何、配色全在 C++ ClassCenterController
                        //    （context property: classCenter），点击只调 classCenter.requestOp("名字")。
                        Loader {
                            source: "../pages/class/ClassCenterPage.qml"
                        }

                        // ③ 设备集控（旧界面迁移 2026-10-24 · 用户裁决 A「先搬后拆」）：
                        //    ControlPage 原样内嵌，零重写。它自带左栏设备列表与多选，
                        //    与本壳的班级导航并存（前者管"选哪台/多选"，后者管"看哪个班"）。
                        // ⚠️ 必须用 setSource(url, {ctx}) 而不是 source + onLoaded 注入：
                        //    ControlPage 里 `visible: ctx.window.currentUid !== ""` 这类绑定
                        //    在**实例化瞬间**就求值，onLoaded 时补 ctx 已经晚了
                        //    （实测 TypeError: Cannot read property 'window' of undefined）。
                        Loader {
                            id: controlLoader
                            Component.onCompleted: {
                                if (shell.ctx)
                                    setSource("../pages/control/ControlPage.qml", { "ctx": shell.ctx })
                            }
                        }

                        // ④ 运行监控（旧界面迁移 2026-10-24）：直接内嵌 MonitorPage，
                        //    不再 goPage(3) 甩给旧壳。同一份 QML 在旧壳 page 3 仍可用，
                        //    差别只在 backFn —— 注入了就切回仪表盘，不注入就跳 page 1。
                        Loader {
                            id: monitorLoader
                            Component.onCompleted: {
                                if (!shell.ctx) return
                                setSource("../pages/control/MonitorPage.qml", {
                                    "ctx": shell.ctx,
                                    "backFn": function () { shell.section = "dash" }
                                })
                            }
                        }

                        // ⑤ 系统设置：只给管理员（navActivated 里的 system:settings 门禁）
                        Loader {
                            source: "../pages/settings/SettingsPage.qml"
                            onLoaded: if (item) item.ctx = shell.ctx
                        }

                        // ⑥ 个人中心（2026-10-25）：头像的落点。**不受** system:settings 门禁 ——
                        //    老师必须能看自己的账号、在册班级、提醒/外观偏好与退出登录。
                        //    原来头像直接跳 ⑤，而 ⑤ 对 teacher 是关的 ⇒ 老师点自己头像被挡。
                        Loader {
                            source: "../pages/account/AccountCenterPage.qml"
                            onLoaded: if (item) item.ctx = shell.ctx
                        }
                    }
                }
            }
        }

        // ══ 竖屏：底部导航（与 Sidebar 同一份 navItems 的横排形态）══
        // 放在状态条**上方**：状态条是全局信息带，朝向无关，两种模式都在最底。
        BottomNav {
            Layout.fillWidth: true
            Layout.preferredHeight: shell.navBarH
            visible: shell.portrait
            items: shell.navItems
            current: shell.section
            onActivated: function (key) { shell.navActivated(key) }
        }

        // ── H4 状态条（全宽贴底）──────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: shell.statusBarH
            color: shell.th ? shell.th.cSurfaceVar : "#E7E0EC"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: shell.th ? shell.th.spM : 16
                anchors.rightMargin: shell.th ? shell.th.spM : 16
                spacing: shell.th ? shell.th.spM : 16
                Text {
                    Layout.fillWidth: true
                    text: shell._statusText()
                    font.pixelSize: shell.th ? shell.th.bodyS : 12
                    color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                    elide: Text.ElideRight
                }
            }
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

    // ── H3 Toast 浮层（最上层，z 高于 SideSheet）────────────────────
    Item {
        id: toastLayer
        anchors.fill: parent
        z: 1000
        visible: false
        Rectangle {
            id: toastBox
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // 竖屏：抬到底部导航 + 状态条上方（否则糊在底栏上看不清）
            anchors.bottomMargin: shell.portrait
                                    ? (shell.navBarH + shell.statusBarH + 12)
                                    : shell.toastBottomMargin
            radius: shell.th ? shell.th.rS : 8
            color: shell.th ? shell.th.cInverseSurf : "#313033"
            Text {
                id: toastText
                anchors.centerIn: parent
                text: ""
                font.pixelSize: shell.th ? shell.th.body13 : 13
                color: shell.toastErr ? "#FFFFFF"
                                       : (shell.th ? shell.th.cInverseOn : "#F4EFF4")
                padding: shell.th ? shell.th.spS + shell.th.spXS : 12
            }
        }
        Timer {
            id: toastHide
            interval: 2600
            onTriggered: toastLayer.visible = false
        }
    }

    // ── 重认证弹窗（2026-10-24 安全门）──────────────────────────────────
    // 敏感操作被云端 REAUTH_REQUIRED 拦（长时间未验证密码）：连接**不断**，
    // 只提示"该再验一次密码了"→ 点「重新验证」走 backend.loginWithSite()
    // （OAuth 授权流程 = 站点验证密码）→ 新票据到位后一切自动恢复。
    // 自绘遮罩（理由同 Toast：骨架页不依赖灵动岛/组件弹窗的挂载层级）。
    Rectangle {
        id: reauthLayer
        anchors.fill: parent
        visible: false
        color: "#66000000"        // scrim 半透明黑（MD3 overlay 口径）

        Rectangle {
            id: reauthCard
            width: Math.min(360, parent.width - 48)
            anchors.centerIn: parent
            radius: 12
            color: shell.th ? shell.th.cSurface : "#FFFBFE"

            Column {
                width: parent.width - 32
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                spacing: 12

                Text {
                    text: "需要重新验证密码"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: shell.th ? shell.th.cOnSurface : "#1C1B1F"
                }
                Text {
                    id: reauthMsg
                    width: parent.width
                    text: "长时间未验证，敏感操作被云端拦截。"
                    font.pixelSize: 13
                    color: shell.th ? shell.th.cOnSurfaceVar : "#49454F"
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: "连接不会断开，重新验证一次即可恢复全部操作。"
                    font.pixelSize: 12
                    color: shell.th ? shell.th.cOutline : "#79747E"
                    wrapMode: Text.WordWrap
                }

                Row {
                    width: parent.width
                    height: 40
                    spacing: 12
                    layoutDirection: Qt.RightToLeft

                    Md3Btn {
                        text: "重新验证"
                        variant: "filled"
                        onClicked: {
                            reauthLayer.visible = false
                            if (backend) backend.loginWithSite()
                        }
                    }
                    Md3Btn {
                        text: "稍后"
                        variant: "text"
                        onClicked: reauthLayer.visible = false
                    }
                }
            }
        }
    }
    Connections {
        target: backend
        function onReauthRequired(message) {
            if (message) reauthMsg.text = message
            reauthLayer.visible = true
        }
    }

    // 模块二（2026-10-28）：身份变了（站点登录换票 / 服务端下发锁定身份）⇒ 重新按身份落点。
    // 这里**允许重种**：登录是显著事件，「登录完直接看到本班」正是蓝图要的效果。
    Connections {
        target: backend
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
        backend.subscribeThumbnails(u)
    }

    Connections {
        target: backend
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
        if (backend)
            backend.setScreenActive(shell.section === "control" || shell.section === "monitor")
    }

    Component.onCompleted: {
        // 模块二（2026-10-28）：先定初始落点 —— 老师进本班面板、管理员进仪表盘。
        // 必须排在下面依赖 section 的动作之前，否则首帧的拉流开关会按错的栏目判一次。
        shell.seedHomeSection()
        shell.syncThumbs()
        // S3：进页面拉一次排行（云端 instructions 近 7 天；失败静默，界面自己会显示空态）
        if (backend) backend.fetchActionRanking()
        // 把栏目跳转通道交给宿主的 goPage（老页号兼容层）：Main.qml 的 goPage(idx)
        // 与各处 `ctx.window.goPage(1)` 最终都落到这里，RBAC 也只在这里判一次。
        if (shell.ctx && shell.ctx.window) {
            shell.ctx.window.sectionHook = function (key) { shell.navActivated(key) }
        }
        if (backend)
            backend.setScreenActive(shell.section === "control" || shell.section === "monitor")
    }
    Component.onDestruction: backend.subscribeThumbnails([])
}
