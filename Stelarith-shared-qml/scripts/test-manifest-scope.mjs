#!/usr/bin/env node
/**
 * 界面声明「端过滤」门禁（2026-10-16 立 · 真机事故驱动）
 *
 * 为什么有它：UiContract.js:60 里 `scope` 的默认值是 **["viewer"]**。
 * 被控端 `control-qt/qml/shell/AgentPages.js` 的 5 条页声明**全都漏写 scope**
 * ⇒ 归一成 viewer 专属 ⇒ 在 agent 端被 `visible()` 全过滤 ⇒ 真机表现是：
 *
 *     面板只剩顶栏，**内容区与底导航一起消失**（白底、零 QML ERROR、零日志）。
 *
 * 这是最费的一种坏法：所有门禁都绿（声明合法、QML 无错），只是"什么都不显示"。
 * 纪律由此定下：**在 agent 端跑的声明必须显式写 scope，不许吃默认值。**
 * 默认值对 viewer 恰好正确 —— 靠人记住这件事不可靠，靠这个脚本记。
 *
 * 扫三个声明文件：
 *   control  qml/shell/AgentPages.js        ★强制：每条必须显式 scope 且含 "agent"
 *   viewer   qml/UiPlugins.js                 结构：id 唯一 / surfaces 合法 / entry 存在
 *   shared   ui/samples/UiPlugins.sample.js   同上（参考实现，混合端）
 *
 * ⚠️ 后两个文件**没有**"必须显式写 scope"的强制项：它们只在 viewer 端跑，
 *    默认值是恰好正确的；本门禁只**提示**有多少条依赖默认值（见末行报告），
 *    要不要把它改成显式，是另一个决定（不在这里替谁做主）。
 *
 * 用法：node scripts/test-manifest-scope.mjs
 * 退出码：0 全过 / 1 有失败
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BASE = path.resolve(ROOT, "..");

const SURFACES = ["page", "panel", "action", "command"];
const SCOPES = ["viewer", "agent", "both"];

const TARGETS = [
	{
		label: "被控端页声明",
		file: path.join(BASE, "Stelarith-control-qt", "qml", "shell", "AgentPages.js"),
		entryBase: path.join(BASE, "Stelarith-control-qt", "qml", "shell"),
		mustScope: "agent", // ★ 强制显式写且含 agent
	},
	{
		label: "管理端界面声明",
		file: path.join(BASE, "Stelarith-viewer-qt", "qml", "UiPlugins.js"),
		entryBase: path.join(BASE, "Stelarith-viewer-qt", "qml"),
		mustScope: null,
	},
	{
		label: "共享参考声明",
		file: path.join(ROOT, "ui", "samples", "UiPlugins.sample.js"),
		// ⚠️ entry 写的是 "samples/SamplePage.qml" ⇒ 基准是 ui/（不是 ui/samples/）
		entryBase: path.join(ROOT, "ui"),
		mustScope: null,
	},
];

/** 把 QML 的 .js 当普通脚本读进来（`.pragma library` 那行 node 不认，剥掉）。 */
function loadManifest(file) {
	const raw = fs.readFileSync(file, "utf8");
	const pragma = raw.split(/\r?\n/).filter((l) => /^\s*\.pragma\b/.test(l)).length;
	if (pragma !== 1)
		throw new Error(`expected exactly one .pragma line, found ${pragma}`);
	const src = raw.replace(/^\s*\.pragma\b.*$/gm, "");
	const fn = new Function(`${src}\nreturn manifest;`)();
	const list = fn();
	if (!Array.isArray(list)) throw new Error("manifest() 必须返回数组");
	return { list, raw };
}

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
function eq(actual, expect, what) {
	if (actual !== expect) throw new Error(`${what}: 期望 ${expect}，实际 ${actual}`);
}

const notices = [];

