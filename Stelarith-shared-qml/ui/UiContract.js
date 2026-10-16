// 星集控 · 界面插件声明契约（唯一真源 · 纯函数）
// ───────────────────────────────────────────────────────────────────────────
// 用户方向（2026-10-16）：「插件化思维，统一界面结构」。
//
// 病根：viewer 的 Main.qml（3858 行）把「有哪些界面、怎么排」写死在代码里 ——
//   页 3 个（StackLayout + root.page）、面板 14 个（openOnly 的 14+14 行 if-else）、
//   设备动作 39 条（同一份清单在右侧按钮与命令面板**各维护一遍**）。
// 目标：界面单元自己**声明**自己（本文件定义的形状），外壳读注册表自动装配。
//
// 为什么单独一个纯函数 .js（照 NotifyParams.js 的成例）：
//   契约对不对，肉眼看 QML 看不出来。抽成纯函数后 scripts/test-ui-contract.mjs
//   能用 node 直接跑断言，不需要起 Qt、不需要点界面 —— 这个沙箱里唯一能真跑的回归。
//
// 权限词表**与后端同源**（不是另立一套）：真源＝
//   control-qt/src/plugin/pluginhost.cpp 的 PluginHost::kKnownPermissions
//   以及 StelarithPluginSDK.h 的 enum Permission。改那边要同步改这里。
//   ⇒ 第三方插件与内置界面单元走**同一条**门控。
.pragma library

// ── 扩展面：一个界面单元可以占的位置（可多占）──────────────────────────
//   page    整屏页（对应现在的 StackLayout 一页）
//   panel   浮层/弹窗（对应现在的 openOnly 那 14 个）
//   action  针对某台设备的动作（对应现在的 39 条按钮）
//   command 命令面板可检索条目
//   island  灵动岛能力展示（permissions 里的 island）
var SURFACES = ["page", "panel", "action", "command", "island"];

// ── 适用端 ──
var SCOPES = ["viewer", "agent", "both"];

// ── 权限词表：与 PluginHost::kKnownPermissions 逐字一致 ──
var PERMISSIONS = [
    "island",           // 灵动岛展示（island_show）
    "notify",           // 通知（notify）
    "settings",         // 读/写本地设置
    "events:noise",     // 订阅噪音采样
    "events:schedule",  // 订阅课表变更
    "cmd:exec"          // 经 agent 执行本地命令（高危，预留）
];

// ── 动作的分派方式（action 面）────────────────────────────────────────
//   send     发一条指令给设备（backend.sendAction(action)）
//   panel    开一个浮层（openOnly(panel)）
//   special  需要多步/条件的分支（语音、屏幕广播、考试、软件…）⇒ 走宿主的 SPECIAL 表
//   app      本地应用命令（切页 / 换主题 / 换角色 / 登录 / 开外链）
// 声明**纯数据**：执行只在宿主的 runActionById() 一处收敛 —— 这样清单能用 node 回归，
// 而且「同一动作在右栏按钮与命令面板走两条路」这个老病根被彻底掐掉。
var KINDS = ["send", "panel", "special", "app"];

// app 类的子类型（kind === "app" 时用；payload 为字符串参数）
//   page 切到第 N 页 ／ theme 换外观 ／ role 换身份 ／ login 走站点登录 ／ external 开外链
var APPS = ["page", "theme", "role", "login", "external"];

