#!/usr/bin/env node
/**
 * QML 结构性致命错误门禁（2026-10-29 立 · 用户点单「把页面静默加载不了的雷排干净」）
 *
 * ── 它守什么 ──────────────────────────────────────────────────────────────
 * 下面两类错误 qmllint **报 0 Error**，但运行时是「整页静默不加载 / 进程直接退出」：
 *
 *   1. 同一个 QML 对象上写两个 Component.onCompleted
 *      → Qt 报 "Property value set multiple times"，整页不加载
 *   2. 同一个作用域内两个同名 function
 *      → Qt 报 "Duplicate method name"，整页崩、进程退出
 *
 * 症状是最阴的一类：**构建全绿、进程正常起，就是那一页打不开** —— 老师只会觉得软件坏了。
 * 项目 MEMORY.md §5 有实证记录。本门禁把它焊成可执行断言。
 *
 * ── 为什么剥字符串，而不只是剥注释 ────────────────────────────────────────
 * 本门禁靠「花括号深度 + block id」判定「是不是同一个对象」。字符串字面量里的
 * 花括号（如 text: "{"）会把深度算错 ⇒ block id 错位 ⇒ 误报或漏报。
 * 故扫描前把**注释与字符串字面量**一并替换成等长空格（保留换行与列位，行号不变）。
 *
 * ── 为什么必须按 block id，不能按文件名 ──────────────────────────────────
 * 只按文件名判同名函数会大量假阳性：QML 里
 *     Connections { function onXxx(...) {} }
 * 是**按 Connections 对象作用域**的信号处理器，多个 Connections 各写一个同名处理器
 * 完全合法（viewer/Main.qml 就有两处 onResultReceived）；嵌套对象之间同理。
 * 只有「**同一 block 内**出现两次」才是真重复。
 *
 * 用法：node scripts/test-qml-structural.mjs
 * 退出码：0 无违例（可含仅基线债务） / 1 有新违例或基线回归 / 2 门禁自身故障
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BASE = path.resolve(ROOT, "..");

/** 只扫 .qml：这两类错误是 QML 对象结构特有的，.js 文件没有对象块概念。 */
const SCAN_ROOTS = [
	"Stelarith-viewer-qt/qml",
	"Stelarith-control-qt/qml",
	"Stelarith-shared-qml",
];

/** 命中键定义：id 用于基线 key */
const KINDS = {
	onCompleted: "DUP_onCompleted",
	function: "DUP_function",
};

function loadBaseline() {
	const p = path.join(ROOT, "scripts", "fixtures", "qml-structural-baseline.json");
	let j;
	try {
		j = JSON.parse(fs.readFileSync(p, "utf8"));
	} catch (e) {
		console.error("基线文件读取失败：" + p + "\n  " + e.message);
		process.exit(2);
	}
	const map = new Map();
	for (const k of j.known || []) map.set(k.file + " " + k.kind + " " + k.name, k.count);
	return { raw: j, map };
}

function walk(dir, out = []) {
	let ents;
	try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch { return out; }
	for (const ent of ents) {
		const p = path.join(dir, ent.name);
		if (ent.isDirectory()) {
			if (ent.name === "build" || ent.name === "node_modules" || ent.name === "deploy") continue;
			walk(p, out);
		} else if (path.extname(ent.name) === ".qml") {
			out.push(p);
		}
	}
	return out;
}

/**
 * 把注释与字符串字面量替换成等长空格（保留换行 ⇒ 行号不漂、花括号不误计）。
 * 状态机：逐字符处理行注释、块注释、双引号串、单引号串（含反斜杠转义）。
 */
function stripNoise(text) {
	const out = [];
	let i = 0;
	const n = text.length;
	while (i < n) {
		const c = text[i];
		const nx = i + 1 < n ? text[i + 1] : "";
		if (c === "/" && nx === "/") {
			while (i < n && text[i] !== "\n") { out.push(" "); i++; }
			continue;
		}
		if (c === "/" && nx === "*") {
			out.push(" ", " ");
			i += 2;
			while (i < n) {
				if (text[i] === "*" && i + 1 < n && text[i + 1] === "/") { out.push(" ", " "); i += 2; break; }
				out.push(text[i] === "\n" ? "\n" : " ");
				i++;
			}
			continue;
		}
		if (c === '"' || c === "'") {
			const q = c;
			out.push(" ");
			i++;
			while (i < n) {
				if (text[i] === "\\" && i + 1 < n) { out.push(" ", " "); i += 2; continue; }
				if (text[i] === q) { out.push(" "); i++; break; }
				out.push(text[i] === "\n" ? "\n" : " ");
				i++;
			}
			continue;
		}
		out.push(c);
		i++;
	}
	return out.join("");
}

