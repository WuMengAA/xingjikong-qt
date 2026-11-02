#!/usr/bin/env node
/**
 * UI 文案术语门禁（2026-10-20 立）
 *
 * ── 它守什么 ──────────────────────────────────────────────────────────
 * 用户在 2026-10-20 定了一套「教育行业 B 端管理系统」文案规范：对象统一叫「设备」，
 * 动作用「广播通知 / 文件分发 / 锁定桌面 / 屏幕监视 / 屏幕广播 / 远程关机 / 语音对讲」
 * 这类标准词，状态用「网络中断 / 授权失效 / 画面获取失败」这类不带情绪的说法。
 *
 * 但**规范写在文档里就等于没有**——上一个迭代刚统一，下一个迭代顺手就写成「电脑」。
 * 所以这里把 UiTerms.js 的禁用词表变成可执行断言：词一进代码就红，且必须给出替换建议。
 *
 * ── 为什么只扫字符串字面量 ────────────────────────────────────────────
 * 注释里为了讲清历史必须能写旧词（例如「以前叫教室机，容易误解」）。
 * 一刀切扫全文会让注释变成雷区，最终大家一起把门禁关掉 —— 那比没有门禁更糟。
 * 这里的做法：跳过整行注释、剥掉行尾 `//` 注释，再扫。
 *
 * 用法：node scripts/test-ui-terms.mjs
 * 退出码：0 全过 / 1 有违规
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPO = path.resolve(ROOT, "..");

function loadTerms() {
	const raw = fs.readFileSync(path.join(ROOT, "ui", "UiTerms.js"), "utf8");
	const src = raw.replace(/^\.pragma\b.*$/gm, "");
	return new Function(`${src}
return { objects, actions, states, sections, confirm, banned, scanSkipPaths };`)();
}

const T = loadTerms();

/** 要扫的源码根（相对仓库根） */
const SCAN_ROOTS = [
	{ dir: "Stelarith-viewer-qt/qml", exts: [".qml", ".js"] },
	{ dir: "Stelarith-control-qt/qml", exts: [".qml", ".js"] },
	{ dir: "Stelarith-shared-qml/ui", exts: [".qml", ".js"] },
	{ dir: "Stelarith-shared-qml/island", exts: [".qml", ".js"] },
];
/** 单文件目标（HTML 单独处理：注释语法不同） */
const SCAN_FILES = ["Stelarith-cloud/src/admin.html", "Stelarith-cloud/src/bind.html"];
/** 被控端 C++ 里的用户可见文案（托盘菜单 / 对话框 / 提示） */
const SCAN_CPP = ["Stelarith-control-qt/src/main.cpp", "Stelarith-control-qt/src/island/island.cpp",
	"Stelarith-control-qt/src/oobe/oobe_wizard.cpp"];

const SKIP = T.scanSkipPaths();

function walk(dir, exts, out = []) {
	for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, ent.name);
		if (ent.isDirectory()) {
			if (ent.name === "build" || ent.name === "node_modules" || ent.name === "deploy") continue;
			walk(p, exts, out);
		} else if (exts.includes(path.extname(ent.name))) {
			out.push(p);
		}
	}
	return out;
}

/** 剥掉注释（保留行号）：整行注释跳过，行尾双斜杠注释剥掉，块注释按行跨行处理。 */
function stripComments(text) {
	const lines = text.split(/\r?\n/);
	let inBlock = false;
	return lines.map((line) => {
		let s = line;
		if (inBlock) {
			const end = s.indexOf("*/");
			if (end < 0) return "";
			s = s.slice(end + 2);
			inBlock = false;
		}
		// 块注释可能在同一行开始并结束，也可能跨行
		for (;;) {
			const b = s.indexOf("/*");
			if (b < 0) break;
			const e = s.indexOf("*/", b + 2);
			if (e < 0) { s = s.slice(0, b); inBlock = true; break; }
			s = s.slice(0, b) + " ".repeat(e + 2 - b) + s.slice(e + 2);
		}
		const t = s.trimStart();
		if (t.startsWith("//")) return "";
		// 行尾注释：只在 `//` 前面不是 `:`（避免把 https:// 当注释）时剥
		const m = s.match(/\s\/\/\s/);
		if (m && m.index !== undefined) {
			const before = s.slice(0, m.index);
			if (!/:\s*$/.test(before)) s = before;
		}
		return s;
	});
}

/** HTML：只剥 <!-- --> 与 CSS 注释 */
function stripHtmlComments(text) {
	return text.replace(/<!--[\s\S]*?-->/g, (m) => m.replace(/[^\n]/g, " "))
		.replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
		.split(/\r?\n/);
}

const BANNED = T.banned();
const hits = [];

