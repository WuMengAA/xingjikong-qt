#!/usr/bin/env node
/**
 * 文件规模 / 上帝类门禁（2026-10-22 立 · 用户点单《全局统一设计规范》第 0 步）
 *
 * ── 它守什么 ──────────────────────────────────────────────────────────
 * 规范写了「.h ≤ 300 / .cpp ≤ 600 / .qml ≤ 200 行，超了必须拆」。
 * 但**红线写在文档里就等于没有**：下一个会话顺手在 8142 行的 main.cpp 里
 * 再加两个函数，谁也不会拦。所以这里把红线变成可执行断言。
 *
 * ── 为什么不直接全库判红 ──────────────────────────────────────────────
 * 现状是历史包袱（main.cpp 8142 行），一刀切判红 = 门禁第一天就全红，
 * 结局必然是「把门禁关掉」——那比没有门禁更糟（见 test-ui-terms.mjs 同款教训）。
 *
 * 这里的三段式策略：
 *   1. 白名单内 + 行数**没有变胖**      → warn（允许存在，不许再长）
 *   2. 白名单内 + 行数**比登记时变胖**  → error（防恶化：烂地基不许再叠叠乐）
 *   3. 白名单外 + 超线                  → error（新代码一律从严）
 *
 * ── 上帝类信号 ────────────────────────────────────────────────────────
 * 行数不是唯一病症：一个 500 行的文件塞 60 个 Q_INVOKABLE 同样是上帝类。
 * 所以附加两个信号：单文件 Q_INVOKABLE > 30 或 顶层定义 > 60 → 报信号。
 * 白名单内的老文件只报信号；新文件出现信号 = error。
 *
 * 用法：node scripts/test-file-size.mjs [--update-whitelist]
 *   --update-whitelist 把当前所有超限文件按现状重写白名单（仅在人工确认后使用）
 * 退出码：0 全过 / 1 有 error
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPO = path.resolve(ROOT, "..");
const WHITELIST = path.join(ROOT, "scripts", "fixtures", "oversize-legacy.json");

/** 红线（行 = 换行符数，与 wc -l 同口径） */
const LIMITS = { ".h": 300, ".cpp": 600, ".qml": 200, ".js": 600, ".mjs": 600 };
const LIMIT_NOTE = {
	".h": "规范：头文件 ≤ 300",
	".cpp": "规范：实现文件 ≤ 600",
	".qml": "规范：QML 组件 ≤ 200",
	".js": "本项目自定（规范未覆盖云端）",
	".mjs": "本项目自定（规范未覆盖云端）",
};

/** 上帝类信号阈值 */
const GOD = { qInvokable: 30, topLevel: 60 };

/** 扫描范围（相对仓库根） */
const SCAN = [
	{ dir: "Stelarith-control-qt/src", exts: [".h", ".cpp"] },
	{ dir: "Stelarith-viewer-qt/src", exts: [".h", ".cpp"] },
	{ dir: "Stelarith-control-qt/qml", exts: [".qml"] },
	{ dir: "Stelarith-viewer-qt/qml", exts: [".qml"] },
	{ dir: "Stelarith-shared-qml/ui", exts: [".qml"] },
	{ dir: "Stelarith-shared-qml/island", exts: [".qml"] },
	{ dir: "Stelarith-cloud/src", exts: [".js", ".mjs"] },
];

const SKIP_DIR = new Set(["build", "dist", "node_modules", ".git", "third_party"]);

function walk(dir, exts, out = []) {
	let ents;
	try {
		ents = fs.readdirSync(dir, { withFileTypes: true });
	} catch {
		return out;
	}
	for (const ent of ents) {
		const p = path.join(dir, ent.name);
		if (ent.isDirectory()) {
			if (!SKIP_DIR.has(ent.name)) walk(p, exts, out);
		} else if (exts.some((e) => ent.name.endsWith(e))) {
			out.push(p);
		}
	}
	return out;
}

function lineCount(file) {
	const raw = fs.readFileSync(file, "latin1");
	let n = 0;
	for (let i = 0; i < raw.length; i++) if (raw.charCodeAt(i) === 10) n++;
	// 末行无换行符时补 1（与 wc -l 的差异：wc 不补，这里补，取严）
	if (raw.length > 0 && !raw.endsWith("\n")) n++;
	return n;
}