// ── 字段定义：名 / 类型 / 默认值 ──
// 类型只用于归一（"list" 允许写单个字符串，自动包成数组）。
var FIELDS = [
    { name: "id",       type: "string", dflt: "" },
    { name: "title",    type: "string", dflt: "" },
    { name: "icon",     type: "string", dflt: "" },
    { name: "scope",    type: "list",   dflt: ["viewer"] },
    { name: "order",    type: "int",    dflt: 100 },
    { name: "surfaces", type: "list",   dflt: ["panel"] },
    { name: "perm",     type: "string", dflt: "" },
    { name: "caps",     type: "list",   dflt: [] },
    { name: "danger",   type: "bool",   dflt: false },
    { name: "admin",    type: "bool",   dflt: false },
    { name: "action",   type: "string", dflt: "" },
    { name: "entry",    type: "string", dflt: "" },
    { name: "keys",     type: "list",   dflt: [] },
    { name: "tip",      type: "string", dflt: "" },
    // ── 动作面专用（U4）────────────────────────────────────────────
    // role 与 perm 是**两套不同**的门控，别混：
    //   role  角色能力键 ⇒ backend.mayDo(role)（admin / lock / reboot / broadcast / terminal_open）
    //   perm  插件权限词表 ⇒ 与环境 permissions 比（见上）
    { name: "role",     type: "string", dflt: "" },
    { name: "kind",     type: "string", dflt: "send" },
    { name: "panel",    type: "string", dflt: "" },
    { name: "app",      type: "string", dflt: "" },
    { name: "payload",  type: "string", dflt: "" },
    // 右栏展示
    { name: "group",    type: "string", dflt: "" },   // 空 = 不进右栏按钮区
    { name: "wide",     type: "bool",   dflt: false },// 独占整行（电源那类）
    { name: "heavy",    type: "bool",   dflt: false },// 重描边（不可逆动作）
    // 概览页快捷行
    { name: "quick",    type: "bool",   dflt: false },
    { name: "phone",    type: "bool",   dflt: true }, // false = 桌面专属（手机端不显示）
    { name: "quickOrder", type: "int",  dflt: 999 },  // 快捷行内顺序（与 order 解耦）
    // 命令面板内顺序（同样与 order 解耦）。为什么要第三个排序键：
    //   右栏要求「通知」压最顶（老师用得最多），命令面板要求「锁屏教室机」排第一
    //   （现状如此，打开面板不搜时前 9 条就是它）—— 同一个动作不可能同时满足两边。
    { name: "palOrder",   type: "int",  dflt: 999 },
    // 同一动作在三个面的叫法本来就不一样（按钮短 / 命令面板长 / 快捷行口语），
    // **保留各自文案**——统一的是"清单只有一份"，不是"文案必须一样"。
    { name: "palName",    type: "string", dflt: "" }, // 命令面板名（空 = 用 title）
    { name: "quickTitle", type: "string", dflt: "" }, // 快捷行文案（空 = 用 title）
    // 执行前先切到第 N 页（-1 = 不切）。原来分散在 quickAction / 命令面板各处的手写
    // `root.page = 1` 收敛成这一个字段。
    { name: "page",     type: "int",    dflt: -1 },
    // ⚠️ 仅用于快捷行的一道额外 mayDo 检查。存在的唯一理由是**保留现状**：
    //   改造前「快捷行·锁屏」先判 permOk("lock") 再走 runAction（后者只判 "admin"，
    //   而 mayDo("admin") 因权限表里没有 "admin" 字样 ⇒ 恒真 ⇒ 形同不判）。
    //   也就是说：现状**只有快捷行**这道是真的在拦 teacher 身份，右栏与命令面板都拦不住。
    //   迁移必须逐字保持 —— 否则 teacher 身份下快捷行锁屏会由"被拦"变成"能点"（安全倒退）。
    //   三处口径不一致本身是笔待收敛的债，但不该在这次结构重构里顺手改掉（见归档 §11.5）。
    { name: "quickGuard", type: "string", dflt: "" },
    // 进「多选批量条」（左下角选中多台后出现的那排按钮）。
    { name: "bulk",     type: "bool",   dflt: false }
];

// 宿主注入给 entry（panel/page）的上下文对象字段。
// entry 里声明 `property var ctx` 即可拿到；声明 `property var args` 收 open(id, args) 的载荷。
//   window      主窗口（面板从大文件抽出后，靠它够到原来的 `root.xxx`）
//   theme       设计令牌
//   registry/host/scope  注册表、宿主、本端
//   open/close  跨面板跳转 / 关掉当前面板
//   closePanels 把面板全部关掉（原来直接调另一个面板的 close()，抽出后做不到）
var CTX_FIELDS = ["window", "theme", "registry", "host", "scope", "open", "close", "closePanels"];