function scanFile(abs, { html = false } = {}) {
	const rel = path.relative(REPO, abs).replace(/\\/g, "/");
	if (SKIP.some((s) => rel.includes(s))) return;
	let text;
	try { text = fs.readFileSync(abs, "utf8"); } catch { return; }
	const lines = html
		// admin.html / bind.html 里内嵌 JS，所以 HTML 注释剥完还要再剥一遍 JS 行注释
		? stripComments(stripHtmlComments(text).join("\n"))
		: stripComments(text);
	lines.forEach((line, i) => {
		if (!line) return;
		for (const b of BANNED) {
			if (line.includes(b.word)) {
				hits.push({ rel, line: i + 1, word: b.word, to: b.to, why: b.why, text: line.trim().slice(0, 120) });
			}
		}
	});
}

console.log("UI 文案术语门禁（教育行业 B 端规范）\n");

for (const r of SCAN_ROOTS) {
	const abs = path.join(REPO, r.dir);
	if (!fs.existsSync(abs)) continue;
	for (const f of walk(abs, r.exts)) scanFile(f);
}
for (const f of SCAN_FILES) {
	const abs = path.join(REPO, f);
	if (fs.existsSync(abs)) scanFile(abs, { html: true });
}
for (const f of SCAN_CPP) {
	const abs = path.join(REPO, f);
	if (fs.existsSync(abs)) scanFile(abs);
}

// ── 断言 ──────────────────────────────────────────────────────────────
let pass = 0;
const fails = [];
function t(name, fn) {
	try { fn(); pass++; console.log(`  PASS  ${name}`); }
	catch (e) { fails.push(name); console.log(`  FAIL  ${name}\n        ${e.message}`); }
}
function ok(c, msg) { if (!c) throw new Error(msg || "断言失败"); }

console.log("── 一、术语表自身合法 ──");
t("禁用词表非空且每条都有 why/to", () => {
	ok(BANNED.length >= 10, `禁用词只有 ${BANNED.length} 条，太少`);
	for (const b of BANNED) {
		ok(b.word && b.why && b.to, `条目缺字段：${JSON.stringify(b)}`);
		ok(!/[、。，]/.test(b.word), `禁用词不应含标点：${b.word}`);
	}
});
t("动作名不含禁用词（否则术语表自相矛盾）", () => {
	const bad = Object.values(T.actions()).filter((v) => BANNED.some((b) => v.includes(b.word)));
	ok(bad.length === 0, `动作名里出现禁用词：${bad.join(" / ")}`);
});
t("核心动作名与用户规范逐字一致", () => {
	const want = {
		notify: "广播通知", filePush: "文件分发", lockScreen: "锁定桌面",
		monitor: "屏幕监视", screenCast: "屏幕广播", shutdown: "远程关机",
		restart: "远程重启", intercom: "语音对讲", forceOffline: "强制离线",
	};
	const a = T.actions();
	for (const [k, v] of Object.entries(want)) ok(a[k] === v, `${k} 应为「${v}」，实际「${a[k]}」`);
});
t("对象称呼与区块名符合规范", () => {
	const o = T.objects(), s = T.sections();
	ok(o.device === "设备", "objects.device 必须是「设备」");
	ok(!BANNED.some((b) => o.device.includes(b.word)), "设备称呼自身含禁用词");
	ok(s.settings === "系统设置", "设置区块应为「系统设置」");
	ok(s.deviceList === "设备列表", "设备区块应为「设备列表」");
	ok(s.opLog === "操作日志", "日志区块应为「操作日志」");
});

console.log("\n── 二、确认弹窗模板 ──");
t("模板包含台数与动作名，且以「是否确认」收尾", () => {
	const s = T.confirm(60, T.actions().lockScreen, true);
	ok(s.includes("60 台设备"), `缺台数：${s}`);
	ok(s.includes("【锁定桌面】"), `缺动作名：${s}`);
	ok(/是否确认？$/.test(s), `结尾不规范：${s}`);
	ok(s.startsWith("即将对"), `开头不规范：${s}`);
});
t("台数未知时退化为「全部在线设备」（不写 0 台）", () => {
	const s = T.confirm(0, T.actions().shutdown, true);
	ok(s.includes("全部在线设备"), s);
	ok(!s.includes("0 台"), s);
});

console.log("\n── 三、源码扫描 ──");
t("字符串文案中零禁用词", () => {
	if (hits.length === 0) return;
	// 先按文件聚合（长清单时一眼看出该先改哪个文件），再列前 25 条明细
	const byFile = new Map();
	for (const h of hits) byFile.set(h.rel, (byFile.get(h.rel) || 0) + 1);
	const summary = [...byFile.entries()].sort((a, b) => b[1] - a[1])
		.map(([f, n]) => `        ${String(n).padStart(3)}  ${f}`).join("\n");
	const shown = hits.slice(0, 25).map((h) =>
		`        ${h.rel}:${h.line}  「${h.word}」→ 应为「${h.to}」\n            ${h.text}`).join("\n");
	throw new Error(`共 ${hits.length} 处违规（${byFile.size} 个文件）：\n${summary}\n      —— 明细（前 25 条）——\n${shown}`);
});

console.log(`\n════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) {
	for (const f of fails) console.log("  ✗ " + f);
	process.exit(1);
}
process.exit(0);
