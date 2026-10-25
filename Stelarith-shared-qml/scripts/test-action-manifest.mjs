#!/usr/bin/env node
/**
 * 设备动作 / 命令清单的回归测试（2026-10-16 立 · U4）
 *
 * 为什么有它：U4 把同一批动作的**三份硬编码清单**（概览快捷行 7 条 / 右栏 5 组 12 条 /
 * 命令面板 38 条）合并成一份声明（viewer-qt/qml/ActionDefs.js）。
 * 这种"合并"最危险的失败模式是**静默漏项**：少一条命令 → 搜不到；
 * 文案漂一个字 → 老师看到的名字变了。两者都不会报错、也不会崩。
 *
 * 所以拿改造前的原文当 fixture（scripts/fixtures/action-manifest.expected.json），
 * 逐字比对。fixture 是**冻结的历史基线**：以后改文案要连它一起改 ——
 * 那时你会被迫看一眼"我是不是在无意中改变用户看到的东西"。
 *
 * 用法：node scripts/test-action-manifest.mjs
 * 退出码：0 全过 / 1 有失败
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const CONTRACT = path.join(ROOT, "ui", "UiContract.js");
const ACTIONS = path.join(ROOT, "..", "Stelarith-viewer-qt", "qml", "ActionDefs.js");
const PLUGINS = path.join(ROOT, "..", "Stelarith-viewer-qt", "qml", "UiPlugins.js");
const FIXTURE = path.join(ROOT, "scripts", "fixtures", "action-manifest.expected.json");

function loadQmlJs(file) {
	const raw = fs.readFileSync(file, "utf8");
	const src = raw.replace(/^\.pragma\b.*$/gm, "");
	return new Function(`${src}
return {
  manifest,
  specialIds: (typeof specialIds === "function") ? specialIds : null
};`)();
}

const rawC = fs.readFileSync(CONTRACT, "utf8");
const C = new Function(`${rawC.replace(/^\.pragma\b.*$/gm, "")}
return { normalizeAll, validate, visible, forSurface, byId, capNeeds, groups, quickActions, forGroup };`)();

const A = loadQmlJs(ACTIONS);
const U = loadQmlJs(PLUGINS);
const F = JSON.parse(fs.readFileSync(FIXTURE, "utf8"));

const items = C.normalizeAll(A.manifest().concat(U.manifest()));
const env = { scope: "viewer", permissions: [], caps: [] };

let pass = 0;
const fails = [];
function t(name, fn) {
	try { fn(); pass++; console.log(`  PASS  ${name}`); }
	catch (e) { fails.push(name); console.log(`  FAIL  ${name}\n        ${e.message}`); }
}
function eq(a, b, label) {
	const x = JSON.stringify(a), y = JSON.stringify(b);
	if (x !== y) throw new Error(`${label || "值不等"}：\n          实际 ${x}\n          期望 ${y}`);
}
function ok(c, label) { if (!c) throw new Error(label || "断言失败"); }

console.log("── 一、声明合法性 ──");
t("两份清单合并后零问题（id 跨清单唯一）", () => eq(C.validate(items), [], "validate"));
t("条目数 = 52（动作）+ 14（面板）", () => {
	// 2026-10-25 教室小工具第一刀：45 → 52（新增 7 条课堂工具，见 fixture note）
	eq(A.manifest().length, 52, "ActionDefs");
	eq(U.manifest().length, 14, "UiPlugins");
});

console.log("\n── 二、★ 命令面板：与改造前 38 条逐字比对 ──");
const cmds = C.forSurface(items, "command", env).slice().sort((a, b) => a.palOrder - b.palOrder);
t("条数 = 38", () => eq(cmds.length, F.pal.length, "条数"));
t("名称逐字一致（同序）", () => eq(cmds.map((p) => p.palName || p.title), F.pal.map((b) => b.name), "name"));
t("检索别名逐条一致", () => eq(cmds.map((p) => p.keys), F.pal.map((b) => b.keys), "keys"));
t("说明文案逐条一致", () => eq(cmds.map((p) => p.tip), F.pal.map((b) => b.tip), "tip"));

console.log("\n── 三、★ 右栏 5 组：与改造前逐字比对 ──");
t("分组名与顺序一致", () => eq(C.groups(items, env), Object.keys(F.right), "groups"));
t("组内文案与顺序一致", () => {
	const got = {};
	for (const g of Object.keys(F.right)) got[g] = C.forGroup(items, g, env).map((p) => p.title);
	eq(got, F.right, "组内文案");
});
t("电源组 = 整行 + 重描边 + 危险（不可逆那三个）", () => {
	ok(C.forGroup(items, "电源", env).every((p) => p.wide && p.heavy && p.danger), "电源组属性");
});
t("看看组无 wide（2 列网格）", () => {
	ok(C.forGroup(items, "看看", env).every((p) => !p.wide), "看看组应无 wide");
});

console.log("\n── 四、★ 概览快捷行：与改造前 7 条（手机 5）比对 ──");
t("桌面文案与顺序一致", () => {
	eq(C.quickActions(items, env).map((p) => p.quickTitle || p.title), F.quickDesktop, "quick");
});
t("手机端一致（滤掉手机不显示的）", () => {
	eq(C.quickActions(items, env).filter((p) => p.phone !== false)
		.map((p) => p.quickTitle || p.title), F.quickPhone, "phone quick");
});

console.log("\n── 五、多选批量条 ──");
t("文案与顺序一致", () => {
	eq(items.filter((p) => p.bulk).map((p) => p.title), F.bulk.map((b) => b.title), "bulk");
});

console.log("\n── 六、分派完整性 ──");
t("kind 枚举合法（send/panel/special/app）", () => {
	const bad = items.filter((p) => !["send", "panel", "special", "app"].includes(p.kind)).map((p) => p.id);
	eq(bad, [], "非法 kind");
});
t("★ kind=special 的 id 与宿主 runSpecial 的 case 一一对应", () => {
	const host = fs.readFileSync(path.join(ROOT, "..", "Stelarith-viewer-qt", "qml", "Main.qml"), "utf8");
	// ⚠️ 行尾必须 \r?\n：本仓 core.autocrlf=true，工作树按 CRLF 检出
	//    （git stash/pop、全新 clone 都会把 LF blob 写成 CRLF 工作树文件）。
	//    写死 \n 会在 CRLF 工作树上静默失配 ⇒ ok(body) 假阴性。
	//    同目录 test-ui-terms / test-ui-contract / test-manifest-scope 用的都是 \r?\n。
	const body = host.match(/function runSpecial\(id\) \{([\s\S]*?)\r?\n    \}\r?\n/);
	ok(body, "找不到 runSpecial");
	const cases = new Set([...body[1].matchAll(/case "([^"]+)":/g)].map((m) => m[1]));
	const need = A.specialIds();
	eq(need.filter((x) => !cases.has(x)), [], "声明有、宿主缺 case");
	eq([...cases].filter((x) => !need.includes(x)), [], "宿主有、声明没有");
});
t("kind=panel 指向的面板 id 都真实存在（除 openOnly 的兼容别名）", () => {
	const ids = new Set(U.manifest().map((p) => p.id));
	ids.add("broadcast");   // openOnly("broadcast") → control，历史别名
	eq(items.filter((p) => p.kind === "panel" && !ids.has(p.panel)).map((p) => p.id), [], "悬空 panel");
});
t("kind=app 的子类型都在 5 个之内", () => {
	const s = new Set(items.filter((p) => p.kind === "app").map((p) => p.app));
	eq([...s].sort(), ["external", "login", "page", "role", "theme"], "app 子类型");
});

console.log("\n── 七、门控口径与改造前一致 ──");
t("caps：简单动作用 action 兜底（lock → [lock]）", () => {
	eq(C.capNeeds(C.byId(items, "lock")), ["lock"], "lock");
});
t("caps：显式多条优先（software-list 要两条才算能开）", () => {
	eq(C.capNeeds(C.byId(items, "software-list")),
		["list_shortcut_candidates", "process_list"], "software-list");
});
t("caps：面板类靠显式（terminal → [terminal_open]）", () => {
	eq(C.capNeeds(C.byId(items, "terminal")), ["terminal_open"], "terminal");
});
t("能力缺项 ⇒ 不可见；未上报能力 ⇒ 一律放行（老端容错）", () => {
	ok(!C.visible(C.byId(items, "lock"), { scope: "viewer", permissions: [], caps: ["ping"] }), "缺项应隐藏");
	ok(C.visible(C.byId(items, "lock"), { scope: "viewer", permissions: [], caps: [] }), "未上报应放行");
});
t("role（看人）与 perm（插件权限词表）分开，不混用", () => {
	const p = C.byId(items, "lock");
	eq(p.role, "admin", "role");
	eq(p.perm, "", "perm");
});
t("quickGuard 只有 lock（保留快捷行原有的那道，见契约注释）", () => {
	eq(items.filter((p) => p.quickGuard).map((p) => p.id), ["lock"], "quickGuard");
});

console.log(`\n════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) { for (const f of fails) console.log("  ✗ " + f); process.exit(1); }
process.exit(0);
