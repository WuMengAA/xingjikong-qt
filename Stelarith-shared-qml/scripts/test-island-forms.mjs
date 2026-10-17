#!/usr/bin/env node
/**
 * 灵动岛**形态清单**的三方一致性门禁（2026-10-17 立 · #330 形态多样化）
 *
 * 为什么有它：一个"形态"要在**三处**同时认得，任一处漏了都会静默降级：
 *   ① C++ `IslandOverlay::Form`（枚举）—— 几何真源（geometryForForm 按它算窗口）
 *   ② `island_bridge.cpp::form()`（字符串）—— QML 唯一能看见的形态信号
 *   ③ `IslandRoot.qml`（`_formKind` 判定 + 对应面组件）
 *
 * 漏掉的典型症状（都是"零错误"的）：
 *   · 加了枚举但桥没给字符串 ⇒ QML 永远收到 "capsule"，按胶囊排版塞进 72px 窗口；
 *   · 桥给了字符串但 QML 没判定 ⇒ 面组件不显示，窗口里一片**透明空气**；
 *   · QML 判了但没把面组件实例化 ⇒ 同上；
 *   · 面组件装了但没让胶囊那套内容让位 ⇒ 两套版式叠在一起（文字互相压）。
 * 这些编得过、跑得动、日志干净 —— 只能靠机器盯。
 *
 * 用法：node scripts/test-island-forms.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 与 test-island-metrics.mjs 同族：**跨仓读真源**（island.h/.cpp/bridge/main.cpp），
 *    红了说明某一端没跟上，别当噪声关掉。匹配前一律先剥注释（门禁自己被注释晃过）。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPO = path.join(ROOT, "..");
const ISLAND_H = path.join(REPO, "Stelarith-control-qt", "src", "island.h");
const ISLAND_CPP = path.join(REPO, "Stelarith-control-qt", "src", "island.cpp");
const BRIDGE_CPP = path.join(REPO, "Stelarith-control-qt", "src", "island_bridge.cpp");
const MAIN_CPP = path.join(REPO, "Stelarith-control-qt", "src", "main.cpp");
const ROOT_QML = path.join(ROOT, "island", "IslandRoot.qml");
const BANNER_QML = path.join(ROOT, "island", "IslandBannerFace.qml");
const MINI_QML = path.join(ROOT, "island", "IslandMiniFace.qml");

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
function ok(cond, label) {
	if (!cond) throw new Error(label || "断言失败");
}
const read = (p) => {
	if (!fs.existsSync(p)) throw new Error(`文件不存在：${p}`);
	return fs.readFileSync(p, "utf8");
};
// 剥注释：`//` 行注释 + `/* */` 块注释。⚠️ 门禁被注释晃过（/kMinContentW/ 命中说明性注释），
// 这里统一先剥再匹配；字符串里的 `//`（URL 之类）不被误剥 —— 本组文件里形态串都是纯字面量。
function stripComments(src) {
	return src
		.replace(/\/\*[\s\S]*?\*\//g, "")
		.replace(/(^|[^:])\/\/.*$/gm, "$1");
}

// ── 一、C++ 枚举是真源 ────────────────────────────────────────────
const hSrc = stripComments(read(ISLAND_H));
const cppSrc = stripComments(read(ISLAND_CPP));
const bridgeSrc = stripComments(read(BRIDGE_CPP));
const mainSrc = stripComments(read(MAIN_CPP));
const rootQml = stripComments(read(ROOT_QML));

const enumMatch = hSrc.match(/enum class Form\s*\{([^}]*)\}/);
ok(enumMatch, "island.h 里找不到 `enum class Form { … }`（枚举被改名了？）");
const forms = enumMatch[1]
	.split(",")
	.map((s) => s.trim())
	.filter(Boolean);
// 每档的"标识名 → 期望的 QML 形态串"
const EXPECT = {
	Capsule: "capsule",
	Centered: "centered",
	Fullscreen: "fullscreen",
	Banner: "banner",
	Mini: "mini",
};

t("Form 枚举档位与三端清单一致（加档必须同时改桥 + QML，见文件头）", () => {
	const unknown = forms.filter((f) => !(f in EXPECT));
	ok(
		unknown.length === 0,
		`枚举里有本门禁不认识的档：${unknown.join(", ")} —— 新加形态必须同步：` +
			`island_bridge.cpp::form()、IslandRoot.qml 的 _formKind、本文件的 EXPECT`
	);
	const missing = Object.keys(EXPECT).filter((k) => !forms.includes(k));
	ok(missing.length === 0, `枚举缺档：${missing.join(", ")}`);
});