const RE_ONC_AT = /^Component\s*\.\s*onCompleted\b/;
const RE_FN_AT = /^function\s+([A-Za-z_]\w*)\s*\(/;

/**
 * 逐字符单遍扫描（**不能按行取 block id**）。
 *
 * ⚠️ 为什么必须单遍、按位置取栈顶：
 *   若按「行首」取 block id，那么
 *       Connections { function onX() {} }
 *   这种**同一行**同时有对象开括号与函数声明的情况，取到的是**父块**而不是
 *   Connections 自己的块 ⇒ 两个不同 Connections 的同名服务被误判成重复（假阳性）。
 *   左到右逐字符处理，token 出现时刻的花括号状态才是真实的所属块。
 *
 * @param {string} rawText 原始 QML 文本
 * @returns {{kind:string,name:string,lines:number[]}[]}
 */
function analyzeText(rawText) {
	const s = stripNoise(rawText);
	const n = s.length;
	const stack = [0];          // 栈底 = 文件根（id 0）
	let nextId = 1;
	const onc = new Map();      // blockId -> [lineNo]
	const fn = new Map();       // blockId \u0000 name -> { name, lines }
	let line = 1;

	for (let i = 0; i < n;) {
		const ch = s[i];
		if (ch === "\n") { line++; i++; continue; }
		if (ch === "{") { stack.push(nextId); nextId++; i++; continue; }
		if (ch === "}") { if (stack.length > 1) stack.pop(); i++; continue; }

		if (ch === "C") {
			const head = s.slice(i, i + 48);
			const m = head.match(RE_ONC_AT);
			if (m) {
				const bid = stack[stack.length - 1];
				if (!onc.has(bid)) onc.set(bid, []);
				onc.get(bid).push(line);
				i += m[0].length;
				continue;
			}
		}
		if (ch === "f") {
			const head = s.slice(i, i + 96);
			const m = head.match(RE_FN_AT);
			if (m) {
				const bid = stack[stack.length - 1];
				const key = bid + "\u0000" + m[1];
				if (!fn.has(key)) fn.set(key, { name: m[1], lines: [] });
				fn.get(key).lines.push(line);
				i += m[0].length;
				continue;
			}
		}
		i++;
	}

	const out = [];
	for (const [, lns] of onc) {
		if (lns.length > 1) out.push({ kind: KINDS.onCompleted, name: "Component.onCompleted", lines: lns });
	}
	for (const [, v] of fn) {
		if (v.lines.length > 1) out.push({ kind: KINDS.function, name: v.name, lines: v.lines });
	}
	return out;
}

/** 读文件并分析 */
function analyze(abs) {
	let text;
	try { text = fs.readFileSync(abs, "utf8"); } catch { return []; }
	return analyzeText(text);
}

// ── 主流程 ──────────────────────────────────────────────────────────────
const base = loadBaseline();

console.log("QML 结构性致命错误门禁（整页不加载 / 进程退出）\n");

let pass = 0;
const fails = [];
function t(name, fnc) {
	try { fnc(); pass++; console.log(`  PASS  ${name}`); }
	catch (e) { fails.push(name); console.log(`  FAIL  ${name}\n        ${e.message}`); }
}

console.log("── 一、门禁自身 ──");
t("扫描根目录存在且至少一个是 .qml 目录", () => {
	let ok = 0;
	for (const r of SCAN_ROOTS) if (fs.existsSync(path.join(BASE, r))) ok++;
	if (ok === 0) throw new Error("没有任何扫描根存在（路径写错？）");
});
t("基线 JSON 合法且每项有 file/kind/name/count", () => {
	if (!Array.isArray(base.raw.known)) throw new Error("缺 known 数组");
	for (const k of base.raw.known)
		if (!k.file || !k.kind || !k.name || typeof k.count !== "number")
			throw new Error("基线项缺字段：" + JSON.stringify(k));
});
t("自证：同对象两个 onCompleted 能被认出", () => {
	const v = analyzeText('Item {\n  Component.onCompleted: {}\n  Component.onCompleted: {}\n}\n');
	const n = v.filter((x) => x.kind === KINDS.onCompleted).length;
	if (n !== 1) throw new Error("应识别出 1 处，实际 " + n);
});
t("自证：两个并列对象各一个 onCompleted 不算重复", () => {
	const v = analyzeText('Item {\n  A { Component.onCompleted: {} }\n  B { Component.onCompleted: {} }\n}\n');
	const n = v.filter((x) => x.kind === KINDS.onCompleted).length;
	if (n !== 0) throw new Error("不应报重复，实际 " + n);
});
t("自证：跨 Connections 的同名处理器不算重复（同行写法也要对）", () => {
	const v = analyzeText('Item {\n  Connections { function onX() {} }\n  Connections { function onX() {} }\n}\n');
	const n = v.filter((x) => x.kind === KINDS.function).length;
	if (n !== 0) throw new Error("跨对象同名不应算重复，实际 " + n);
});
t("自证：同一对象内同名函数能认出", () => {
	const v = analyzeText('Item {\n  function go() {}\n  function go() {}\n}\n');
	const n = v.filter((x) => x.kind === KINDS.function).length;
	if (n !== 1) throw new Error("应识别出 1 处，实际 " + n);
});
t("自证：字符串与注释里的花括号/关键字不干扰", () => {
	const v = analyzeText('Item {\n  // Component.onCompleted\n  text: "{"\n  other: "function fake(" \n  Component.onCompleted: {}\n}\n');
	if (v.filter((x) => x.kind === KINDS.onCompleted).length !== 0)
		throw new Error("注释/字符串里的关键字被误计");
});

const known = [];
const news = [];
const regress = [];
const scannedFiles = [];

for (const r of SCAN_ROOTS) {
	const abs = path.join(BASE, r);
	if (!fs.existsSync(abs)) continue;
	for (const f of walk(abs)) {
		scannedFiles.push(f);
		const rel = path.relative(BASE, f).replace(/\\/g, "/");
		for (const v of analyze(f)) {
			const key = rel + " " + v.kind + " " + v.name;
			const expected = base.map.get(key);
			const rec = { rel, kind: v.kind, name: v.name, lines: v.lines, expected };
			if (expected === undefined) news.push(rec);
			else if (v.lines.length > expected) regress.push(rec);
			else known.push(rec);
		}
	}
}

console.log(`\n  （扫描 ${scannedFiles.length} 个 .qml）`);

console.log("\n── 二、已知债务（基线，不阻断，待修）──");
if (known.length === 0) console.log("  （无）");
for (const k of known)
	console.log(`  · ${k.rel}  [${k.kind}] ${k.name} ×${k.lines.length} 行: ${k.lines.join(", ")}`);

console.log("\n── 三、新增违例（阻断）──");
if (news.length === 0) console.log("  （无）");
for (const n of news)
	console.log(`  ✗ ${n.rel}  [${n.kind}] ${n.name} ×${n.lines.length} 行: ${n.lines.join(", ")}`);
for (const r of regress)
	console.log(`  ✗ 基线回归 ${r.rel} [${r.kind}] ${r.name} 基线 ${r.expected} → 实际 ${r.lines.length}`);

const okAll = fails.length === 0 && news.length === 0 && regress.length === 0;
console.log(`\n════ 扫描 ${scannedFiles.length} 文件 / 自检 ${pass} 项 / 已知债务 ${known.length} / 新增违例 ${news.length} ════`);
if (!okAll) {
	console.log("  ✗ 门禁失败（exit 1）。");
	console.log("    DUP_onCompleted → 同一对象只能有一个 Component.onCompleted，合并成一个。");
	console.log("    DUP_function    → 同一作用域内函数改名；若确为跨嵌套作用域（如多个 Connections），");
	console.log("                      请在 fixtures/qml-structural-baseline.json 登记并经点单。");
	process.exit(1);
}
console.log("  ✓ 无新增违例。");
process.exit(0);
