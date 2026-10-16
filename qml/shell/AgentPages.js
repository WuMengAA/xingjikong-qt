// 被控端 QML 控制面板的**页声明**（U5-A3 · 2026-10-16）
//
// 与 viewer 的 `qml/UiPlugins.js` 是**同一份契约**（shared-qml/ui/UiContract.js）：
//   · `surfaces: ["page"]` + `entry`（缺 entry 会被 validate 报错）
//   · `order` 即导航顺序；`title`/`icon` 供底导航（Md3NavBar）显示
//   · 加一页 = 加一条声明 + 一个 entry 文件；壳（AgentShell.qml）零改动
//
// 被控端页面规划（用户已定 5 页）—— 已全部上齐：
//   设置(openInfoDialog 迁移) / 自检(runSelfCheck) / 插件市场(openPluginStore) /
//   记录(噪音+自习) / 播放时段(openPlayScheduleDialog)。
.pragma library

function manifest() {
    return [
        {
            id: "settings",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 1,
            title: "设置",
            icon: "设",          // 教室机 emoji 有 tofu 风险 ⇒ 导航图标用单字
            entry: "pages/AgentSettingsPage.qml"
        },
        {
            id: "selfcheck",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 2,
            title: "自检",
            icon: "检",
            entry: "pages/AgentSelfCheckPage.qml"
        },
        {
            id: "plugins",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 3,
            title: "插件",
            icon: "件",
            entry: "pages/AgentPluginsPage.qml"
        },
        {
            id: "records",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 4,
            title: "记录",
            icon: "录",
            entry: "pages/AgentRecordsPage.qml"
        },
        {
            id: "playschedule",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 5,
            title: "时段",
            icon: "播",
            entry: "pages/AgentPlaySchedulePage.qml"
        }
    ];
}