// ── 二、桥：每个非 default 档都要显式返回小写串 ────────────────────
t("island_bridge::form() 为每档 Form 给出对应字符串（Capsule 允许走 default）", () => {
	for (const [cls, str] of Object.entries(EXPECT)) {
		if (cls === "Capsule") continue; // Capsule 在 switch 里既有 case 也有 default，两种写法都行
		const re = new RegExp(
			`case\\s+IslandOverlay::Form::${cls}\\s*:\\s*return\\s+QStringLiteral\\("${str}"\\)`
		);
		ok(re.test(bridgeSrc), `island_bridge.cpp 没把 Form::${cls} 映射成 "${str}"`);
	}
	// 反向：bridge 里 return 的形态串不能有 EXPECT 之外的
	const returns = [...bridgeSrc.matchAll(/case\s+IslandOverlay::Form::\w+\s*:\s*return\s+QStringLiteral\("([a-z]+)"\)/g)]
		.map((m) => m[1]);
	const allowed = new Set(Object.values(EXPECT));
	const stray = returns.filter((s) => !allowed.has(s));
	ok(stray.length === 0, `bridge 返回了未登记的形态串：${stray.join(", ")}`);
});

// ── 三、QML：形态串有判定、面组件有实例化、且互斥 ──────────────────
t("IslandRoot 对 banner/mini 都有 `_formKind ===` 判定", () => {
	for (const s of ["banner", "mini"]) {
		ok(
			new RegExp(`_formKind\\s*===\\s*"${s}"`).test(rootQml),
			`IslandRoot.qml 没有 \`_formKind === "${s}"\` 判定 —— 该形态会被当胶囊排版`
		);
	}
});

t("_compactForm 覆盖 banner 与 mini（紧凑外框统一判据）", () => {
	const m = rootQml.match(/readonly\s+property\s+bool\s+_compactForm\s*:\s*([^\n]*)/);
	ok(m, "IslandRoot.qml 里找不到 _compactForm 定义");
	for (const s of ["banner", "mini"])
		ok(m[1].includes(`"${s}"`), `_compactForm 没包含 "${s}"`);
});

t("胶囊那套内容在紧凑形态下必须让位（否则两套版式叠字）", () => {
	ok(
		/visible:\s*!root\._idle\s*&&\s*!root\._compactForm/.test(rootQml),
		'找不到「visible: !root._idle && !root._compactForm」—— 胶囊内容没给紧凑形态让位'
	);
});

t("两个面组件都存在、被实例化、visible 绑到对应形态串", () => {
	for (const [file, id, str] of [
		[BANNER_QML, "IslandBannerFace", "banner"],
		[MINI_QML, "IslandMiniFace", "mini"],
	]) {
		ok(fs.existsSync(file), `缺面组件文件：${path.basename(file)}`);
		const inst = new RegExp(
			`${id}\\s*\\{[\\s\\S]{0,600}?visible:\\s*root\\._formKind\\s*===\\s*"${str}"`
		);
		ok(inst.test(rootQml), `IslandRoot.qml 没把 ${id} 实例化并绑到 "${str}"`);
	}
});

t("面组件显式接依赖（th/icon/title/desc）—— 隐式作用域不跟着搬文件", () => {
	const inst = rootQml.match(/IslandBannerFace\s*\{[\s\S]{0,600}?\}/);
	ok(inst, "找不到 IslandBannerFace 实例化块");
	for (const p of ["th", "icon", "title", "desc"])
		ok(new RegExp(`\\b${p}:\\s*root\\.`).test(inst[0]), `IslandBannerFace 没显式传 ${p}`);
});

t("鲸鱼在紧凑形态下隐藏（96px 角标放不下 28px 鲸鱼）", () => {
	ok(
		/visible:\s*!root\._compactForm/.test(rootQml),
		"鲸鱼没有按 _compactForm 隐藏 —— 会盖住角标标题 / 横幅描述"
	);
});

