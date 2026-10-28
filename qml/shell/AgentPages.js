// 被控端 QML 控制面板的**页声明**（U5-A3 · 2026-10-16 ｜ 用途分流 2026-10-25）
//
// 与 viewer 的 `qml/UiPlugins.js` 是**同一份契约**（shared-qml/ui/UiContract.js）：
//   · `surfaces: ["page"]` + `entry`（缺 entry 会被 validate 报错）
//   · `order` 即导航顺序；`title`/`icon` 供底导航（Md3NavBar）显示
//   · 加一页 = 加一条声明 + 一个 entry 文件；壳（AgentShell.qml）零改动
//
// 2026-10-25：**用途（role）成为页清单的第一维过滤条件**。
//   用户裁决「独立界面要在 OOBE 阶段完成」—— 用途在向导第一步定，之后端侧据此决定
//   这台机器开哪套界面。它不是一个开关，而是页清单的过滤器：
//     classroom 教室机 —— 设备设置 / 自检 / 插件 / 记录 / 播放时段 / 广播社
//     broadcast 广播机 —— 广播台（首屏）/ 设置 / 自检 / 插件 / 播放时段 / 广播社
//   `records` 只给教室机：噪音与自习记录是班级现场的采集，广播机没有这层语义。
//
// 为什么 `roles` 缺省 = 两种都可见：向后兼容 —— 以后新加的机器级页（设置类）不必
// 每一页都记得标；只有**真正分用途**的页才写 roles。
.pragma library

/** 内部：单一份声明表（manifest() / manifestFor() 都从这里出，避免两套真源）。 */
function _all() {
    return [
        {
            id: "broadcastconsole",
            surfaces: ["page"],
            scope: ["agent"],
            order: 0,                 // 0 = 广播机首屏（排序后仍在最前）
            title: "广播台",
            icon: "radio",
            roles: ["broadcast"],     // 只有广播机有这一页
            entry: "pages/BroadcastConsolePage.qml"
        },
        {
            id: "settings",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！契约的 scope 默认是 ["viewer"]（UiContract.js:60）
                                  //    ⇒ 漏写会在 agent 端被门控全过滤（页与导航一起消失，
                                  //    2026-10-16 真机实证：白底 + 无底导航）
            order: 1,
            title: "设置",
            icon: "settings",          // 2026-10-28：改回 lucide 图标名（底导航换 SvgIcon 渲染）。
            // 旧方案是单汉字交给 Md3NavBar 的 Text 当字形画；那个 Text 没设 color，
            // 深色主题下黑字看不见（实机截图实证）。参考图也是图标。
            entry: "pages/AgentSettingsPage.qml"
        },
        {
            id: "selfcheck",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！（理由同上）
            order: 2,
            title: "自检",
            icon: "circle-check",
            entry: "pages/AgentSelfCheckPage.qml"
        },
        {
            id: "plugins",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！
            order: 3,
            title: "插件",
            icon: "square",
            entry: "pages/AgentPluginsPage.qml"
        },
        {
            id: "records",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！
            order: 4,
            title: "记录",
            icon: "folder",
            roles: ["classroom"],  // 噪音/自习记录 = 教室现场采集，广播机不参与
            entry: "pages/AgentRecordsPage.qml"
        },
        {
            id: "playschedule",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 必写！
            order: 5,
            title: "时段",
            icon: "hourglass",
            entry: "pages/AgentPlaySchedulePage.qml"
        },
        {
            id: "broadcastclub",
            surfaces: ["page"],
            scope: ["agent"],     // ⚠️ 同上：漏写会被门控全过滤
            order: 6,
            title: "广播社",
            icon: "users",
            // 广播社成员独立界面（2026-10-23 #广播站移交）：页内自带登录门，
            // 登录后只能看/排自己归属 station 的排期，不触及老师其它功能。
            entry: "pages/BroadcastClubPage.qml"
        }
    ];
}

/** 全量清单（不含用途过滤）—— 给门禁/契约校验用，保持既有调用不变。 */
function manifest() {
    return _all();
}

/**
 * 按用途取页清单。role 缺省/未知 → 教室机（与云端 normalizeDeviceRole 同口径）。
 * order 重新编号：过滤掉中间项后不留空档（底导航是按数组顺序画的）。
 */
function manifestFor(role) {
    // ⚠️ 归一必须与云端 registry.normalizeDeviceRole 逐字对齐：trim + lower。
    //    少一步 trim 就会出现"云端判广播机、端侧判教室机"的分裂
    //    （2026-10-25 探针实测：' broadcast ' 就命中了这个坑）——
    //    后果是广播机仍显示班级页、仍推缩略图，现场只看得到"怪事"看不到原因。
    const r = (String(role || "").trim().toLowerCase() === "broadcast") ? "broadcast" : "classroom";
    return _all()
        .filter(function (p) {
            const roles = p.roles || ["classroom", "broadcast"];
            return roles.indexOf(r) >= 0;
        })
        .sort(function (a, b) { return a.order - b.order; })
        .map(function (p, i) {
            const copy = {};
            for (const k in p) copy[k] = p[k];
            copy.order = i + 1;
            return copy;
        });
}