function godSignals(file, ext) {
	if (!(ext === ".h" || ext === ".cpp")) return null;
	const src = fs.readFileSync(file, "utf8");
	const qInvokable = (src.match(/\bQ_INVOKABLE\b/g) || []).length;
	const topLevel = (src.match(/^(?:static\s+[\w:<>*&\s]+\w+\s*\(|void\s|bool\s|int\s|QString\s|qint64\s|class\s|struct\s|namespace\s)/gm) || []).length;
	if (qInvokable <= GOD.qInvokable && topLevel <= GOD.topLevel) return null;
	return { qInvokable, topLevel };
}

const whitelist = (() => {
	try {
		return JSON.parse(fs.readFileSync(WHITELIST, "utf8"));
	} catch {
		return { note: "无白名单：所有超限一律判红（首次落地时用它生成清单）", files: {} };
	}
})();

const scanned = [];
for (const { dir, exts } of SCAN) {
	for (const abs of walk(path.join(REPO, dir), exts)) {
		const rel = path.relative(REPO, abs).split(path.sep).join("/");
		const ext = path.extname(abs);
		const limit = LIMITS[ext];
		if (!limit) continue;
		const lines = lineCount(abs);
		scanned.push({ rel, ext, lines, limit, god: godSignals(abs, ext) });
	}
}

if (process.argv.includes("--update-whitelist")) {
	const files = {};
	for (const f of scanned) {
		if (f.lines > f.limit || f.god) {
			files[f.rel] = {
				lines: f.lines,
				limit: f.limit,
				qInvokable: f.god ? f.god.qInvokable : 0,
				topLevel: f.god ? f.god.topLevel : 0,
				plan: "待拆：填写拆分计划与责任人",
			};
		}
	}
	fs.writeFileSync(WHITELIST, JSON.stringify({ updated: new Date().toISOString().slice(0, 10), files }, null, 2) + "\n", "utf8");
	console.log("[门禁] 白名单已按现状重写：" + Object.keys(files).length + " 项 → " + path.relative(REPO, WHITELIST));
	process.exit(0);
}

const allowed = [];   // 白名单内未恶化
const grew = [];      // 白名单内但变胖
const fresh = [];     // 白名单外超线
const godFresh = [];  // 白名单外出现上帝类信号
const godLegacy = [];

for (const f of scanned) {
	const w = whitelist.files?.[f.rel];
	const over = f.lines > f.limit;
	const godNew = f.god && (!w || f.god.qInvokable > (w.qInvokable || 0) || f.god.topLevel > (w.topLevel || 0));
	if (w) {
		if (f.lines > w.lines) grew.push({ ...f, was: w.lines });
		else if (over) allowed.push(f);
		if (f.god) godLegacy.push({ ...f, plan: w.plan });
	} else {
		if (over) fresh.push(f);
		if (godNew) godFresh.push(f);
	}
}

const pad = (s, n) => (s + " ".repeat(n)).slice(0, n);
console.log("\n[门禁] 文件规模 / 上帝类 · " + new Date().toISOString().slice(0, 10));
console.log("扫描 " + scanned.length + " 个文件 ｜ 白名单 " + Object.keys(whitelist.files || {}).length + " 项");
console.log("-".repeat(78));

if (allowed.length) {
	console.log("① 历史超限·未恶化（白名单内，允许存在；再长 1 行即判红）：");
	for (const f of allowed.sort((a, b) => b.lines - a.lines).slice(0, 12)) {
		console.log("   ~ " + pad(f.rel, 52) + pad(String(f.lines), 6) + "行 / 红线 " + f.limit);
	}
	if (allowed.length > 12) console.log("   ... 其余 " + (allowed.length - 12) + " 项见白名单");
}
if (grew.length) {
	console.log("\n② 历史超限·但比登记时变胖了（禁止 —— 烂地基上不许再叠）：");
	for (const f of grew) console.log("   ✗ " + f.rel + "  登记 " + f.was + " 行 → 现在 " + f.lines + " 行");
}
if (fresh.length) {
	console.log("\n③ 新增超限（白名单外，必须拆或登记）：");
	for (const f of fresh.sort((a, b) => b.lines - a.lines)) {
		console.log("   ✗ " + f.rel + "  " + f.lines + " 行 / 红线 " + f.limit + "（" + LIMIT_NOTE[f.ext] + "）");
	}
}
if (godFresh.length || godLegacy.length) {
	console.log("\n④ 上帝类信号（Q_INVOKABLE > " + GOD.qInvokable + " 或 顶层定义 > " + GOD.topLevel + "）：");
	for (const f of godFresh) console.log("   ✗ " + f.rel + "  Q_INVOKABLE=" + f.god.qInvokable + " 顶层定义=" + f.god.topLevel + "（新文件，判红）");
	for (const f of godLegacy.slice(0, 6)) console.log("   ! " + f.rel + "  Q_INVOKABLE=" + f.god.qInvokable + " 顶层定义=" + f.god.topLevel + "  " + (f.plan || ""));
}
console.log("-".repeat(78));
const errs = grew.length + fresh.length + godFresh.length;
console.log(errs === 0
	? "结论：PASS ✓  无新增膨胀；历史包袱 " + allowed.length + " 项在拆分计划内"
	: "结论：FAIL ✗  " + errs + " 项新增违规 —— 拆分它，或经人确认后 node scripts/test-file-size.mjs --update-whitelist");
console.log("");

process.exit(errs === 0 ? 0 : 1);
