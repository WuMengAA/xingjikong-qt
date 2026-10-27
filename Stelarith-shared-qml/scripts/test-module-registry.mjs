#!/usr/bin/env node
/**
 * 内置模块层门禁（2026-10-17 立 · 「模块化插件」地基）
 *
 * 为什么有它：内置模块层（src/modules/）是**编译进 agent** 的第二条扩展通道，
 * 坏起来全是「无声」的那类——
 *   · 模块头忘列进 CMakeLists 的 add_executable ⇒ 不生成 moc 产物 ⇒ LNK2001
 *     （而且只在链接期炸，改完 CMakeLists 还得手动重跑 configure，见记忆 AUTOMOC 三连坑）
 *   · 模块 .cpp 写完了却没在 main.cpp registerModule ⇒ 编进了二进制但永远不启用，
 *     零报错零日志，功能「做完了看不见」
 *   · 探针编译钩子（STE_USB_PROBE_FAKE_REMOVABLE）漏进生产构建 ⇒ 真机把某个盘符
 *     当成 U 盘乱弹窗
 *   · 模块声明了权限词表外的权限 / 认领了内置同名 action / 订阅了没人投喂的事件
 *     ⇒ 分别对应「给了超纲权限」「分派被内置抢先或两模块抢同一 action」「永远不触发」
 *
 * 用法：node scripts/test-module-registry.mjs
 * 退出码：0 全过 / 1 有失败
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BASE = path.resolve(ROOT, "..");
const CTRL = path.join(BASE, "Stelarith-control-qt");
const MODULES_DIR = path.join(CTRL, "src", "modules");
const CMAKE = path.join(CTRL, "CMakeLists.txt");
const MAIN = path.join(CTRL, "src", "main.cpp");
const PLUGINHOST = path.join(CTRL, "src", "plugin", "pluginhost.cpp");

/** 剥掉 C++ 注释再匹配（否则说明文字里的字面量会造成假命中/假红）。 */
function stripComments(src) {
	return src.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
}
function read(p) {
	return stripComments(fs.readFileSync(p, "utf8"));
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

// ── 0. 收集模块文件 ──
const moduleFiles = fs
	.readdirSync(MODULES_DIR)
	.filter((f) => f.endsWith(".cpp") && f !== "modulehost.cpp")
	.map((f) => path.basename(f, ".cpp"));

console.log(`── 内置模块层（src/modules/，共 ${moduleFiles.length} 个模块）──`);

const cmake = read(CMAKE);
const mainSrc = read(MAIN);
const pluginSrc = read(PLUGINHOST);

// CMakeLists 的 add_executable 源列表（取第一个 add_executable 段）
// ⚠️ 2026-10-27 修：原来用 /add_executable\s*\(([\s\S]*?)\)/ 非贪婪匹配到第一个 `)`，
// 但 CMakeLists 的注释里也含括号（如「Canvas(纯 Qt) / Overlay(Win32)」），会提前截断，
// 导致 modulehost/usbmodule/voicehubmodule 恒报「未列入」（假阴性）。改为按括号深度配对。
function extractParen(src, keyword) {
	const idx = src.indexOf(keyword);
	if (idx < 0) return null;
	let i = src.indexOf("(", idx);
	if (i < 0) return null;
	let depth = 0, j = i;
	for (; j < src.length; j++) {
		const ch = src[j];
		if (ch === "(") depth++;
		else if (ch === ")") { depth--; if (depth === 0) break; }
	}
	return src.slice(i + 1, j);
}
const execSources = extractParen(cmake, "add_executable") || "";
const execMatch = cmake.includes("add_executable");

t("CMakeLists 里有 add_executable 段", () => {
	if (!execMatch) throw new Error("没找到 add_executable(");
});

t("每个模块的 .h 与 .cpp 都列进了 add_executable（AUTOMOC 三连坑③）", () => {
	const missing = [];
	for (const m of moduleFiles) {
		for (const ext of [".h", ".cpp"]) {
			const rel = `src/modules/${m}${ext}`;
			if (!execSources.includes(rel)) missing.push(rel);
		}
	}
	// modulehost 自身（宿主）同理
	for (const rel of ["src/modules/modulehost.h", "src/modules/modulehost.cpp"]) {
		if (!execSources.includes(rel)) missing.push(rel);
	}
	if (missing.length > 0)
		throw new Error(
			`未列入：${missing.join(", ")}\n        ⇒ moc 产物不生成会 LNK2001；` +
			`且改完必须手动 cmake 重配（file(GLOB) 不自动重查）`
		);
});

t("探针编译钩子 STE_USB_PROBE_FAKE_REMOVABLE 不出现在 CMakeLists（只许探针用）", () => {
	if (cmake.includes("STE_USB_PROBE_FAKE_REMOVABLE"))
		throw new Error("CMakeLists 里出现了探针宏 ⇒ 生产构建会把某盘符当 U 盘");
});

// ── 1. 逐模块解析 manifest ──
const mods = moduleFiles.map((f) => {
	const src = read(path.join(MODULES_DIR, `${f}.cpp`));
	const pick = (re) => {
		const m = src.match(re);
		return m ? m[1] : null;
	};
	const strList = (block) =>
		block
			? [...block.matchAll(/QStringLiteral\("([^"]+)"\)/g)].map((x) => x[1])
			: [];
	return {
		file: f,
		id: pick(/m\.id\s*=\s*QStringLiteral\("([^"]+)"\)/),
		name: pick(/m\.name\s*=\s*QStringLiteral\("([^"]+)"\)/),
		version: pick(/m\.version\s*=\s*QStringLiteral\("([^"]+)"\)/),
		description: pick(/m\.description\s*=\s*QStringLiteral\("([^"]+)"\)/),
		permissions: strList(pick(/m\.permissions\s*=\s*\{([^}]*)\}/)),
		actions: strList(pick(/m\.actions\s*=\s*\{([^}]*)\}/)),
		events: strList(pick(/m\.events\s*=\s*\{([^}]*)\}/)),
	};
});

