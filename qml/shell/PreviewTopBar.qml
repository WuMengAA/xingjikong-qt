// 新版外壳 · 顶栏（2026-11-01 · 转正后唯一顶栏 · QtShadcn 全量）
//
// 转正改动（2026-11-01）：从「纯展示」换成**真功能**顶栏 —— 接住经典外壳 TopBar 的三件事：
//   ① 班级切换下拉 → classPicked(code) → 宿主 shell.pickClass(code)
//      （**双写**：旧筛选 setCurrentClass + 新班级域 subscribeClass，2026-10-23 #472 裁决）
//   ② 未登录态「登录」按钮（H8）→ loginRequested() → backendRef.loginWithSite()
//   ③ 头像 → avatarClicked() → 个人中心（**不**是系统设置：system:settings 有 RBAC 门禁，
//      老师点自己头像会被挡回，2026-10-25 裁决）
//
// ── 2026-11-01 QtShadcn 迁移（执行 10-29 用户裁决「两端全量 QtShadcn」）──
// 之前这里全是手画 Rectangle/Text + 原生 Menu —— 与裁决不符。本版换成库组件：
//   · 班级下拉：原生 Menu → **ShadcnDropdownMenu / Trigger / Content / Item**
//     （Overlay 层渲染，天然置顶，不会被页面元素遮挡；自带点击外部/ESC 关闭）
//   · 登录按钮：手画 → **ShadcnButton**(variant: Primary)
//   · 头像：手画圆 + Text → **ShadcnAvatar**（自带尺寸档与 status 点）
//   · 分隔线：Rectangle → **ShadcnSeparator**
// ⚠️ 前提：main.cpp 必须 qputenv("QT_QUICK_CONTROLS_STYLE", "Basic")（已在 viewer main.cpp:423），
//    否则组件内部 QtQuick.Controls.Basic 的自绘样式会被宿主风格吃掉 ⇒ 静默"样式全不对"。
//
// ⚠️ backendRef 而不是裸 backend：MainWindow 作用域内的裸 `backend` 实测恒为 null
//    （QML 非限定名先查自身属性，而 Main 用 backendRef 这个不重名转发名）。
import QtQuick
import QtQuick.Layouts
import QtShadcn
import "../components/theme"     // Theme 令牌（调色板；QtShadcn 是静态模块，Theme 自带调色板）
import "ClassSort.js" as ClassSort

