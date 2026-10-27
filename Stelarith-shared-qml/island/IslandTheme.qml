import QtQuick 2.15

// 灵动岛主题令牌（被控端岛专用）
//
// ⚠️ 本组件**不是 singleton**：本岛需要「深/浅两套」在运行期切换（跟随系统主题），
//    单例只能持有一份值。IslandRoot 里 new 两个实例（thLight / thDark）按需选。
//    （管理端 Theme.qml 也是同一做法；纯 pragma Singleton 无法深浅并存。）
//
// 色值来源：src/island.cpp 逐字照抄，改这里 = 改外观，务必与 C++ 版对比过再改。
//   · 严重度（island.cpp 556-558）
//   · 空闲态（island.cpp 644-689）：浅底 rgba(250,250,250,150) / 深底 rgba(20,20,20,140)
//   · 活动态（island.cpp 695-806）：fg/sub 两套
QtObject {
    id: theme

    // ── 深浅模式 ─────────────────────────────────────────────
    property bool light: false

    // ── 空闲半胶囊（island.cpp 644-689）─────────────────────
    property color idleBg:    light ? Qt.rgba(250/255, 250/255, 250/255, 150/255)
                                    : Qt.rgba( 20/255,  20/255,  20/255, 140/255)
    // 自绘时前景直接用 fg；空闲态 C++ 用 cf 颜色 = 与活动态一致
    property color idleFg:    light ? "#161616" : "#FAFAFA"

    // ── 活动态（island.cpp 695-806）─────────────────────────
    property color bg:        light ? "#F5F5F5" : "#1E1E1E"
    property color fg:        light ? "#161616" : "#FAFAFA"
    property color sub:       light ? "#6E6E6E" : "#C8C8C8"

    // ── 严重度（island.cpp 556-558，逐字；2026-10-13 urgent 归 MD3 error 色系）──
    // MD3 唯一模板（用户 2026-10-13 批准）：urgent 对齐 baseline error #B3261E。
    // inform（琥珀）/ remind（绿）为协议语义档，MD3 baseline 未定义语义状态色，保留原值。
    property color severityUrgent: Qt.rgba(179/255, 38/255, 30/255, 210/255)
    property color severityInform: Qt.rgba(200/255, 140/255, 10/255, 205/255)
    property color severityRemind: Qt.rgba(20/255, 140/255, 60/255, 205/255)
    property color severityNone:   "transparent"

    // ── 品牌强调色（进度条 / 下载态等，#198 文件传输进度用）──
    // 2026-10-13 MD3 唯一模板：accent 由系统蓝 #0A84FF/#2997FF 改 MD3 primary
    // #6750A4（浅）/ #D0BCFF（深）。island.cpp 无 accent（只 QML 侧用）。
    property color accent:    light ? "#6750A4" : "#D0BCFF"

    function severityColor(s) {
        if (s === "urgent") return severityUrgent
        if (s === "inform") return severityInform
        if (s === "remind") return severityRemind
        return severityNone
    }

    // ── 字号（项目 UI 规范：12 注 / 13 正 / 14 徽标 / 16 标题）──
    property int fsNote:    12   // 描述小字
    property int fsBody:    13   // 正文字号（空闲时钟 13px Medium，对齐 island.cpp）
    property int fsBadge:   14   // 徽标 / 展开列表标题
    property int fsTitle:   15   // 收起态标题（island.cpp 15px bold）/ 17 展开态
    // 展开态标题自适应档位（15 → 64px，按字数收敛，见 IslandRoot 的 fitTitlePx）
    property int fsTitleXs:  15   // 长标题（≥33 字）
    property int fsTitleSm:  18   // 28–32 字
    property int fsTitleMd:  24   // 17–27 字
    property int fsTitleLg:  36   // 9–16 字
    property int fsTitleXl:  48   // 5–8 字
    property int fsTitleXxl: 64   // 1–4 字（最大，短通知/大数字）
    // 展开态描述的"大字档"（# 标题行用），比正文大一号
    property int fsBodyLg:   15

    // ── 形态（island.h Shape/Form 对齐）─────────────────────
    // ⚠️⚠️ 三档高度是**复刻值**，真源在 `control-qt/src/island.h::enum Shape`
    //     （`{ Idle = 44, Collapsed = 56, Expanded = 320 }`）—— 被控端尺寸由
    //     C++ `IslandOverlay::geometryForContent/geometryForForm` 定，QML 只拿它们当布局预算。
    //     2026-10-16（U 系列 D 档）修正历史漂移：idle 曾是 36、expanded 曾是 168
    //     （管理端旧 DynamicIsland 时代的残留值），与 C++ 分叉；其中 `shapeExpanded`
    //     当时全仓零消费，属死令牌。两端同值由 `scripts/test-island-metrics.mjs` 机器盯。
    //     `shapeIdle` 另有 4:3 约束：`shapeIdle * 3 === kIdleW(132)`（见 IslandRoot.implicitWidth）。
    property int shapeIdle:      44
    property int shapeCollapsed: 56
    property int shapeExpanded:  320

    // ── 间距（2026-10-27 · 统一布局规范：对齐 MD3 spacing 阶梯）──────────
    // ⚠️ 新增但**不改既有行为**：各 face 之前硬编码的 4/8/12/16/20 现统一引这里，
    //   调用方若不引这些令牌、继续用裸数字，渲染结果与改造前完全一致。
    property real spXS: 4
    property real spS:  8
    property real spM:  12
    property real spL:  16
    property real spXL: 20

    // ── 圆角（2026-10-27 · 统一布局规范：对齐 IslandSurface 当前取值）────
    //   各 face 之前散落的 10/8/6 现统一引用，杜绝"同语义不同半径"。
    property real rCard:    10   // 独立容器（历史面板 / 卡片）
    property real rRow:     8    // 列表行
    property real rSection: 6    // 分组标签
    property real rPill:    999  // 胶囊 / 按钮 / chip

    // ── 列表 / 头部度量（2026-10-27 · 取代各面硬编码 48/32/18）─────────
    property real rowH:     48   // 列表单行高（标题+描述两行）
    property real headH:    32   // 面头部高
    property real sectionH: 18   // 分组标签高

    // ── 动效（2026-10-18 · 用户指令「统一动画时长为主题令牌」）──────────────
    // 之前只有 durShape/durFade 两个令牌，岛内另有 14 处**硬编码时长**
    // （60/75/80/90/120×6/160×2/180/200×2/600×2/1600×2）——
    // 改节奏要满岛找数字，且同一语义在不同文件里取不同值（120 出现在进度条、
    // 颜色过渡、宽高过渡三处，语义其实不同）。
    //
    // 现在按 **MD3 动效阶梯**（见 docs/MD3风格与排版标准-2026-10-18.md §1.6）建令牌：
    //   short 系列＝微交互（状态层/按键反馈/小位移）
    //   mid   系列＝常规过渡（颜色/进度/尺寸）
    //   long  系列＝大元素（几何形变/面板）
    // ⚠️ 旧名 durShape/durFade 保留为别名（存量零改动），语义映射见下。
    property int durInstant: 90    // 即时反馈（按下缩放等，短到"跟上手"）
    property int durXShort:  120   // 微过渡（进度条/颜色/小尺寸）
    property int durShort:   160   // 快速（透明度、悬停态）
    property int durMid:     200   // 常规（淡入淡出、状态切换）
    property int durLong:    380   // 几何形变（岛展开/收起，原 durShape）
    property int durXLong:   600   // 强调动作（弹幕滑入、大面板）

    // 旧名别名（迁移桥）—— 语义同 durLong / durMid
    property int durShape: durLong   // 几何缓动 380（管理端 DynamicIsland 同值）
    property int durFade:  durMid    // 淡入淡出 220 → 归到 mid 档 200
}
