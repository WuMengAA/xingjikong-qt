// notify 指令的参数拼装 —— **纯函数，不碰 UI、不碰网络**。
//
// 为什么单独一个文件：契约对不对，靠肉眼看 QML 是看不出来的。抽成纯函数之后，
// scripts/test-notify-params.mjs 可以用 node 直接跑断言（见该脚本），
// 不需要起 Qt、不需要点界面 —— 在这个"GUI 跑不稳"的沙箱里这是唯一能真跑的回归。
//
// 契约来源（**别凭记忆改，去读**）：control-qt `src/main.cpp` 的 notifyFromParams()
//   action = "notify"
//   params = {
//     kind:    "popup" | "island" | "fullscreen"   // 未知/缺省 → 被控端一律按 popup 处理
//     title:   string                              // 必填；空 → 被控端回 failed "notify 缺 title"
//     content: string
//     seconds: int                                 // **>0 才带**；不带 = 被控端按字数自适应
//     flags: { speech: bool, severity?: "remind"|"inform"|"urgent", emergency_confirm?: bool }
//   }
//
// ⚠️ 不发 notice_id：那是站点侧对账用的（站点有 notice_kinds 表 + receiptsByNotice()），
//    管理端没有这张表，带了也没人认。
.pragma library

// 与被控端 NotifyWindow 的 kTitleCap / kContentCap 一致。改这三个数要同步改那边。
var CAP_TITLE = 24;
var CAP_CONTENT = 64;
var MAX_SECONDS = 3600;

var KINDS = ["popup", "island", "fullscreen"];
var SEVERITIES = ["remind", "inform", "urgent"];

/** 形态归一化：未知值一律 popup —— 与被控端 notifyFromParams() 的安全默认同口径。 */
function normalizeKind(kind) {
    return (KINDS.indexOf(String(kind)) >= 0) ? String(kind) : "popup";
}

/**
 * 拼一条 notify 的 params。
 *
 * @param input {{
 *   kind: string, title: string, content: string,
 *   seconds: number|string, speech: bool, severity: string, emergency: bool
 * }}
 * @returns {{ok: true, params: object} | {ok: false, error: string}}
 */
function buildNotifyParams(input) {
    var d = input || {};
    var title = String(d.title === undefined || d.title === null ? "" : d.title).trim();
    if (title === "") return { ok: false, error: "标题必填" };

    var kind = normalizeKind(d.kind);

    // flags 永远带上 speech（哪怕 false）——被控端读 flags.speech，缺省视为 false，
    // 但我们显式给出来，回执/日志里能看出"这条是刻意不朗读"而不是"忘了设"。
    var flags = { speech: d.speech === true };

    // severity 只对 fullscreen 生效（弹窗/灵动岛本来就是深色半透明，换配色无意义）。
    // 所以非全屏时**根本不带这个键**，而不是带一个被忽略的值 —— 免得以后有人看到
    // 参数里明明有 severity 却不起作用，反过来怀疑被控端有 bug。
    if (kind === "fullscreen") {
        flags.severity = (SEVERITIES.indexOf(String(d.severity)) >= 0) ? String(d.severity) : "remind";
    }
    if (d.emergency === true) flags.emergency_confirm = true;

    var params = { kind: kind, title: title, content: String(d.content || ""), flags: flags };

    // seconds：**>0 才带**。0/空/非数 → 整个键不出现（被控端据此走"按字数自适应"）。
    // 上限 3600 与被控端 qMin(m_seconds, 3600) 一致，这里先夹一次，免得界面上写着 9999
    // 实际只生效 3600 而没人知道。
    var sec = parseInt(d.seconds, 10);
    if (isFinite(sec) && sec > 0) params.seconds = Math.min(sec, MAX_SECONDS);

    return { ok: true, params: params };
}

/** 标题/正文是否会被被控端截断（界面上给字数计数用，口径与被控端一致）。 */
function willTruncate(text, cap) {
    return String(text === undefined || text === null ? "" : text).length > cap;
}