// ── 四、C++ 几何：新形态的尺寸真源与"圆/方"判定 ────────────────────
t("geometryForForm 有 Banner / Mini 分支，且用 availableGeometry（避开任务栏）", () => {
	const banner = /if\s*\(f\s*==\s*Form::Banner\)\s*\{([\s\S]{0,600}?)\n\s*\}/.exec(cppSrc);
	const mini = /if\s*\(f\s*==\s*Form::Mini\)\s*\{([\s\S]{0,600}?)\n\s*\}/.exec(cppSrc);
	ok(banner, "island.cpp 里找不到 geometryForForm 的 Banner 分支");
	ok(mini, "island.cpp 里找不到 geometryForForm 的 Mini 分支");
	ok(/availableGeometry\(\)/.test(banner[1]), "Banner 分支没用 availableGeometry（任务栏在顶部会被压住）");
	ok(/availableGeometry\(\)/.test(mini[1]), "Mini 分支没用 availableGeometry（会压进底部任务栏）");
	// 圆/方判定：Mini 分支必须按"描述为空"分档，与 QML 的 _miniOrb 同口径
	ok(
		/m_desc\.isEmpty\(\)/.test(mini[1]),
		"Mini 分支没按 m_desc.isEmpty() 分档 —— 与 QML 的 _miniOrb 口径不一致会出现「窗口圆、底色方」"
	);
});

t("QML 的 _miniOrb 与 C++ 同口径（按描述是否为空）", () => {
	const m = rootQml.match(/readonly\s+property\s+bool\s+_miniOrb\s*:\s*([\s\S]{0,200}?)\n\s*(?:readonly|property|Item)/);
	ok(m, "IslandRoot.qml 里找不到 _miniOrb 定义");
	ok(/desc\s*===\s*""/.test(m[1]), '_miniOrb 没用 `desc === ""` 判定');
});

t("紧凑形态尺寸常量只有一处真源（Banner/Mini 的盒子尺寸）", () => {
	for (const k of ["kBannerH", "kBannerHFlat", "kMiniBox", "kMiniOrb", "kMiniMargin"]) {
		const def = new RegExp(`static\\s+const\\s+int\\s+${k}\\s*=`).test(cppSrc);
		const use = (cppSrc.match(new RegExp(`\\b${k}\\b`, "g")) || []).length;
		ok(def, `island.cpp 缺常量 ${k}`);
		ok(use >= 2, `常量 ${k} 声明了却没被用（values 漂了？）`);
	}
});

// ── 五、按钮降级与线上 kind 映射 ──────────────────────────────────
t("showWithActions：紧凑形态 + 按钮必须降级 Centered（否则按钮被裁在窗外）", () => {
	const fn = /void\s+IslandOverlay::showWithActions[\s\S]{0,1200}?showIsland\(/.exec(cppSrc);
	ok(fn, "找不到 showWithActions 的实现体");
	const guard = /Form::Banner\s*\|\|\s*form\s*==\s*Form::Mini[\s\S]{0,200}?Form::Centered|form\s*==\s*Form::Mini[\s\S]{0,80}?Form::Banner[\s\S]{0,200}?Form::Centered/.test(
		fn[0]
	);
	ok(guard, "showWithActions 缺「Banner/Mini + 按钮 ⇒ 降级 Centered」守卫 —— 按钮会被裁掉且点不到");
});

t("线上 kind → Form 映射覆盖 banner/mini（管理端可下发这两档）", () => {
	for (const [kind, cls] of [["banner", "Banner"], ["mini", "Mini"]]) {
		const re = new RegExp(`kind\\s*==\\s*QLatin1String\\("${kind}"\\)\\s*\\)\\s*f\\s*=\\s*IslandOverlay::Form::${cls}`);
		ok(re.test(mainSrc), `main.cpp 的 kind 分流没把 "${kind}" 映射到 Form::${cls}`);
	}
	// 未知 kind 仍必须落 Centered（安全默认不能被新分支带偏）
	ok(
		/IslandOverlay::Form\s+f\s*=\s*IslandOverlay::Form::Centered/.test(mainSrc),
		"kind 分流的默认值不是 Centered —— 未知 kind 会落错形态"
	);
});

console.log(`\n════ ${pass} 通过 / ${fails.length} 失败 ════`);
if (fails.length) {
	console.log("失败项：" + fails.join(" / "));
	process.exit(1);
}
