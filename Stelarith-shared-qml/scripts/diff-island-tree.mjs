#!/usr/bin/env node
/**
 * 灵动岛 QML 树 / 属性的**基线比对器**（2026-10-16 立 · U 系列 D 档）
 *
 * 为什么有它：D 档要拆 `IslandRoot.qml`（2207 行 → 若干子组件）。拆分是**纯结构搬迁**，
 * 行为必须零变化 —— 但 QML 里"搬错了"不报错（隐式作用域断链、锚被静默丢弃、
 * 绑定环），编得过也跑得动，只在某个形态下悄悄画歪。所以拆之前先冻结探针基线，
 * 拆之后逐值比对，**差异必须为空**（除了我们**显式声明**的类型改名）。
 *
 * 数据来源：被控端岛探针 `control-qt/scripts/_island_qml_probe.py` 写出的
 * `D:/Stelarith/_probe/shots/_probe_result.txt`（`=== tree ===` 全子树几何 +
 * `root.*` 属性 dump）。它每轮以 WriteOnly 截断，所以**每态单独存档**。
 *
 * 用法：
 *   # 冻结基线（拆之前）
 *   python scripts/_island_qml_probe.py --expand && cp ... tree-expand.txt
 *   # 拆分后比对（--rename 把"新抽出的组件名"映射回基线里的匿名类名）
 *   node diff-island-tree.mjs baseline/tree-expand.txt now.txt \
 *        --rename IslandIdleFace=QQuickItem --rename IslandCollapsedFace=QQuickItem
 *
 * 退出码：0 = 零差异 / 1 = 有差异 / 2 = 用法或文件错
 *
 * 确定性（2026-10-16 实测）：同一个 exe 同模式跑两次，全树 216 项**只差 1 项** ——
 * 空闲/展开面那颗时钟的 `HH:mm` 文本（隔两分钟跑就会差）。它已被内置忽略；
 * 想连时钟一起比（比如同分钟内连跑两次，验证时钟未被误改）加 `--keep-time`。
 *
 * ⚠️ 另一类非确定项：**动画驱动的属性**。`WhaleMascot` 有呼吸浮动（`floatAmplitude: 3`），
 *    它那颗 `Image` 的 `y` 是按帧算的 —— 同一份代码连跑三次得到 -1.01067 / -1.01621 /
 *    -1.10437（实测），**每次都不同**，与代码改动无关。这类项用 `--ignore-line` 显式排除，
 *    别用放大 `--tol` 的方式掩盖（那会同时放过真实的 0.1 级偏移）。
 *    鲸鱼的用法：`--ignore-line 'QQuickImage.*y=-'`（负 y 只有鲸鱼本体有，气泡里那张是 y=0）。
 *
 * ⚠️ 为什么需要 --rename：探针打的是 `metaObject()->className()`，QML 给匿名内联对象
 *    编的名字带编译期序号（`QQuickItem_QML_22`、`IslandRoot_QMLTYPE_11`）——这些序号
 *    改一行就变，所以默认**剥掉**；而抽成独立文件后那层会变成真名
 *    （`IslandIdleFace_QMLTYPE_3`）。剥序号后它对不上基线的 `QQuickItem`，
 *    这正是**唯一允许的差异**，用 --rename 显式声明，别默默忽略。
 */
import fs from "node:fs";

const argv = process.argv.slice(2);
const opts = { tol: 0.01, section: "all", renames: {}, keepTime: false, ignoreLines: [] };
const files = [];
for (let i = 0; i < argv.length; i++) {
	const a = argv[i];
	if (a === "--tol") opts.tol = Number(argv[++i]);
	else if (a === "--section") opts.section = argv[++i];
	else if (a === "--keep-time") opts.keepTime = true;
	else if (a === "--ignore-line") {
		const re = String(argv[++i]);
		try { opts.ignoreLines.push(new RegExp(re)); }
		catch (e) { console.error("--ignore-line 正则非法：" + re + "（" + e.message + "）"); process.exit(2); }
	}
	else if (a === "--rename") {
		const [from, to] = String(argv[++i]).split("=");
		if (!from || !to) { console.error("--rename 需要 A=B"); process.exit(2); }
		opts.renames[from] = to;
	} else if (a.startsWith("--")) { console.error("未知参数 " + a); process.exit(2); }
	else files.push(a);
}
if (files.length !== 2) {
	console.error("用法：node diff-island-tree.mjs <baseline.txt> <current.txt> [--rename A=B] [--tol 0.01] [--section all|tree|props] [--keep-time] [--ignore-line <re>]");
	process.exit(2);
}

// 时钟文本（`HH:mm`）是唯一随时间变的东西，默认忽略。
const CLOCK = /^\d{1,2}:\d{2}$/;
const sameText = (a, b) => {
	if (a === b) return true;
	if (!opts.keepTime && CLOCK.test(a) && CLOCK.test(b)) return true;
	return false;
};

// ── 归一化 ────────────────────────────────────────────────────
// 剥掉 QML 编译期匿名序号：`WhaleMascot_QMLTYPE_6_QML_20` → `WhaleMascot`，
// `QQuickRectangle_QML_21` → `QQuickRectangle`。（两种后缀可能叠加，循环剥。）
function normClass(c) {
	let prev;
	do {
		prev = c;
		c = c.replace(/_QMLTYPE_\d+$/, "").replace(/_QML_\d+$/, "");
	} while (c !== prev);
	return opts.renames[c] || c;
}