// entry 必须实现的接口（宿主调用的只有这两个，外加可选的 openWith）
var ENTRY_API = ["open", "close"];

function isArray(v) {
    return Object.prototype.toString.call(v) === "[object Array]";
}

function has(arr, v) {
    return arr && arr.indexOf(v) >= 0;
}

/** 取字段定义（未知字段返回 null）。 */
function field(name) {
    for (var i = 0; i < FIELDS.length; i++)
        if (FIELDS[i].name === name) return FIELDS[i];
    return null;
}

/** 深拷贝一份默认值（数组要拷，不能共享同一引用 —— 否则一处改处处变）。 */
function defaults() {
    var o = {};
    for (var i = 0; i < FIELDS.length; i++) {
        var f = FIELDS[i];
        o[f.name] = isArray(f.dflt) ? f.dflt.slice(0) : f.dflt;
    }
    return o;
}

/** 归一化一条声明：补默认值 + 类型归一（不抛错，容错优先；错了由 validate 报）。 */
function normalize(raw) {
    var out = defaults();
    if (!raw || typeof raw !== "object") return out;
    for (var i = 0; i < FIELDS.length; i++) {
        var f = FIELDS[i];
        if (!(f.name in raw)) continue;
        var v = raw[f.name];
        if (f.type === "list") {
            if (isArray(v)) out[f.name] = v.slice(0);
            else if (v === null || v === undefined || v === "") out[f.name] = [];
            else out[f.name] = [v];              // "panel" → ["panel"]
        } else if (f.type === "bool") {
            out[f.name] = !!v;
        } else if (f.type === "int") {
            out[f.name] = (typeof v === "number") ? (v | 0) : (parseInt(v, 10) || 0);
        } else {
            out[f.name] = (v === null || v === undefined) ? "" : String(v);
        }
    }
    return out;
}

/** 归一化整个清单。 */
function normalizeAll(list) {
    var out = [];
    if (!isArray(list)) return out;
    for (var i = 0; i < list.length; i++) out.push(normalize(list[i]));
    return out;
}

/** 单条声明的问题清单（空数组 = 没问题）。 */
function validateOne(p) {
    var bad = [];
    if (!p.id) bad.push("缺 id");
    if (p.id && p.id !== p.id.toLowerCase()) bad.push("id 应全小写：" + p.id);
    if (p.scope.length === 0) bad.push("scope 为空：" + p.id);
    for (var i = 0; i < p.scope.length; i++)
        if (!has(SCOPES, p.scope[i])) bad.push("未知 scope：" + p.scope[i] + "（" + p.id + "）");
    if (p.surfaces.length === 0) bad.push("surfaces 为空：" + p.id);
    for (var j = 0; j < p.surfaces.length; j++)
        if (!has(SURFACES, p.surfaces[j])) bad.push("未知 surface：" + p.surfaces[j] + "（" + p.id + "）");
    if (p.perm && !has(PERMISSIONS, p.perm)) bad.push("未知 permission：" + p.perm + "（" + p.id + "）");
    if (p.kind && !has(KINDS, p.kind)) bad.push("未知 kind：" + p.kind + "（" + p.id + "）");
    if (p.kind === "app" && !has(APPS, p.app)) bad.push("app 面缺/错 app 子类型：" + p.id);
    if (p.kind === "panel" && !p.panel) bad.push("kind=panel 缺 panel：" + p.id);
    // 有画面的面必须给 entry；纯逻辑面（action/command/island）可以没有
    var needsEntry = has(p.surfaces, "panel") || has(p.surfaces, "page");
    if (needsEntry && !p.entry) bad.push("panel/page 面缺 entry：" + p.id);
    // 只有 kind=send 的 action 面才必须有指令名（panel/special/app 走别的通道）
    if (has(p.surfaces, "action") && p.kind === "send" && !p.action)
        bad.push("action 面缺 action 指令名：" + p.id);
    return bad;
}

