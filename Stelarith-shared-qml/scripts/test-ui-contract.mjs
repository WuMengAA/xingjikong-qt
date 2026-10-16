#!/usr/bin/env node
/**
 * 界面插件声明契约的回归测试（2026-10-16 立 · U2）
 *
 * 为什么有它：U 系列要把「有哪些界面、怎么排」从 Main.qml（3858 行）里的硬编码
 * 改成一条条**声明**。声明对吧？靠肉眼看看不出来 —— 少一个字段、scope 写成
 * 单串而不是列表、permission 拼错一个冒号，全都能编过、也能跑，只在运行期表现为
 * "这个面板莫名其妙不显示"。所以把契约抽成纯函数（ui/UiContract.js），用 node 直接断言。
 *
 * 用法：node scripts/test-ui-contract.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 其中一条断言是**跨仓漂移门禁**：权限词表必须与
 *    control-qt/src/plugin/pluginhost.cpp 的 kKnownPermissions **逐字一致**。
 *    它红了就说明前后端的权限词表分叉了 —— 别把它当噪声关掉。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const JS = path.join(ROOT, "ui", "UiContract.js");
const PLUGINHOST = path.join(ROOT, "..", "Stelarith-control-qt", "src", "plugin", "pluginhost.cpp");

// 把 QML 的 .js（.pragma library + 顶层函数声明）当普通脚本读进来。
// 只有 `.pragma library` 这一行是 QML 专有语法（node 会报 Unexpected token '.'），剥掉即可；
// 其余必须是**纯 JS** —— 有人写了 QML 专有语法，这里立刻语法错，正好当门禁。
const raw = fs.readFileSync(JS, "utf8");
const src = raw.replace(/^\.pragma\b.*$/gm, "");
const stripped = raw.split(/\r?\n/).filter((l) => /^\.pragma\b/.test(l)).length;
if (stripped !== 1) throw new Error(`expected exactly one .pragma line, found ${stripped}`);

const C = new Function(`${src}
return { SURFACES, SCOPES, PERMISSIONS, FIELDS, CTX_FIELDS, ENTRY_API,
         normalize, normalizeAll, validate, validateOne, visible, forSurface, byId, defaults };`)();

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
const V = (env) => Object.assign({ scope: "viewer", permissions: [], caps: [] }, env);

console.log("── 一、结构常量 ──");
t("扩展面枚举齐全（page/panel/action/command/island）", () => {
	eq(C.SURFACES, ["page", "panel", "action", "command", "island"]);
});
t("适用范围枚举齐全", () => eq(C.SCOPES, ["viewer", "agent", "both"]));
t("entry 必须实现 open/close", () => eq(C.ENTRY_API, ["open", "close"]));

console.log("── 二、★ 跨仓漂移门禁：权限词表 ──");
t("PERMISSIONS 与 control-qt pluginhost.cpp 的 kKnownPermissions 逐字一致", () => {
	const cpp = fs.readFileSync(PLUGINHOST, "utf8");
	const m = cpp.match(/kKnownPermissions\s*=\s*\{([\s\S]*?)\};/);
	ok(m, "在 pluginhost.cpp 里没找到 kKnownPermissions 块（真源结构变了？）");
	const found = [...m[1].matchAll(/QStringLiteral\("([^"]+)"\)/g)].map((x) => x[1]);
	eq(C.PERMISSIONS, found, "权限词表分叉");
});

console.log("── 三、归一化 ──");
t("默认值：id 空 / scope=[viewer] / surfaces=[panel] / order=100", () => {
	const d = C.defaults();
	eq(d.id, ""); eq(d.scope, ["viewer"]); eq(d.surfaces, ["panel"]); eq(d.order, 100);
});
t("默认值深拷贝：改一份不影响另一份", () => {
	const a = C.normalize({}), b = C.normalize({});
	a.scope.push("agent");
	eq(b.scope, ["viewer"], "默认数组被共享引用");
});
t("list 字段：单串自动包成数组", () => {
	const p = C.normalize({ id: "x", surfaces: "action", keys: "lock" });
	eq(p.surfaces, ["action"]); eq(p.keys, ["lock"]);
});
t("list 字段：空串/undefined → 空数组", () => {
	eq(C.normalize({ id: "x", caps: "" }).caps, []);
	eq(C.normalize({ id: "x" }).caps, []);
});
t("bool / int 归一", () => {
	const p = C.normalize({ id: "x", danger: 1, admin: 0, order: "42" });
	eq(p.danger, true); eq(p.admin, false); eq(p.order, 42);
});
t("未知字段被丢弃（不让脏字段混进模型）", () => {
	ok(!("whatever" in C.normalize({ id: "x", whatever: 1 })));
});
t("normalizeAll：非数组 → 空数组", () => { eq(C.normalizeAll(null), []); eq(C.normalizeAll("x"), []); });

console.log("── 四、校验 ──");
t("缺 id 报错", () => ok(C.validateOne(C.normalize({})).some((s) => s.includes("缺 id"))));
t("id 大写报错", () => ok(C.validateOne(C.normalize({ id: "Interact" })).some((s) => s.includes("全小写"))));
t("未知 surface 报错", () => {
	ok(C.validateOne(C.normalize({ id: "x", surfaces: ["popup"] })).some((s) => s.includes("未知 surface")));
});
t("未知 permission 报错", () => {
	ok(C.validateOne(C.normalize({ id: "x", perm: "events_noise" })).some((s) => s.includes("未知 permission")));
});
t("panel 面缺 entry 报错", () => {
	ok(C.validateOne(C.normalize({ id: "x", surfaces: ["panel"] })).some((s) => s.includes("缺 entry")));
});
t("action 面缺 action 名报错", () => {
	ok(C.validateOne(C.normalize({ id: "x", surfaces: ["action"] })).some((s) => s.includes("缺 action")));
});
t("纯逻辑面（command）不要求 entry", () => {
	eq(C.validateOne(C.normalize({ id: "x", surfaces: ["command"] })), []);
});
t("合法声明零问题", () => {
	eq(C.validateOne(C.normalize({ id: "ok", surfaces: ["panel"], entry: "a/b.qml" })), []);
});
t("整个清单能查出 id 重复", () => {
	const l = C.normalizeAll([{ id: "dup", surfaces: ["command"] }, { id: "dup", surfaces: ["command"] }]);
	ok(C.validate(l).some((s) => s.includes("id 重复")));
});

console.log("── 五、门控（容错口径是重点）──");
t("scope：viewer 环境看不到 agent-only", () => {
	const p = C.normalize({ id: "a", scope: ["agent"] });
	ok(!C.visible(p, V({ scope: "viewer" })));
	ok(C.visible(p, V({ scope: "agent" })));
});
t("scope：both 两端都看得到", () => {
	const p = C.normalize({ id: "a", scope: ["both"] });
	ok(C.visible(p, V({ scope: "viewer" })));
	ok(C.visible(p, V({ scope: "agent" })));
});
t("★ perm：环境未上报权限（老端）⇒ 一律放行，不锁死", () => {
	const p = C.normalize({ id: "a", perm: "notify" });
	ok(C.visible(p, V({ permissions: [] })), "空权限表应放行");
});
t("perm：环境上报了权限但缺这一条 ⇒ 隐藏", () => {
	const p = C.normalize({ id: "a", perm: "notify" });
	ok(!C.visible(p, V({ permissions: ["settings"] })));
	ok(C.visible(p, V({ permissions: ["settings", "notify"] })));
});
t("★ caps：被控端未上报能力（老机器）⇒ 一律放行", () => {
	const p = C.normalize({ id: "a", caps: ["lock"] });
	ok(C.visible(p, V({ caps: [] })), "空能力表应放行");
});
t("caps：上报了能力但不含所需 ⇒ 隐藏", () => {
	const p = C.normalize({ id: "a", caps: ["lock", "reboot"] });
	ok(!C.visible(p, V({ caps: ["lock"] })), "缺 reboot 应隐藏");
	ok(C.visible(p, V({ caps: ["lock", "reboot", "shutdown"] })));
});
t("perm 与 caps 同时具备才可见", () => {
	const p = C.normalize({ id: "a", perm: "notify", caps: ["lock"] });
	ok(C.visible(p, V({ permissions: ["notify"], caps: [] })), "caps 未上报 ⇒ 放行");
	ok(!C.visible(p, V({ permissions: ["notify"], caps: ["reboot"] })), "caps 上报了但缺 lock ⇒ 隐藏");
	ok(!C.visible(p, V({ permissions: ["settings"], caps: ["lock"] })), "权限缺 notify ⇒ 隐藏");
	ok(C.visible(p, V({ permissions: ["notify"], caps: ["lock"] })));
});
t("visible(null) 安全返回 false", () => ok(!C.visible(null, V({}))));

console.log("── 六、派生与检索 ──");
const MAN = C.normalizeAll([
	{ id: "c", title: "C", surfaces: ["panel"], order: 30, entry: "p/c.qml" },
	{ id: "a", title: "A", surfaces: ["panel", "command"], order: 10, entry: "p/a.qml", keys: ["aa"] },
	{ id: "b", title: "B", surfaces: ["action"], order: 20, action: "lock" },
	{ id: "g", title: "G", surfaces: ["panel"], order: 5, entry: "p/g.qml", scope: ["agent"] }
]);
t("forSurface：按 order 升序", () => {
	eq(C.forSurface(MAN, "panel", V({})).map((p) => p.id), ["a", "c"], "agent-only 应被滤掉");
});
t("forSurface：action 面只留 action", () => eq(C.forSurface(MAN, "action", V({})).map((p) => p.id), ["b"]));
t("forSurface：command 面只留 command", () => eq(C.forSurface(MAN, "command", V({})).map((p) => p.id), ["a"]));
t("byId 命中 / 未命中", () => { eq(C.byId(MAN, "a").title, "A"); eq(C.byId(MAN, "nope"), null); });

console.log("");
console.log(`════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) {
	console.log("失败项：" + fails.join("、"));
	process.exit(1);
}