const TREE_LINE = /^(\s*)(\S+)\s+y=(-?[\d.eE+-]+)\s+h=(-?[\d.eE+-]+)\s+w=(-?[\d.eE+-]+)\s+vis=(\d)(?:\s+text="(.*)")?$/;
const PROP_LINE = /^\s+(root\.\S+)\s=\s(.*)$/;

// boot 行带 pid（每轮不同），一律剔除；fonts 段是环境信息，稳定，但同样不进比对
// （探针的字体诊断与岛结构无关，比对里只会是噪声）。
function slice(src) {
	const lines = src.split(/\r?\n/);
	const tree = [];
	const props = [];
	let inTree = false;
	for (const ln of lines) {
		if (ln.startsWith("boot pid=")) continue;
		if (ln === "=== tree ===") { inTree = true; continue; }
		if (ln === "=== end tree ===") { inTree = false; continue; }
		if (inTree) tree.push(ln);
		else {
			const m = ln.match(PROP_LINE);
			if (m) props.push(ln);
		}
	}
	return { tree, props };
}

const load = (p) => {
	if (!fs.existsSync(p)) { console.error("找不到文件：" + p); process.exit(2); }
	return slice(fs.readFileSync(p, "utf8"));
};

const want = (s) => opts.section === "all" || opts.section === s;
const base = load(files[0]);
const cur = load(files[1]);

const diffs = [];
let checked = 0;
let ignored = 0;

// ── 一、树结构 ────────────────────────────────────────────────
if (want("tree")) {
	const bl = base.tree, cl = cur.tree;
	const n = Math.max(bl.length, cl.length);
	for (let i = 0; i < n; i++) {
		const b = bl[i], c = cl[i];
		if (b === undefined) { diffs.push(`[tree #${i}] 基线没有、当前多出：${c.trim()}`); continue; }
		if (c === undefined) { diffs.push(`[tree #${i}] 当前缺行（基线有）：${b.trim()}`); continue; }
		// 动画驱动项（见文件头）：显式排除，不是"放宽容差"。两侧都跳 ⇒ 下标仍然同步。
		if (opts.ignoreLines.some((re) => re.test(b) || re.test(c))) { ignored++; continue; }
		const mb = b.match(TREE_LINE), mc = c.match(TREE_LINE);
		if (!mb || !mc) {
			// 非 tree 样式的行（理论不会出现）：原样比
			if (b.trim() !== c.trim()) diffs.push(`[tree #${i}] 原样不等：${b.trim()}  ≠  ${c.trim()}`);
			continue;
		}
		checked++;
		const [, bi, bc, by, bh, bw, bv, bt] = mb;
		const [, ci, cc, cy, ch, cw, cv, ct] = mc;
		const tag = `#${i} ${bc}${bi.length === ci.length ? "" : "(!depth " + bi.length + "→" + ci.length + ")"}`;
		if (normClass(bc) !== normClass(cc))
			diffs.push(`[tree ${tag}] 类型不等：${normClass(bc)} ≠ ${normClass(cc)}`);
		if (bi.length !== ci.length)
			diffs.push(`[tree ${tag}] 缩进（层级）不等：${bi.length / 2} ≠ ${ci.length / 2}`);
		const dm = (label, a, bb) => {
			if (Math.abs(Number(a) - Number(bb)) > opts.tol)
				diffs.push(`[tree ${tag}] ${label} 不等：${a} ≠ ${bb}`);
		};
		dm("y", by, cy); dm("h", bh, ch); dm("w", bw, cw);
		if (bv !== cv) diffs.push(`[tree ${tag}] vis 不等：${bv} ≠ ${cv}`);
		if (!sameText(bt || "", ct || "")) diffs.push(`[tree ${tag}] text 不等："${bt || ""}" ≠ "${ct || ""}"`);
	}
}

// ── 二、root 属性 ─────────────────────────────────────────────
if (want("props")) {
	const bm = new Map(base.props.map((l) => { const m = l.match(PROP_LINE); return [m[1], m[2]]; }));
	const cm = new Map(cur.props.map((l) => { const m = l.match(PROP_LINE); return [m[1], m[2]]; }));
	for (const k of new Set([...bm.keys(), ...cm.keys()])) {
		checked++;
		if (!bm.has(k)) { diffs.push(`[prop] 当前多出：${k} = ${cm.get(k)}`); continue; }
		if (!cm.has(k)) { diffs.push(`[prop] 当前缺：${k}（基线 ${bm.get(k)}）`); continue; }
		if (bm.get(k) !== cm.get(k)) diffs.push(`[prop] ${k} 不等：${bm.get(k)} ≠ ${cm.get(k)}`);
	}
}

// ── 报告 ──────────────────────────────────────────────────────
console.log(`基线 ${files[0]}`);
console.log(`当前 ${files[1]}`);
if (Object.keys(opts.renames).length)
	console.log(`改名映射 ${JSON.stringify(opts.renames)}`);
console.log(`比对 ${checked} 项（section=${opts.section}, tol=${opts.tol}`
	+ (ignored ? `, 按 --ignore-line 排除 ${ignored} 项（动画驱动）` : "") + "）");
if (!diffs.length) {
	console.log("════ 零差异 ✅ ════");
	process.exit(0);
}
console.log(`════ ${diffs.length} 处差异 ❌ ════`);
for (const d of diffs.slice(0, 200)) console.log("  " + d);
if (diffs.length > 200) console.log(`  …（另有 ${diffs.length - 200} 处）`);
process.exit(1);