Item {
    id: root

    /** 宿主上下文（与 pages/ 下各页同一份 _pageCtx 契约）。 */
    property var ctx: null
    property var backendRef: null
    /** 班级列表：宿主注入；空则回落 backendRef.classes。 */
    property var classes: []
    /** 当前班：宿主注入；空则回落 backendRef.currentClass。 */
    property string currentClass: ""
    /** 顶栏标题（随栏目变，宿主给）。 */
    property string title: ""
    /** 副标题（本班台数等，宿主给；空则不显示）。 */
    property string subtitle: ""
    property string accountAvatarText: ""
    /** 窄屏紧凑模式（2026-11-03 加）：手机竖屏下标题/副标题与右侧控件必然撞车。 */
    property bool compact: false

    signal classPicked(string code)
    signal avatarClicked()
    signal loginRequested()

    /** 实际使用的班级表（注入优先）。 */
    readonly property var _classes: (root.classes && root.classes.length)
                                   ? root.classes
                                   : ((root.backendRef && root.backendRef.classes) ? root.backendRef.classes : [])
    /** 下拉里的显示顺序：1班 < 2班 < … < 10班 < 12班（规则见 ClassSort.js）。 */
    readonly property var _classesSorted: ClassSort.byName(root._classes)
    /** 实际使用的当前班（注入优先）。 */
    readonly property string _cur: (root.currentClass !== "")
                                   ? root.currentClass
                                   : ((root.backendRef && root.backendRef.currentClass) || "")

    /** 当前班级名（下拉按钮文案）—— 派生，不缓存。 */
    readonly property string currentClassName: {
        const cs = root._classes
        const cc = root._cur
        for (var i = 0; i < cs.length; ++i)
            if (cs[i].code === cc) return cs[i].name || cs[i].code
        return cc === "" ? "全部班级" : cc
    }

    /** 是否已登录（未登录时暴露「登录」入口，H8）。 */
    readonly property bool loggedIn: (root.backendRef && root.backendRef.loggedIn === true)

    height: 64

    // ── 2026-11-03 窄屏（390pt 竖屏）实算的撞车点 ─────────────────────
    // 改前：左标题「班级中心」约 64px 从 x=24 起，右组 = 班级下拉(约 100) +
    //   spacing 16 + 头像 32，从 x≈218 起 ⇒ 标题区剩 194px 够，但副标题
    //   （「12 台 · 9 台在线」这类）一长就被右侧压住；未登录时多一个「登录」
    //   按钮（约 56px）⇒ 右组起点前移到 ≈162，标题直接重叠。
    // compact 下：标题只留一行、副标题隐藏（内容区第一屏已有统计行，信息不丢），
    //   右组间距收紧到 xs。**不隐藏班级下拉** —— 手机上切班是主操作之一。
    readonly property bool _c: root.compact

    ShadcnSeparator {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        orientation: Qt.Horizontal
    }

    // ── 左：标题（栏目名 + 副标题）────────────────────────────────
    ColumnLayout {
        objectName: "topTitle"      // 探针锚点（_narrow_probe.qml 量重叠用）
        anchors.left: parent.left
        anchors.leftMargin: root._c ? Theme.spacing.md : Theme.spacing.lg
        anchors.right: rightGroup.left
        anchors.rightMargin: Theme.spacing.sm
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            Layout.fillWidth: true
            visible: root.title !== ""
            text: root.title
            font.pixelSize: 16
            font.weight: Font.Medium
            color: Theme.color.textPrimary
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: root.subtitle !== "" && !root._c
            text: root.subtitle
            font.pixelSize: 13
            color: Theme.color.textSecondary
            elide: Text.ElideRight
        }
    }

    // ── 右：班级下拉 + 登录 + 头像 ───────────────────────────────
    RowLayout {
        id: rightGroup
        objectName: "topRight"      // 探针锚点
        anchors.right: parent.right
        anchors.rightMargin: root._c ? Theme.spacing.md : Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        spacing: root._c ? Theme.spacing.xs : Theme.spacing.md

        // 班级切换下拉（shadcn DropdownMenu：Overlay 层，自带外部点击/ESC 关闭）
        ShadcnDropdownMenu {
            id: classMenu
            Layout.preferredWidth: classTrigger.implicitWidth
            Layout.preferredHeight: 40

            ShadcnDropdownMenuTrigger {
                id: classTrigger
                // 窄屏去掉「 切换」二字：下拉按钮本身就是可点的样子，
                // 这两个字在 390pt 下挤掉的是设备名。
                text: root.currentClassName + (root._c ? "" : " 切换")
                variant: ShadcnButton.Variant.Secondary
                size: ShadcnButton.Size.Small
                // ⚠️ Trigger 是 ShadcnButton 子类，用 onClicked 驱动 open（没有 onTriggered）
                onClicked: classMenu.open = !classMenu.open
            }
            ShadcnDropdownMenuContent {
                ShadcnDropdownMenuItem {
                    text: "全部班级"
                    // ⚠️ MenuItem 的信号是 clicked()，不是 triggered()（无 onTriggered ⇒ 整页加载失败）
                    onClicked: root.classPicked("")
                }
                Repeater {
                    model: root._classesSorted
                    delegate: ShadcnDropdownMenuItem {
                        required property var modelData
                        text: modelData.name || modelData.code
                        onClicked: root.classPicked(modelData.code)
                    }
                }
            }
        }

        // H8：未登录态暴露「登录」入口
        ShadcnButton {
            visible: !root.loggedIn
            text: "登录"
            variant: ShadcnButton.Variant.Primary
            size: ShadcnButton.Size.Small
            onClicked: root.loginRequested()
        }

        // 头像（仅登录后显示）；点了进个人中心（无 RBAC 门禁）
        ShadcnAvatar {
            visible: root.loggedIn
            size: ShadcnAvatar.Size.Medium
            text: root.accountAvatarText || "◍"
            TapHandler { onTapped: root.avatarClicked() }
        }
    }
}
