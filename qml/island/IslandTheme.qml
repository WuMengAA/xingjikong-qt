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

    // ── 严重度（island.cpp 556-558，逐字）───────────────────
    property color severityUrgent: Qt.rgba(180/255, 20/255, 20/255, 210/255)
    property color severityInform: Qt.rgba(200/255, 140/255, 10/255, 205/255)
    property color severityRemind: Qt.rgba(20/255, 140/255, 60/255, 205/255)
    property color severityNone:   "transparent"

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
    property int shapeIdle:      36
    property int shapeCollapsed: 56
    property int shapeExpanded:  168

    // ── 动效 ────────────────────────────────────────────────
    property int durShape: 380   // 几何缓动（管理端 DynamicIsland 同值）
    property int durFade:  220
}