for (const tg of TARGETS) {
	const rel = path.relative(BASE, tg.file).replace(/\\/g, "/");
	console.log(`\n── ${tg.label}（${rel}）──`);

	let man;
	try {
		man = loadManifest(tg.file);
	} catch (e) {
		fails.push(`${tg.label} 读取`);
		console.log(`  FAIL  读取/解析\n        ${e.message}`);
		continue;
	}
	const { list } = man;

	t(`${tg.label}：声明条数 ≥ 1`, () => {
		if (list.length < 1) throw new Error("manifest() 返回空数组");
	});

	t(`${tg.label}：id 唯一且非空`, () => {
		const seen = new Set();
		for (const p of list) {
			if (!p.id) throw new Error("有条声明没有 id");
			if (seen.has(p.id)) throw new Error(`id 重复：${p.id}`);
			seen.add(p.id);
		}
	});

	t(`${tg.label}：surfaces 合法（非空数组，元素 ∈ ${SURFACES.join("/")}）`, () => {
		for (const p of list) {
			if (!Array.isArray(p.surfaces) || p.surfaces.length === 0)
				throw new Error(`${p.id}: surfaces 必须是非空数组`);
			for (const s of p.surfaces)
				if (!SURFACES.includes(s)) throw new Error(`${p.id}: 未知面 "${s}"`);
		}
	});

	t(`${tg.label}：page 类声明的 entry 文件真实存在`, () => {
		for (const p of list) {
			if (!Array.isArray(p.surfaces) || !p.surfaces.includes("page")) continue;
			if (!p.entry) throw new Error(`${p.id}: page 类声明必须写 entry`);
			const f = path.join(tg.entryBase, p.entry);
			if (!fs.existsSync(f))
				throw new Error(`${p.id}: entry 不存在 → ${path.relative(BASE, f)}`);
		}
	});

	if (tg.mustScope) {
		// ★ 本次事故的核心断言
		t(`${tg.label}：每条声明**显式**写 scope（不许吃默认 ["viewer"]）`, () => {
			const missing = list.filter((p) => p.scope === undefined).map((p) => p.id);
			if (missing.length > 0)
				throw new Error(
					`${missing.length} 条没写 scope：${missing.join(", ")}\n` +
					`        ⇒ 这些声明在 ${tg.mustScope} 端会被 UiContract.visible() 全过滤，` +
					`真机表现为「内容与导航一起消失」`
				);
		});

		t(`${tg.label}：每条 scope 含 "${tg.mustScope}"（或 both）且元素合法`, () => {
			for (const p of list) {
				if (p.scope === undefined) continue; // 上一条已报
				if (!Array.isArray(p.scope))
					throw new Error(`${p.id}: scope 必须是数组（契约 type=list）`);
				for (const s of p.scope)
					if (!SCOPES.includes(s)) throw new Error(`${p.id}: 未知 scope "${s}"`);
				if (!p.scope.includes(tg.mustScope) && !p.scope.includes("both"))
					throw new Error(
						`${p.id}: scope=${JSON.stringify(p.scope)} 不含 "${tg.mustScope}"`
					);
			}
		});

		t(`${tg.label}：所有声明在 ${tg.mustScope} 端都能派生出来（照契约算一遍）`, () => {
			// 与 UiContract.visible() 的端判定逐字一致（scope 那一段）
			const sc = tg.mustScope;
			const hidden = list
				.filter((p) => {
					const a = Array.isArray(p.scope) ? p.scope : [p.scope];
					return !a.includes("both") && !a.includes(sc);
				})
				.map((p) => p.id);
			if (hidden.length > 0) throw new Error(`被端过滤掉：${hidden.join(", ")}`);
		});
	} else {
		const dep = list.filter((p) => p.scope === undefined).length;
		notices.push(
			`${tg.label}：${dep}/${list.length} 条依赖 scope 默认值（该端恰好正确；` +
			`改成显式是另一个决定）`
		);
	}
}

console.log("");
for (const n of notices) console.log(`  note  ${n}`);
console.log(`\ntest-manifest-scope: ${pass} 通过 / ${fails.length} 失败`);
if (fails.length > 0) {
	console.log(`失败项：${fails.join(" / ")}`);
	process.exit(1);
}