t("每个模块 manifest 四要素齐全（id/name/version/description）", () => {
	for (const m of mods) {
		for (const k of ["id", "name", "version", "description"])
			if (!m[k]) throw new Error(`${m.file}: 缺 ${k}`);
	}
});

t("模块 id 全小写、跨模块唯一", () => {
	const seen = new Set();
	for (const m of mods) {
		if (m.id !== m.id.toLowerCase()) throw new Error(`${m.file}: id「${m.id}」不是全小写`);
		if (seen.has(m.id)) throw new Error(`id 重复：${m.id}`);
		seen.add(m.id);
	}
});

// 权限词表：与进程外插件同一份（PluginHost::kKnownPermissions）
const permBlock = pluginSrc.match(/kKnownPermissions\s*=\s*\{([\s\S]*?)\}/);
const knownPerms = permBlock
	? [...permBlock[1].matchAll(/QStringLiteral\("([^"]+)"\)/g)].map((x) => x[1])
	: [];

t("模块权限 ⊆ PluginHost::kKnownPermissions（两份词表同一口径）", () => {
	if (knownPerms.length === 0) throw new Error("没解析出 kKnownPermissions");
	for (const m of mods) {
		for (const p of m.permissions)
			if (!knownPerms.includes(p))
				throw new Error(`${m.file}: 权限「${p}」不在词表里（词表：${knownPerms.join("/")}）`);
	}
});

// 内置动作表：kActionNames[]
const actionBlock = mainSrc.match(/kActionNames\[\]\s*=\s*\{([\s\S]*?)\}/);
const builtinActions = actionBlock
	? [...actionBlock[1].matchAll(/"([^"]+)"/g)].map((x) => x[1])
	: [];

t("模块认领的 action 不与内置动作撞名（否则分派顺序决定行为，歧义）", () => {
	if (builtinActions.length === 0) throw new Error("没解析出 kActionNames");
	for (const m of mods)
		for (const a of m.actions)
			if (builtinActions.includes(a))
				throw new Error(`${m.file}: 认领了内置动作「${a}」`);
});

t("同一 action 不被两个模块同时认领（dispatch 歧义）", () => {
	const seen = new Map();
	for (const m of mods)
		for (const a of m.actions) {
			if (seen.has(a)) throw new Error(`「${a}」被 ${seen.get(a)} 与 ${m.file} 同时认领`);
			seen.set(a, m.file);
		}
});

t("模块订阅的事件名在 main.cpp 里真的有人投喂（没有死订阅）", () => {
	for (const m of mods)
		for (const e of m.events) {
			const fed = new RegExp(`forwardEvent\\s*\\(\\s*QStringLiteral\\("${e}"\\)`).test(mainSrc);
			if (!fed) throw new Error(`${m.file}: 订阅了「${e}」但没人 forwardEvent 它`);
		}
});

t("每个模块 .cpp 都在 main.cpp 里 registerModule 了（没有编了不启用的模块）", () => {
	// 两种合法登记写法都要认：
	//   ① mh->registerModule(new UsbModule());
	//   ② auto *vh = new VoiceHubModule(); mh->registerModule(vh);
	//      —— 需要拿指针去接线（注入岛/壳的 QML）时只能这么写，别把它判成漏登记。
	const direct = [...mainSrc.matchAll(/registerModule\s*\(\s*new\s+(\w+)\s*\(/g)].map((x) => x[1]);
	const vars = [...mainSrc.matchAll(/(?:auto\s*\*|(\w+)\s*\*)\s*(\w+)\s*=\s*new\s+(\w+)\s*\(/g)]
		.map((x) => ({ v: x[2], cls: x[3] }));
	const viaVar = vars
		.filter((p) => new RegExp(`registerModule\\s*\\(\\s*${p.v}\\s*\\)`).test(mainSrc))
		.map((p) => p.cls);
	const registered = [...direct, ...viaVar];
	for (const m of mods) {
		const cls = m.file.charAt(0).toUpperCase() + m.file.slice(1); // usb → Usb
		if (!registered.some((r) => r.toLowerCase() === cls.toLowerCase()))
			throw new Error(`${m.file}: 没找到 registerModule(new ${cls}…) 或 registerModule(指针变量)（已登记：${registered.join(", ") || "无"}）`);
	}
});

console.log(`\ntest-module-registry: ${pass} 通过 / ${fails.length} 失败`);
if (fails.length > 0) {
	console.log(`失败项：${fails.join(" / ")}`);
	process.exit(1);
}
