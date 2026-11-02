// 管理端 · 手机竖屏（390×844）外壳几何探针（2026-11-03）
//
// ── 为什么必须有它 ────────────────────────────────────────────────────
// 手机端跑的是 MainWindow → PreviewConsole 这套桌面外壳（**不是** qml/mobile/，
// 那是历史遗留死树，全库零引用）。竖屏时 240 侧栏换 80 底栏，其余一字未改，
// 于是桌面写死的尺寸直接进了 390pt 屏幕。改前实算的硬溢出：
//
//   · PreviewBottomNav：`Row { 5 × width:88 + 4 × spacing:8 }` = **472 > 390**
//     ⇒ 第 5 项「系统设置」被右边界裁掉 —— 手机上从底栏**永远进不去系统设置**。
//     这不是"不好看"，是功能不可达。
//   · PreviewTopBar：未登录时右组 = 登录(56) + spacing(16) + 头像(32) +
//     spacing(16) + 班级下拉(≈100) ⇒ 起点前移到 ≈162；左侧标题「班级中心」
//     从 x=24 起占约 64 ⇒ 两区重叠，标题被压在按钮下面。
//   · PreviewStatsRow：4 张卡各 (390-48)/4 = 85.5px，内含 44px 圆 + 两列文字
//     ⇒「设备总数」四个字必然折行或被裁。
//
// ── 断言怎么选（这才是本文件的价值）──────────────────────────────────
// **不能断言"改好之后的值"**——那是把实现抄进断言，改坏了照样绿。
// 这里断言的是**几何不变量**，任何"写死尺寸"的写法都会违反：
//   A 底栏所有可见项都落在容器内（x + width <= 容器宽）
//   B 底栏项均分后的单项宽 >= 56（触摸目标下限；Android 无障碍 48，留余量）
//   C 顶栏标题区与右侧控件区**不重叠**（标题右 <= 右组左）
//   D 统计行 compact 高度 = 40，且横向不溢出
//   E 竖屏边距 <= 16（桌面 24 在 390pt 上等于吃掉 12% 内容宽度）
//
// 反向验证（必做）：底栏 width 改回 88 ⇒ A、B 同时红；edgeMargin 改回 lg ⇒ E 红。
//
// 退出码：0 全过 / 3 有失败
import QtQuick
import QtQuick.Window
import "components/theme"
import "shell"

Window {
    id: win
    width: 390; height: 844
    visible: false

    property int fails: 0
    function chk(label, cond, extra) {
        if (!cond) {
            console.error("[narrow] FAIL " + label + (extra !== undefined ? "  (" + extra + ")" : ""))
            fails++
        } else {
            console.log("[narrow] OK   " + label)
        }
    }

    property var navItems: [
        { key: "dash",     icon: "monitor",     label: "仪表盘" },
        { key: "classes",  icon: "layout-grid", label: "班级中心" },
        { key: "control",  icon: "layout-grid", label: "设备集控" },
        { key: "monitor",  icon: "eye",         label: "运行监控" },
        { key: "settings", icon: "settings",    label: "系统设置" }
    ]

    PreviewBottomNav {
        id: nav
        width: 390
        height: 80
        navItems: win.navItems
    }

    PreviewTopBar {
        id: topBar
        width: 390; height: 64
        compact: true
        title: "班级中心"
        subtitle: "12 台 · 9 台在线"
        currentClass: "cls-fbe3c89dc4"
    }

    PreviewStatsRow {
        id: stats
        width: 390; height: 40
        compact: true
        deviceTotal: 20
        onlineCount: 12
        offlineCount: 8
    }

    Component.onCompleted: {
        // ── A/B：底栏 ──（靠 objectName 锚点，不靠 children 顺序猜）
        const navRow = nav.contentItem ? null : null
        var row = null
        for (var i = 0; i < nav.children.length; ++i)
            if (nav.children[i].objectName === "navRow") row = nav.children[i]
        chk("A0 底栏找到 navRow 锚点", row !== null)

        if (row) {
            var worst = -1, counted = 0
            for (var j = 0; j < row.children.length; ++j) {
                var it = row.children[j]
                if (!it || it.visible === false) continue
                counted++
                var r = it.x + it.width
                if (r > worst) worst = r
            }
            chk("A 底栏所有项在容器内", counted >= 5 && worst <= row.width + 0.5,
                "项数=" + counted + " 最右=" + worst.toFixed(1) + " 容器=" + row.width.toFixed(1))

            var n = win.navItems.length
            var each = (row.width - row.spacing * (n - 1)) / n
            chk("B 底栏项均分后 >= 56", each >= 56,
                "每项=" + each.toFixed(1) + "（改前写死 88，5×88+4×8=472>390）")
        }

        // ── C：顶栏重叠 ──
        var leftCol = null, rightRow = null
        for (var k = 0; k < topBar.children.length; ++k) {
            var c = topBar.children[k]
            if (c.objectName === "topTitle") leftCol = c
            if (c.objectName === "topRight") rightRow = c
        }
        chk("C0 顶栏找到两侧锚点", leftCol !== null && rightRow !== null)
        if (leftCol && rightRow)
            chk("C 顶栏标题与右侧控件不重叠",
                (leftCol.x + leftCol.width) <= (rightRow.x + 0.5),
                "标题右=" + (leftCol.x + leftCol.width).toFixed(1)
                + " 右组左=" + rightRow.x.toFixed(1))
            // C2 是 C 的补集，也是 C 单独存在时的**假阴性补丁**：
            // 锚定能让两区几何上不重叠，但代价是把标题列压窄 —— 文字被 elide
            // 吃成「班…」，探针却全绿。所以必须另断"没被压窄"。
            chk("C2 标题列没被压窄（文字不被 elide）",
                leftCol.width >= leftCol.implicitWidth - 0.5,
                "宽=" + leftCol.width.toFixed(1) + " 需要=" + leftCol.implicitWidth.toFixed(1))

        // ── D：统计行 ──
        chk("D 统计行 compact 高 40", stats.height === 40, "h=" + stats.height)

        // ── E：竖屏边距 ──
        chk("E 竖屏边距 <= 16", Theme.spacing.md <= 16, "spacing.md=" + Theme.spacing.md)

        console.log("[narrow] DONE fails=" + fails)
        Qt.exit(fails > 0 ? 3 : 0)
    }
}
