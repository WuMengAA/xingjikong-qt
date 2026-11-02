#!/usr/bin/env node
/**
 * 灵动岛几何常量的跨端漂移门禁（2026-10-16 立 · U 系列 D 档）
 *
 * 为什么有它：岛的尺寸有**两个主人**，而且都在用：
 *   · 被控端 —— C++ `IslandOverlay` 按 `island.h::Shape` + `island.cpp::geometryForContent`
 *     给 QQuickWidget 定窗（SizeRootObjectToView ⇒ **C++ 是尺寸的真源**）。
 *   · 共享层 —— QML `IslandTheme` / `IslandRoot`（shared-qml/island/，被控端与
 *     管理端审批面板共用同一份）复刻同一套预算。
 * 两边是"两处手写、必须同值"。历史已经漂过：
 *   `IslandTheme.shapeIdle` 长期是 36（旧 DynamicIsland 的值），而 C++ 是 44（132×44 的 4:3 比例）；
 *   `shapeExpanded` 是 168（旧值），而 C++ 是 320。
 * 这类漂移**编得过、跑得动**，只在跨端观感分叉时才暴露 —— 所以必须机器盯。
 *
 * ⚠️ 2026-10-26（产品笔记 P0）：管理端主界面灵动岛已移除，原 `IslandHost.qml`
 *    （管理端宿主）随之删除 —— 本脚本第六/七节的 IslandHost 检查同步退役；
 *    管理端仅剩互动审批面板（InteractDialog）内的岛预览（仍是共享 IslandRoot）。
 *
 * 用法：node scripts/test-island-metrics.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 断言是**跨仓读真源**（island.h / island.cpp）—— 红了说明 QML 侧没跟上 C++ 真源，
 *    别把它们当噪声关掉（与 test-ui-contract.mjs 的权限词表门禁同一个道理）。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPO = path.join(ROOT, "..");
// ⚠️ 2026-11-02：被控端 src 文件夹化后，island.h / island.cpp 已搬到 src/island/。
//    旧路径让脚本启动即 ENOENT throw（不是断言失败、是根本没跑），
//    退出码非 0 却看着像"环境问题"，门禁等于空转。别再改回平铺路径。
const ISLAND_H = path.join(REPO, "Stelarith-control-qt", "src", "island", "island.h");
const ISLAND_CPP = path.join(REPO, "Stelarith-control-qt", "src", "island", "island.cpp");
const THEME = path.join(ROOT, "island", "IslandTheme.qml");
const ROOT_QML = path.join(ROOT, "island", "IslandRoot.qml");

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

const TH = {
	shapeIdle: num(thSrc, /shapeIdle\s*:\s*(\d+)/, "IslandTheme.shapeIdle"),
	shapeCollapsed: num(thSrc, /shapeCollapsed\s*:\s*(\d+)/, "IslandTheme.shapeCollapsed"),
	shapeExpanded: num(thSrc, /shapeExpanded\s*:\s*(\d+)/, "IslandTheme.shapeExpanded"),
};

// IslandRoot: implicitWidth: root._idle ? root.th.shapeIdle * N : (root._expanded ? A : B)
const rootIdleMul = num(rootSrc, /shapeIdle\s*\*\s*(\d+)/, "IslandRoot idle 宽度倍数");
// implicitWidth 行里的三元式（不能全文件 match：IslandRoot 里 _expanded ? N : N
// 还出现在标题字号预算（640）与 px（36/28）等别处，取第一个会抓到错的那个）
const rootWide = rootSrc.match(/implicitWidth:[^;]*?_expanded\s*\?\s*(\d+)\s*:\s*(\d+)/);
if (!rootWide) throw new Error("IslandRoot 的 implicitWidth 三元式没抓到");
const ROOTW = { expanded: Number(rootWide[1]), collapsed: Number(rootWide[2]) };

console.log("── 一、C++ 真源（island.h / island.cpp）──");
console.log(`  Shape = ${JSON.stringify(SHAPE)}  kIdleW = ${CPP.kIdleW}  kMinContentW = ${CPP.kMinContentW}  媒体宽 = ${JSON.stringify(MEDIA)}`);
console.log("── 二、QML 消费方 ──");
console.log(`  IslandTheme = ${JSON.stringify(TH)}`);
console.log(`  IslandRoot  = idle×${rootIdleMul} / ${JSON.stringify(ROOTW)}`);
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

console.log("── 六、C++ 真源完整性（2026-10-26：管理端宿主已退役，仅留真源检查）──");
t("kMinContentW 落在 C++ 真源", () => {
	ok(CPP.kMinContentW > 0, "kMinContentW 缺失");
});

console.log("");
console.log(`════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) {
	console.log("失败项：" + fails.join("、"));
	process.exit(1);
}
