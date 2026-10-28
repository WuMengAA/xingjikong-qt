import QtQuick

// 被控端 · shadcn 设计令牌（2026-10-28 用户裁决：「MD3 只用于管理端」）
//
// ⚠️ 这条口径**推翻了 2026-10-13 的「Md3 是两端唯一视觉模板」**：
//   · 管理端（viewer）继续用 Md3（shared-qml/ui/theme/Md3.qml + ui/md3/）
//   · 被控端（control）用本件 + qml/ash/ 的组件
//   两套并存是用户明确裁决，不是漂移。
//
// 规格来源（**逐条照抄，不是自创**）：
//   QtShadcn v0.1.2（MIT，github.com/QtShadcn/qtshadcn）
//   · 色板 = 源码 src/core/thememanager.cpp 的 light/dark 字典（标准 shadcn zinc）
//   · 主色默认 #18181b(浅) / #fafafa(深)，且前景按感知亮度自动取对比色（同文件 66-80 行）
//   · radius = 8（同文件 107/145 行）
//   · 输入框 h-9(36px) + rounded-md(6px) + 聚焦 ring 3px@0.3（ShadcnInput.qml 头注释）
//   · 开关 32×18、thumb 位移 tw-2、track 圆角 h/2（ShadcnSwitch.qml 22-68 行）
//
// ⚠️ 为什么手写而不直接引 QtShadcn：上游 49 件里 19 件 `import QtQuick.Controls.Basic`
//    并要求全局 QQuickStyle::setStyle("Basic")；被控端部署历史上因缺 QQC 运行库
//    整页空白过（ScrollBar is not a type），用户裁决「不引 Controls，只借色板手写」。
//    所以本件 + qml/ash/ 全部只用 QtQuick 基础类型，test-agent-qml-deps 不需要开例外。
//
// ⚠️ 本件**不是 singleton**：深浅两套要并存（跟随设置/系统），单例只能持一份值。
//    宿主 new 两个实例（ashLight / ashDark）按需注入，做法与 island/IslandTheme.qml 一致。
//
// ⚠️ QML 零逻辑：本件只有常量与色值；需要"算"的地方（主色对比前景、hover 混色）
//    一律在这里预先给好固定值，QML 侧只做绑定。
Item {
    id: t
    // ⚠️ 根类型必须是可视 Item（2026-10-28 实证）：二分 A 实测本件以 QtObject 为根时，
    // 一旦被实例化进设置页，整页渲染为空且 QML 层零告警（UiPageHost 静默吞）。
    // width/height 0 的 Item 不画任何东西，作为纯令牌载体无副作用。
    width: 0
    height: 0
    visible: false

    /** 深色模式。宿主创建两个实例后按设置选一个，本件不判断系统主题。 */
    property bool dark: false

    // ══ 颜色（zinc 语义层，逐字照 thememanager.cpp）═══════════════════════
    readonly property color background: dark ? "#09090b" : "#ffffff"
    readonly property color foreground: dark ? "#fafafa" : "#09090b"
    readonly property color card: dark ? "#09090b" : "#ffffff"
    readonly property color cardFg: dark ? "#fafafa" : "#09090b"
    readonly property color popover: dark ? "#09090b" : "#ffffff"
    readonly property color muted: dark ? "#27272a" : "#f5f5f5"
    readonly property color mutedFg: dark ? "#a1a1aa" : "#71717a"
    readonly property color accent: dark ? "#27272a" : "#f5f5f5"
    readonly property color accentFg: dark ? "#fafafa" : "#18181b"
    readonly property color border: dark ? "#27272a" : "#e4e4e7"
    readonly property color input: dark ? "#27272a" : "#e4e4e7"
    readonly property color ring: dark ? "#d4d4d8" : "#a1a1aa"

    /** 主色：shadcn 默认不是彩色，是"近黑(浅)/近白(深)"——这是它和 MD3 紫最大的区别。 */
    readonly property color primary: dark ? "#fafafa" : "#18181b"
    readonly property color primaryFg: dark ? "#18181b" : "#fafafa"
    /** hover 态：照 shadcn 的 primary/90 与 primary/80 预先算好，QML 里不 mix。 */
    readonly property color primaryHover: dark ? "#e4e4e7" : "#27272a"

    readonly property color secondary: dark ? "#27272a" : "#f4f4f5"
    readonly property color secondaryFg: dark ? "#fafafa" : "#18181b"
    readonly property color secondaryHover: dark ? "#3f3f46" : "#e4e4e7"

    readonly property color destructive: dark ? "#7f1d1d" : "#ef4444"
    readonly property color destructiveFg: dark ? "#fafafa" : "#fafafa"

    /** 业务语义（本项目需要，shadcn 只有 destructive）。取 zinc 系可读档。 */
    readonly property color warning: dark ? "#fbbf24" : "#b45309"
    readonly property color success: dark ? "#4ade80" : "#15803d"

    /** 卡片边框色：照 ShadcnCard.qml:44 —— foreground 加 alpha，不是直接 border 色。 */
    readonly property color cardBorder: Qt.rgba(foreground.r, foreground.g, foreground.b, 0.10)
    /** 半透明遮罩（Dialog 背景）：shadcn 用 black/50。 */
    readonly property color overlay: Qt.rgba(0, 0, 0, 0.50)

    // ══ 形状（radius: 8 派生）════════════════════════════════════════════
    // shadcn 的 scale：sm = r-4 / md = r-2 / lg = r / xl = r+4（globals.css 69-71 行）
    readonly property int radiusSm: 4
    readonly property int radiusMd: 6
    readonly property int radiusLg: 8
    readonly property int radiusXl: 12

    // ══ 尺寸（h-9 / h-10 等 Tailwind 档）══════════════════════════════════
    readonly property int controlH: 36          // h-9：输入框、按钮默认
    readonly property int controlHLg: 40        // h-10
    readonly property int controlHSm: 32        // h-8
    readonly property int rowPadH: 12           // px-3
    readonly property int cardPad: 16           // p-4
    readonly property int cardPadLg: 24         // p-6
    readonly property int gapSm: 8              // gap-2
    readonly property int gap: 12               // gap-3
    readonly property int gapLg: 16             // gap-4
    /** 设置页行高：shadcn 没有"设置行"组件，按 text-sm(20) + 上下 12 = 44 取，
     *  与开关行对齐（Switch 18 高居中）。 */
    readonly property int rowH: 44

    // ══ 字号（Tailwind 默认档）═══════════════════════════════════════════
    readonly property int fsXs: 12              // text-xs
    readonly property int fsSm: 14              // text-sm（正文，shadcn 应用层主力）
    readonly property int fsBase: 16            // text-base
    readonly property int fsLg: 18              // text-lg（卡片标题）
    readonly property int fs2xl: 24             // text-2xl（页面标题）
    readonly property string fontFamily: "Segoe UI"

    // ══ 动效（shadcn/Tailwind：150ms 为主，ColorAnimation 120-150）══════
    readonly property int durFast: 120
    readonly property int dur: 150
    readonly property int durSlow: 220

    // ══ 开关（ShadcnSwitch.qml 22-23 行）═════════════════════════════════
    readonly property int switchW: 32
    readonly property int switchH: 18
    readonly property int switchThumb: 14       // tw = _h - 4
    readonly property int switchThumbTravel: 14 // tw-2 位移量（_w - tw - gap）
}