/**
 * 这条声明要设备具备哪些指令能力才点得动（空数组 = 不参与能力门控）。
 * 与 viewer 原来那份 capNeedOf 同口径：
 *   显式 caps 优先 ＞ 否则用 action 指令名 ＞ 否则不门控。
 * 「音量/软件/定时任务/文件/通知/终端」这些按钮的 action 是空串，真正依赖哪几条指令
 * 必须单独点名（像「软件」要同时拿到候选列表**和**进程列表才算真能开）。
 */
function capNeeds(p) {
    if (!p) return [];
    if (p.caps && p.caps.length > 0) return p.caps;
    if (p.action) return [p.action];
    return [];
}

/** 按分组取（已过门控 + 已排序），group 为空的条目自然被排除。 */
function forGroup(list, group, env) {
    var out = [];
    for (var i = 0; i < list.length; i++)
        if (list[i].group === group && has(list[i].surfaces, "action") && visible(list[i], env))
            out.push(list[i]);
    return out;
}

/** 本端右栏用到的分组名（按声明里首次出现的 order 排序）。 */
function groups(list, env) {
    var seen = {}, out = [];
    var acts = forSurface(list, "action", env);
    for (var i = 0; i < acts.length; i++) {
        var g = acts[i].group;
        if (!g || seen[g]) continue;
        seen[g] = true;
        out.push(g);
    }
    return out;
}

/** 概览页快捷行（quick 的那些）。按 quickOrder 排 —— 与右栏的 order 解耦：
 *  同一个动作在右栏属于哪一组、在快捷行排第几，本来就是两件事。 */
function quickActions(list, env) {
    var out = [];
    var acts = forSurface(list, "action", env);
    for (var i = 0; i < acts.length; i++) if (acts[i].quick) out.push(acts[i]);
    out.sort(function (a, b) { return a.quickOrder - b.quickOrder; });
    return out;
}

/** 整个清单的问题清单（含 id 重复）。 */
function validate(list) {
    var bad = [];
    var seen = {};
    for (var i = 0; i < list.length; i++) {
        var p = list[i];
        if (p.id) {
            if (seen[p.id]) bad.push("id 重复：" + p.id);
            seen[p.id] = true;
        }
        var one = validateOne(p);
        for (var j = 0; j < one.length; j++) bad.push(one[j]);
    }
    return bad;
}

/**
 * 门控：这条声明在给定环境下是否可见。
 * env = { scope: "viewer"|"agent", permissions: [], caps: [] }
 *
 * ⚠️ 容错口径（照抄 viewer 既有 capOk 的教训，2026-10-06）：
 *   老云端/老被控端没有这些字段 ⇒ env 里的列表是空的 ⇒ **一律放行**。
 *   门控的目的是「少让人白点一下」，不是拿它当闸门把老机器锁死。
 */
function visible(p, env) {
    if (!p) return false;
    var sc = (env && env.scope) ? env.scope : "";
    if (sc && !has(p.scope, "both") && !has(p.scope, sc)) return false;

    var perms = (env && isArray(env.permissions)) ? env.permissions : [];
    if (p.perm && perms.length > 0 && !has(perms, p.perm)) return false;

    // 能力门控走 capNeeds：显式 caps 优先，否则用 action 指令名兜底。
    // ⚠️ 不能用 p.caps 直接判 —— 那样像 lock/reboot 这种「caps 没写但 action 有」的
    //    简单动作会**从门控里漏掉**（viewer 原实现 capNeedOf 就是 action 兜底的）。
    var need = capNeeds(p);
    var caps = (env && isArray(env.caps)) ? env.caps : [];
    if (need.length > 0 && caps.length > 0) {
        for (var i = 0; i < need.length; i++)
            if (!has(caps, need[i])) return false;
    }
    return true;
}

/** 按扩展面筛（已过门控 + 已排序）。 */
function forSurface(list, surface, env) {
    var out = [];
    for (var i = 0; i < list.length; i++)
        if (has(list[i].surfaces, surface) && visible(list[i], env)) out.push(list[i]);
    out.sort(function (a, b) { return a.order - b.order; });
    return out;
}

/** 按 id 查（不过门控 —— 调用方要能拿到被门控掉的条目去解释原因）。 */
function byId(list, id) {
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
    return null;
}
