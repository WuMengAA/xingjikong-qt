pragma Singleton

// 星集控 · 设计令牌唯一真源（Theme）—— 2026-10-29 QtShadcn 规范落地
//
// ── 定位 ────────────────────────────────────────────────────────────────
// 全项目**唯一**的主题令牌入口。页面/组件里只准写 `Theme.color.primary`、
// `Theme.radius.lg`、`Theme.spacing.md` 这类语义引用；**页面里出现任何 Hex 一律违规**。
// （令牌文件本身是调色板，字面量理应住在这里 —— 这是唯一定义处。）
//
// ── 为什么自带色值、不 import QtShadcn ──────────────────────────────────
// 原先实现是 `ThemeManager.tokens[...]` 的绑定，但 QtShadcn 在本工程是 **static QML 模块**
// （qmldir 写 `optional plugin QtShadcnplugin`、无 .dll、插件目标未编译），
// 导致任何**未链接 QtShadcn 的宿主**（探针、qml.exe、未来工具）加载 Theme 直接失败：
//   `module "QtShadcn" plugin "QtShadcnplugin" not found`
// ⇒ 改为自带调色板，解除运行时依赖。色值来源标注见各项注释，视觉与 shadcn 默认一致。
//
// ── 关于"页面底 vs 卡片底"为什么必须不同 ─────────────────────────────────
// 规范要求「禁边框、靠背景色差分区」。而 shadcn 默认 dark 里 background 与 card **同为
// #09090b** ⇒ 无边可依、也无声色差，卡片会"糊"在一起。故按规范本意取值：
//   浅色：页面 极浅灰 / 卡片 纯白      深色：页面 近黑 / 卡片 深灰
// 这样去掉边框后区域依然分得开。
//
// ── 关于 Md3.qml ────────────────────────────────────────────────────────
// `ui/theme/Md3.qml` 是上一代（Material 3）令牌，存量页面仍在引用；
// 本文件是**取代它的新真源**。新代码只用 Theme，不得再用 Md3；P4 迁移完成后 Md3 退役。

import QtQuick

QtObject {
    id: root

    /** 明暗模式。主外壳唯一切换入口：`Theme.dark = darkMode`（全端绑定自动随动）。 */
    property bool dark: false

    /**
     * 「不要品牌紫」开关（2026-10-30 用户点单：被控端一律中性色，品牌紫只属于管理端）。
     *
     * 为什么不逐个组件传参：开关/分段/药丸/导航的选中色散落在 5 个件里、被控端有 14+ 处
     * 实例，逐处传一个 `accent` 属性既啰嗦又必然漏改。改在令牌层设一个策略位，
     * 被控端外壳设 `Theme.neutralAccent = true` 一次生效；管理端不设（默认 false）
     * ⇒ 读到的仍是品牌紫，**对既有页面零影响**。
     *
     * 设 true 后 `accentStrong` 回落为中性反白（深色模式白底黑字 / 浅色模式黑底白字），
     * 这正是 shadcn 默认按钮与开关的观感。
     */
    property bool neutralAccent: false

    /** 强调色：开关轨道 / 选中段 / 当前导航 / primary 按钮底。 */
    readonly property color accentStrong: neutralAccent ? root.color.textPrimary
                                                       : root.color.primary
    /** 强调色上的文字/滑块色。 */
    readonly property color accentStrongForeground: neutralAccent ? root.color.background
                                                                 : root.color.primaryForeground

    // ═══ 颜色 ═══
    readonly property QtObject color: QtObject {
        /** 页面底色：浅＝极浅灰 ／ 深＝近黑。 */
        readonly property color background: root.dark ? "#09090B" : "#FAFAFA"
        /** 卡片 / 面板底：浅＝纯白 ／ 深＝深灰（必须比 background 亮，否则去边框后分不开）。 */
        readonly property color surface: root.dark ? "#18181B" : "#FFFFFF"
        /** 次级容器底：分组块、次级按钮底。 */
        readonly property color surfaceVariant: root.dark ? "#27272A" : "#F4F4F5"
        /** 主文字。 */
        readonly property color textPrimary: root.dark ? "#FAFAFA" : "#09090B"
        /** 次文字。 */
        readonly property color textSecondary: root.dark ? "#A1A1AA" : "#71717A"
        /** 主色（主按钮、激活态）—— 沿用项目品牌紫，非 shadcn 中性灰，保持品牌识别。 */
        readonly property color primary: root.dark ? "#D0BCFF" : "#6750A4"
        /** 主色上的文字（深色模式主色浅，故配深字）。 */
        readonly property color primaryForeground: root.dark ? "#381E72" : "#FFFFFF"
        /** 危险色（亮红）。 */
        readonly property color destructive: root.dark ? "#F87171" : "#EF4444"
        /** 危险色上的文字。 */
        readonly property color destructiveForeground: "#FAFAFA"
        /** 成功/在线（2026-10-29 补：shadcn 原生 token 有 success，规范色表漏列但语义必需）。 */
        readonly property color success: root.dark ? "#4ADE80" : "#16A34A"
        /** 警告/提醒（同上，shadcn 原生 token）。 */
        readonly property color warning: root.dark ? "#FBBF24" : "#D97706"
        /** 悬停高亮底。 */
        readonly property color accent: root.dark ? "#27272A" : "#F4F4F5"
        /** 禁用态：由主文字推透明度，不另存色值。 */
        readonly property color disabled: Qt.rgba(root.color.textPrimary.r,
                                                  root.color.textPrimary.g,
                                                  root.color.textPrimary.b, 0.38)
        /** 弹窗遮罩：50% 黑。 */
        readonly property color scrim: Qt.rgba(0, 0, 0, 0.5)
    }

    // ═══ 圆角（规范：卡片/按钮/弹窗统一 lg 12–16）═══
    readonly property QtObject radius: QtObject {
        readonly property int sm: 8
        readonly property int md: 10
        readonly property int lg: 14
        readonly property int xl: 20
        /** 药丸（FAB / 开关）。 */
        readonly property int full: 999
    }

    // ═══ 间距（规范：严格 8 的倍数）═══
    readonly property QtObject spacing: QtObject {
        readonly property int xs: 4
        readonly property int sm: 8
        readonly property int md: 16
        readonly property int lg: 24
        readonly property int xl: 32
    }

    // ═══ 动效（果冻质感）═══
    readonly property QtObject motion: QtObject {
        readonly property int fast: 150        // hover 变色
        readonly property int normal: 300      // 按下回弹
        readonly property int slow: 400        // 展开/收起
        readonly property real hoverScale: 1.02
        readonly property real pressScale: 0.95
        /** 果冻曲线 cubic-bezier(.34,1.7,.64,1)，配 Easing.Bezier 用。 */
        readonly property var jellyCurve: [0.34, 1.7, 0.64, 1]
        /** 展开/收起曲线（Easing.OutCubic 等价）。 */
        readonly property var expandCurve: [0.33, 0.0, 0.67, 1]
    }

    // ═══ 阴影（规范：极柔和，禁浓重黑边）═══
    readonly property QtObject shadow: QtObject {
        /** 阴影颜色：4% 黑。 */
        readonly property color soft: Qt.rgba(0, 0, 0, 0.04)
        readonly property int blur: 20
        readonly property int yOffset: 4
    }
}
