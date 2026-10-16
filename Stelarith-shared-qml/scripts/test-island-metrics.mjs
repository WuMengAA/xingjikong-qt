#!/usr/bin/env node
/**
 * 灵动岛几何常量的跨端漂移门禁（2026-10-16 立 · U 系列 D 档）
 *
 * 为什么有它：岛的尺寸有**两个主人**，而且都在用：
 *   · 被控端 —— C++ `IslandOverlay` 按 `island.h::Shape` + `island.cpp::geometryForContent`
 *     给 QQuickWidget 定窗（SizeRootObjectToView ⇒ **C++ 是尺寸的真源**）。
 *   · 管理端 —— QML `IslandHost` 是嵌在布局里的普通 Item，**复刻**同一套预算（没有 C++ 宿主）。
 * 两边是"两处手写、必须同值"。历史已经漂过：
 *   `IslandTheme.shapeIdle` 长期是 36（旧 DynamicIsland 的值），而 C++ 是 44（132×44 的 4:3 比例）；
 *   `shapeExpanded` 是 168（旧值），而 C++ 是 320。
 * 这类漂移**编得过、跑得动**，只在跨端观感分叉时才暴露 —— 所以必须机器盯。
 *
 * 用法：node scripts/test-island-metrics.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 两条断言是**跨仓读真源**（island.h / island.cpp）—— 红了说明 QML 侧没跟上 C++ 真源，
 *    别把它们当噪声关掉（与 test-ui-contract.mjs 的权限词表门禁同一个道理）。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPO = path.join(ROOT, "..");
const ISLAND_H = path.join(REPO, "Stelarith-control-qt", "src", "island.h");
const ISLAND_CPP = path.join(REPO, "Stelarith-control-qt", "src", "island.cpp");
const THEME = path.join(ROOT, "island", "IslandTheme.qml");
const ROOT_QML = path.join(ROOT, "island", "IslandRoot.qml");
const HOST = path.join(REPO, "Stelarith-viewer-qt", "qml", "components", "IslandHost.qml");

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
	if (a !== b) throw new Error(`${label || "值不等"}：实际 ${a} / 期望 ${b}`);
}
function ok(cond, label) {
	if (!cond) throw new Error(label || "断言失败");
}
const read = (p) => fs.readFileSync(p, "utf8");
const num = (src, re, label) => {
	const m = src.match(re);
	if (!m) throw new Error(`没抓到 ${label}（真源/消费方结构变了？）`);
	return Number(m[1]);
};

// ── 一、真源：C++ 侧 ────────────────────────────────────────────
const hSrc = read(ISLAND_H);
const cppSrc = read(ISLAND_CPP);

const shapeBlock = hSrc.match(/enum\s+Shape\s*\{([\s\S]*?)\};/);
if (!shapeBlock) throw new Error("island.h 里没找到 enum Shape 块");
const SHAPE = {};
for (const m of shapeBlock[1].matchAll(/(\w+)\s*=\s*(\d+)/g)) SHAPE[m[1]] = Number(m[2]);

const CPP = {
	kIdleW: num(cppSrc, /kIdleW\s*=\s*(\d+)/, "kIdleW"),
	kMinContentW: num(cppSrc, /kMinContentW\s*=\s*(\d+)/, "kMinContentW"),
	mediaW: num(cppSrc, /const\s+int\s+mw\s*=\s*m_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/, "媒体呈现宽"),
};
// 媒体宽两个分支（展开 / 收起）
const mediaMatch = cppSrc.match(/const\s+int\s+mw\s*=\s*m_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/);
const MEDIA = { expanded: Number(mediaMatch[1]), collapsed: Number(mediaMatch[2]) };

// ── 二、消费方：QML 侧 ──────────────────────────────────────────
const thSrc = read(THEME);
const rootSrc = read(ROOT_QML);
const hostSrc = read(HOST);

const TH = {
	shapeIdle: num(thSrc, /shapeIdle\s*:\s*(\d+)/, "IslandTheme.shapeIdle"),
	shapeCollapsed: num(thSrc, /shapeCollapsed\s*:\s*(\d+)/, "IslandTheme.shapeCollapsed"),
	shapeExpanded: num(thSrc, /shapeExpanded\s*:\s*(\d+)/, "IslandTheme.shapeExpanded"),
};

// IslandRoot: implicitWidth: root._idle ? root.th.shapeIdle * N : (root._expanded ? A : B)
const rootIdleMul = num(rootSrc, /shapeIdle\s*\*\s*(\d+)/, "IslandRoot idle 宽度倍数");
const rootWide = rootSrc.match(/_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/);
if (!rootWide) throw new Error("IslandRoot 的 implicitWidth 三元式没抓到");
const ROOTW = { expanded: Number(rootWide[1]), collapsed: Number(rootWide[2]) };

// IslandHost: implicitWidth/Height 各一条三元式
const hostW = hostSrc.match(/_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/);
if (!hostW) throw new Error("IslandHost 的 implicitWidth 三元式没抓到");
const HOSTW = { expanded: Number(hostW[1]), collapsed: Number(hostW[2]) };
const hostHits = [...hostSrc.matchAll(/_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/g)];
if (hostHits.length !== 2) throw new Error(`IslandHost 应恰好 2 条尺寸三元式，实测 ${hostHits.length}`);
const HOSTH = { expanded: Number(hostHits[1][1]), collapsed: Number(hostHits[1][2]) };

console.log("── 一、C++ 真源（island.h / island.cpp）──");
console.log(`  Shape = ${JSON.stringify(SHAPE)}  kIdleW = ${CPP.kIdleW}  kMinContentW = ${CPP.kMinContentW}  媒体宽 = ${JSON.stringify(MEDIA)}`);
console.log("── 二、QML 消费方 ──");
console.log(`  IslandTheme = ${JSON.stringify(TH)}`);
console.log(`  IslandRoot  = idle×${rootIdleMul} / ${JSON.stringify(ROOTW)}`);
console.log(`  IslandHost  = W ${JSON.stringify(HOSTW)} / H ${JSON.stringify(HOSTH)}`);
console.log("");

console.log("── 三、形状令牌必须等于 C++ enum Shape ──");
t("shapeIdle === Shape::Idle", () => eq(TH.shapeIdle, SHAPE.Idle, "idle 高度漂移"));
t("shapeCollapsed === Shape::Collapsed", () => eq(TH.shapeCollapsed, SHAPE.Collapsed, "收起态高度漂移"));
t("shapeExpanded === Shape::Expanded", () => eq(TH.shapeExpanded, SHAPE.Expanded, "展开态高度漂移"));

console.log("── 四、Idle 宽度的 4:3 比例自洽 ──");
t("shapeIdle × IslandRoot 倍数 === C++ kIdleW", () => {
	eq(TH.shapeIdle * rootIdleMul, CPP.kIdleW, "idle 宽高比与 C++ kIdleW 不符");
});
t("IslandRoot 的 idle 倍数恰为 3（4:3）", () => eq(rootIdleMul, 3));

console.log("── 五、媒体呈现宽：IslandRoot 与 C++ 同值 ──");
t("IslandRoot 媒体宽 === C++ 媒体宽", () => eq(ROOTW, MEDIA, "IslandRoot 与 island.cpp 的媒体宽分叉"));

console.log("── 六、管理端宿主 IslandHost 复刻值（宽度/高度）──");
t("IslandHost 宽 === C++ 媒体宽（收起/展开）", () => eq(HOSTW, MEDIA, "IslandHost 宽度与 C++ 媒体宽分叉"));
t("IslandHost 收起高 === Shape::Collapsed", () => eq(HOSTH.collapsed, SHAPE.Collapsed, "宿主收起高漂移"));
t("IslandHost 展开高 === Shape::Expanded", () => eq(HOSTH.expanded, SHAPE.Expanded, "宿主展开高漂移"));

console.log("── 七、已知缺口看板（防静默变更）──");
t("★ 已知缺口仍在：IslandHost 未复刻非媒体「内容定宽」路径", () => {
	// C++ geometryForContent 的非媒体分支按 QFontMetrics 算内容宽度（qBound(kMinContentW, iconW+textW+48, maxW)）。
	// 管理端 IslandHost 是固定 320 —— 这是**有意保留**（岛嵌在布局里，宽度随文案跳会顶动周围布局），
	// 不是遗漏。一旦有人补上复刻，这条会红 ⇒ 提醒同步更新归档与两处注释，别默默改观感。
	// ⚠️ 判据必须**剥掉注释**再看 —— 注释里提到 kMinContentW 是在说明这个缺口，不是复刻了它。
	const codeOnly = hostSrc.replace(/^\s*\/\/.*$/gm, "");
	const hasTextBudget = /TextMetrics|horizontalAdvance|fontMetrics/.test(codeOnly);
	ok(!hasTextBudget, "IslandHost 出现了内容定宽预算 —— 缺口已补，请同步更新归档与本断言");
});
t("kMinContentW 落在 C++ 真源（提醒：管理端未复刻，见上条）", () => {
	ok(CPP.kMinContentW > 0, "kMinContentW 缺失");
});

console.log("");
console.log(`════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) {
	console.log("失败项：" + fails.join("、"));
	process.exit(1);
}
