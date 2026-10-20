// 星集控 · UI 文案术语真源（教育行业 B 端规范）
//
// ── 为什么要有这个文件（2026-10-20 立）──────────────────────────────────
// 用户点单：「从消费级软件的温和感，转向教育行业 B 端管理系统的权威感与规范感」。
// 但文案这件事最容易退化的形态是——**没人记得住规范，也没人会去查**：
//   · 上一版写「教室机」，下一版顺手写成「电脑」，再下一版变成「机器」；
//   · 同一个动作在三处叫三个名字（锁屏 / 锁屏教室机 / 锁屏选中的）；
//   · 断网提示一会儿「连接失败」、一会儿「网络异常」、一会儿「请检查网络」。
// 纯靠口头约定 = 必然漂移。所以把规范落成**代码里的真源 + 可执行的门禁**
// （scripts/test-ui-terms.mjs），任何禁用词进仓库就红。
//
// ── 三层结构 ──────────────────────────────────────────────────────────
//   ① 术语（objects / actions / states / sections）—— 应该用哪些词
//   ② 模板（confirm / 错误提示）—— 句子的骨架，保证"明确影响范围"
//   ③ 禁用词（banned）—— 不许出现哪些词，以及为什么、该换成什么
//
// ── 纪律 ──────────────────────────────────────────────────────────────
// · 改这里之前先想清楚：这是**跨端共享**的（管理端 viewer / 被控端 agent / 管理台 admin.html）。
// · 门禁只扫**字符串字面量**（不扫注释）：注释里为了讲清历史可以说旧词，用户界面不行。
// · 新增动作时，动作名必须从 actions 里取，不要在被调方就地再写一遍。

.pragma library

/** ①-1 对象称呼：不用"电脑"，不用"节点"。监控场景可称"学生端"。 */
function objects() {
    return {
        device: "设备",
        devices: "全部设备",
        student: "学生端",
        classroom: "教室"          // 场地（不是设备）时才用它
    };
}

/** ①-2 动作名（按钮/菜单/命令面板统一取这里）。 */
function actions() {
    return {
        notify:     "广播通知",
        filePush:   "文件分发",
        lockScreen: "锁定桌面",
        monitor:    "屏幕监视",
        screenCast: "屏幕广播",
        shutdown:   "远程关机",
        restart:    "远程重启",
        intercom:   "语音对讲",
        forceOffline: "强制离线",
        remoteControl: "远程控制",
        terminal:   "远程终端",
        application: "应用管理",
        screenshot: "屏幕截图"
    };
}

/** ①-3 状态与异常：精准、严密、不引发恐慌。 */
function states() {
    return {
        online:     "在线",
        offline:    "离线",
        connecting: "连接中断：正在尝试重新连接…",
        authExpired: "授权失效：登录凭证已过期，请重新绑定",
        cameraError: "采集设备异常：摄像头被占用或未连接",
        frameError:  "画面获取失败：网络延迟过高或设备未响应",
        netDown:     "网络中断",
        sessionExpired: "会话过期"
    };
}

/** ①-4 界面区块名（导航/标题）。 */
function sections() {
    return {
        settings:   "系统设置",
        deviceList: "设备列表",
        opLog:      "操作日志",
        overview:   "概览",
        monitor:    "屏幕监视",
        control:    "管控操作"
    };
}

/**
 * ④ 确认弹窗模板 —— **必须写明影响范围**，这是"权威感"的实际落点。
 * 老师说"我点了之后会发生什么"的答案，就靠这一句。
 * @param {number} n 受影响设备台数（0/未知时用"全部在线设备"）
 * @param {string} actionName 动作名（取自 actions()）
 * @param {boolean} irreversible 是否不可逆（电源类）
 */
function confirm(n, actionName, irreversible) {
    const scope = (typeof n === "number" && n > 0) ? (n + " 台设备") : "全部在线设备";
    const tail = irreversible ? "操作不可逆，" : "";
    return "即将对" + scope + "执行【" + actionName + "】，" + tail + "是否确认？";
}

/**
 * ③ 禁用词表 —— 门禁 test-ui-terms.mjs 的唯一依据。
 * 每条都要给出 why（为什么禁）与 to（换成什么），避免有人绕过时说不清理由。
 * ⚠️ 词表要**精确**：太短的词（如"设置"）会误伤"系统设置"，只收整词。
 */
function banned() {
    return [
        { word: "电脑", why: "消费级口语，B 端统一叫设备", to: "设备" },
        { word: "教室机", why: "内部黑话，家长/新老师看不懂", to: "设备" },
        { word: "机器", why: "口语，且与「设备」混用会让列表口径不一致", to: "设备" },
        { word: "节点", why: "底层 IT 黑话", to: "设备" },
        { word: "黑屏", why: "口语，未说明操作对象", to: "锁定桌面" },
        { word: "投屏", why: "消费级用法，教育行业叫屏幕广播", to: "屏幕广播" },
        { word: "看屏幕", why: "口语", to: "屏幕监视" },
        { word: "屏幕监看", why: "非标准措辞", to: "屏幕监视" },
        { word: "踢下线", why: "口语且带情绪", to: "强制离线" },
        { word: "登不上", why: "口语，未说清是凭证问题", to: "授权失效" },
        { word: "没画面", why: "口语且含糊", to: "画面获取失败" },
        { word: "网线断了", why: "过度具体，实际可能是任一层中断", to: "网络中断" },
        { word: "传文件", why: "口语", to: "文件分发" },
        { word: "发通知", why: "口语，未体现一对多", to: "广播通知" },
        { word: "摁住说话", why: "口语", to: "语音对讲" },
        { word: "学生机", why: "非规范称谓", to: "学生端" }
    ];
}

/**
 * ⑤ 门禁扫描白名单：这些位置里的中文不算用户可见文案。
 * 路径子串匹配（相对仓库根）。
 */
function scanSkipPaths() {
    return [
        "/fixtures/",
        "_archive/",
        ".bak",
        "_backup",
        "LayoutLab.qml",       // 设计实验室（内部调试页，不上老师桌面）
        "MaterialLab.qml",
        "ComponentCheck.qml",
        "Showcase.qml",
        "SamplePage.qml",
        "SamplePanel.qml",
        "ActionDefs.js",       // 它的 keys 里有拼音别名，单独由 test-action-manifest 守
        "UiTerms.js"           // 本文件就是禁用词表，必然"命中"自己
    ];
}
