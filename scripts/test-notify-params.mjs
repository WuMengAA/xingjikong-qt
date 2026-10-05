#!/usr/bin/env node
/**
 * notify 参数拼装的回归测试（2026-10-06 立）。
 *
 * 为什么有它：管理端「通知」的参数必须与被控端 `notifyFromParams()` 严格对齐，
 * 而"对齐"这件事靠肉眼看 QML 是看不出来的 —— 少一个键、多一个键、把 severity 发给了
 * 非全屏，全都能编过、也都能跑，只在真机上表现为"教室机没反应"或"配色没生效"。
 * 这个沙箱里 Qt GUI 跑不稳，所以把拼装抽成纯函数（qml/NotifyParams.js），用 node 直接断言。
 *
 * 用法：node scripts/test-notify-params.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 这里断言的是**契约**，不是实现细节。契约变了（被控端 notifyFromParams 改了），
 *    这个文件要跟着改 —— 它红了就说明两端有分歧，别把它当噪声关掉。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const JS = path.join(ROOT, "qml", "NotifyParams.js");

// 把 QML 的 .js（.pragma library + 顶层函数声明）当普通脚本读进来。
// 只有 `.pragma library` 这一行是 QML 专有语法（node 会报 Unexpected token '.'），剥掉即可；
// 其余必须是**纯 JS** —— 如果哪天有人往里写了 QML 专有语法，这里会立刻语法错，正好当门禁。
const raw = fs.readFileSync(JS, "utf8");
const src = raw.replace(/^\.pragma\b.*$/gm, "");
const stripped = raw.split(/\r?\n/).filter((l) => /^\.pragma\b/.test(l)).length;
if (stripped !== 1) throw new Error(`expected exactly one .pragma line, found ${stripped}`);
const load = new Function(`${src}\nreturn { buildNotifyParams, normalizeKind, willTruncate, CAP_TITLE, CAP_CONTENT };`);
const NP = load();

let pass = 0;
const fails = [];
function t(name, fn) {
	try {
		fn();
		pass++;
		console.log(`  PASS  ${name}`);
	} catch (e) {
		fails.push(name);
		console.log(`  FAIL  ${name}\n        ${e.message}`);
	}
}
function eq(actual, expected, label) {
	const a = JSON.stringify(actual);
	const b = JSON.stringify(expected);
	if (a !== b) throw new Error(`${label || "值"} 不符\n        期望 ${b}\n        实得 ${a}`);
}
function ok(cond, msg) {
	if (!cond) throw new Error(msg);
}

console.log(`\nnotify 参数拼装回归（${path.relative(ROOT, JS)}）\n`);

// ── 1. 常量与被控端一致 ──────────────────────────────────────────────────────
// 这三个数改错了，界面上显示的"24/64"就跟实际截断对不上，老师会以为整段发过去了。
t("截断上限 = 被控端 kTitleCap/kContentCap（24 / 64）", () => {
	eq(NP.CAP_TITLE, 24, "CAP_TITLE");
	eq(NP.CAP_CONTENT, 64, "CAP_CONTENT");
});

// ── 2. 标题必填 ─────────────────────────────────────────────────────────────
t("标题为空 → 不产出 params（被控端会回 failed「notify 缺 title」）", () => {
	eq(NP.buildNotifyParams({ title: "" }).ok, false);
	eq(NP.buildNotifyParams({ title: "   " }).ok, false, "纯空白也算空");
	eq(NP.buildNotifyParams({}).ok, false, "缺字段也算空");
});

t("标题两端空白被 trim", () => {
	const r = NP.buildNotifyParams({ title: "  放学啦  " });
	eq(r.ok, true);
	eq(r.params.title, "放学啦");
});

// ── 3. 形态归一化（与被控端"未知值一律 popup"同口径）────────────────────────
t("三种形态原样透传", () => {
	for (const k of ["popup", "island", "fullscreen"]) {
		eq(NP.buildNotifyParams({ title: "x", kind: k }).params.kind, k);
	}
});
t("未知形态 → 回落 popup", () => {
	eq(NP.buildNotifyParams({ title: "x", kind: "banner" }).params.kind, "popup");
	eq(NP.buildNotifyParams({ title: "x", kind: "" }).params.kind, "popup");
	eq(NP.buildNotifyParams({ title: "x" }).params.kind, "popup", "缺省也是 popup");
});

// ── 4. severity：只对 fullscreen 生效，非全屏**根本不带这个键** ────────────
t("fullscreen 带 severity，未知值回落 remind", () => {
	eq(NP.buildNotifyParams({ title: "x", kind: "fullscreen", severity: "urgent" }).params.flags.severity, "urgent");
	eq(NP.buildNotifyParams({ title: "x", kind: "fullscreen", severity: "inform" }).params.flags.severity, "inform");
	eq(NP.buildNotifyParams({ title: "x", kind: "fullscreen", severity: "紫" }).params.flags.severity, "remind");
	eq(NP.buildNotifyParams({ title: "x", kind: "fullscreen" }).params.flags.severity, "remind", "缺省 remind");
});
t("非全屏**不带** severity 键（带一个被忽略的值会让人误以为被控端有 bug）", () => {
	ok(!("severity" in NP.buildNotifyParams({ title: "x", kind: "popup", severity: "urgent" }).params.flags), "popup 不该有 severity");
	ok(!("severity" in NP.buildNotifyParams({ title: "x", kind: "island", severity: "urgent" }).params.flags), "island 不该有 severity");
});

// ── 5. seconds：>0 才带 ────────────────────────────────────────────────────
t("seconds=0 / 空 / 非数 → 整个键不出现（= 被控端按字数自适应）", () => {
	for (const v of [0, "", "0", "-3", "abc", undefined, null]) {
		ok(!("seconds" in NP.buildNotifyParams({ title: "x", seconds: v }).params), `seconds=${JSON.stringify(v)} 不该带键`);
	}
});
t("seconds>0 原样带；超 3600 夹到 3600（与被控端 qMin 一致）", () => {
	eq(NP.buildNotifyParams({ title: "x", seconds: 8 }).params.seconds, 8);
	eq(NP.buildNotifyParams({ title: "x", seconds: "12" }).params.seconds, 12, "字符串数字也认");
	eq(NP.buildNotifyParams({ title: "x", seconds: 9999 }).params.seconds, 3600);
});

// ── 6. speech / emergency ──────────────────────────────────────────────────
t("flags.speech 永远显式给出（false 也要有，便于回执分辨「刻意不读」而非「忘了设」）", () => {
	eq(NP.buildNotifyParams({ title: "x", speech: false }).params.flags.speech, false);
	eq(NP.buildNotifyParams({ title: "x", speech: true }).params.flags.speech, true);
	ok("speech" in NP.buildNotifyParams({ title: "x" }).params.flags, "缺省时也必须存在");
});
t("emergency_confirm 只在真的勾了时才带", () => {
	ok(!("emergency_confirm" in NP.buildNotifyParams({ title: "x" }).params.flags));
	ok(!("emergency_confirm" in NP.buildNotifyParams({ title: "x", emergency: false }).params.flags));
	eq(NP.buildNotifyParams({ title: "x", emergency: true }).params.flags.emergency_confirm, true);
});

// ── 7. 不发 notice_id（站点侧对账专用，管理端没有那张表）──────────────────
t("永不产出 notice_id / 顶层 emergency_confirm", () => {
	const p = NP.buildNotifyParams({ title: "x", notice_id: 123, emergency_confirm: true, emergency: true }).params;
	ok(!("notice_id" in p), "不该有 notice_id");
	ok(!("emergency_confirm" in p), "顶层不该有 emergency_confirm（走 flags）");
	ok(!("uid" in p), "uid 由 sendAction 加，params 里不该有");
	ok(!("action" in p), "action 由 sendAction 加，params 里不该有");
});

// ── 8. 顶层键的完整形状（多一个少一个都要被这条抓住）──────────────────────
t("params 顶层键集合 = kind/title/content/flags", () => {
	eq(Object.keys(NP.buildNotifyParams({ title: "x" }).params).sort(), ["content", "flags", "kind", "title"]);
});
t("flags 键集合 = speech（非全屏）/ speech+severity（全屏）", () => {
	eq(Object.keys(NP.buildNotifyParams({ title: "x", kind: "popup" }).params.flags).sort(), ["speech"]);
	eq(Object.keys(NP.buildNotifyParams({ title: "x", kind: "fullscreen" }).params.flags).sort(), ["severity", "speech"]);
	eq(Object.keys(NP.buildNotifyParams({ title: "x", kind: "fullscreen", emergency: true }).params.flags).sort(), ["emergency_confirm", "severity", "speech"]);
});

// ── 9. 超长不在这里截断（截断是被控端的职责，界面只做计数）────────────────
t("超 24/64 字仍原样发出，只标记会被截断（截断由被控端做）", () => {
	const long = "啊".repeat(50);
	const r = NP.buildNotifyParams({ title: long, content: long });
	eq(r.params.title.length, 50, "不截断");
	eq(NP.willTruncate(long, NP.CAP_TITLE), true);
	eq(NP.willTruncate("短", NP.CAP_TITLE), false);
	eq(NP.willTruncate("啊".repeat(24), NP.CAP_TITLE), false, "正好 24 字不算超（被控端是 >cap 才截）");
	eq(NP.willTruncate("啊".repeat(25), NP.CAP_TITLE), true);
});

// ── 10. content 缺省为空串（不是 undefined，免得被控端拿到 undefined）──────
t("content 缺省 = 空串", () => {
	eq(NP.buildNotifyParams({ title: "x" }).params.content, "");
	eq(NP.buildNotifyParams({ title: "x", content: undefined }).params.content, "");
});

console.log(`\n结果：PASS=${pass} FAIL=${fails.length}`);
if (fails.length) {
	console.log("失败项：");
	for (const f of fails) console.log(`  · ${f}`);
	process.exit(1);
}
console.log("全部通过 ✓\n");
