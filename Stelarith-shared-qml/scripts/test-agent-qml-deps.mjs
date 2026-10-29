#!/usr/bin/env node
/**
 * 被控端 QML「部署约束」门禁（2026-10-16 立 · 真机事故驱动）
 *
 * ── 事故 ─────────────────────────────────────────────────────────────────────
 * 被控端 QML 控制面板打开后**只有顶栏**，内容区与底导航一起消失（白底、主日志零 QML ERROR）。
 * 真机日志里只有宿主的两行：
 *     [UiPageHost] entry 解析失败: …/AgentSettingsPage.qml:60:9: ScrollBar is not a type
 *     [UiPageHost] entry 解析失败: …/AgentPlaySchedulePage.qml:2:1: Cannot load library
 *         …\qml\QtQuick\Controls\qtquickcontrols2plugin.dll: 找不到指定的模块
 *
 * 根因：这两页用了 **QtQuick.Controls**（`ScrollBar` / `TextArea`），
 * 而**被控端的部署不含 QtQuick Controls 运行库**（exe 旁没有 Qt6QuickControls2.dll，
 * 装到安装目录的 qml/QtQuick/Controls/ 自然也就加载不了）。默认页（设置）加载失败
 * ⇒ 打开面板就是一片空白。**声明合法、qmllint 零 Error、主日志零错误** —— 最难查的一类。
 *
 * ── 纪律 ─────────────────────────────────────────────────────────────────────
 * **被控端 `qml/` 下的 QML 只能用「部署里确实有」的模块。**
 * 白名单：`QtQuick`、`QtQuick.Window`、相对目录导入（`"../components"`）、JS 导入。
 * 想用别的模块（`QtQuick.Layouts` 之类也一样）＝**先去确认部署里有那个 dll**，再加白名单；
 * 别在教室机上现场收获一句 `is not a type`。
 *
 * 用法：node scripts/test-agent-qml-deps.mjs
 * 退出码：0 全过 / 1 有失败
 *
 * ⚠️ 只扫 `control-qt/qml/shell/**`（进 exe 的壳与页）。`qml/Showcase.qml` 是**不进 exe**
 *    的 qml.exe 演示页，它想用什么都行，故不在范围内。
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BASE = path.resolve(ROOT, "..");
const AGENT_QML = path.join(BASE, "Stelarith-control-qt", "qml", "shell");

/** 部署里**确实有**的 QML 模块（白名单）。加之前先确认 exe 旁有对应 dll。 */
// 2026-10-30 补 QtQuick.Layouts / QtQuick.Effects：
// 此前白名单只有 QtQuick / QtQuick.Window，但这两模块**一直在部署**——
//   · 构建脚本每次打印：`QML 模块部署完整：QtQuick.Controls;QtQuick.Effects;QtQuick.Layouts;QtQuick.Window`；
//   · 生产安装目录实证：`C:\Program Files\Stelarith\qml\QtQuick\` 下 Layouts / Effects 都在；
//   · 共享库 App* 组件（AppCard 用 MultiEffect、AppInfoRow/AppCardTitle/AppSwitchRow 用 Layout）
//     在真机截图里渲染正常。
// 白名单过时导致「被控端全面迁移到 App* 体系」时 15 个页面被误判为引入未部署模块。
// 这不是放宽标准：Controls 仍不在白名单（真机事故就是它），黑名单 40 个类型照旧。
const OK_MODULES = ["QtQuick", "QtQuick.Window", "QtQuick.Layouts", "QtQuick.Effects"];

/** 只在 QtQuick.Controls 里存在的类型名（被控端 shell 下不该出现）。 */
const CONTROLS_TYPES = [
	"ScrollBar", "ScrollView", "ScrollIndicator", "TextArea", "TextField", "ComboBox",
	"SpinBox", "Slider", "RangeSlider", "Dial", "Switch", "CheckBox", "RadioButton",
	"Button", "RoundButton", "DelayButton", "ToolButton", "Label", "BusyIndicator",
	"ProgressBar", "PageIndicator", "Tumbler", "ToolBar", "TabBar", "TabButton",
	"StackView", "SwipeView", "Drawer", "DialogButtonBox", "Popup", "Dialog",
	"MenuItem", "MenuSeparator", "ToolSeparator", "SplitView", "ItemDelegate",
	"CheckDelegate", "RadioDelegate", "SwitchDelegate", "SelectionRectangle",
];

/** 剥掉注释（门禁正则必须先剥注释，否则会命中「说明这个不能用」的文字 ⇒ 假红）。 */
function stripComments(src) {
	return src
		.replace(/\/\*[\s\S]*?\*\//g, "")
		.split(/\r?\n/)
		.map((l) => l.replace(/\/\/.*$/, ""))
		.join("\n");
}

function walk(dir, out = []) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) walk(p, out);
		else if (e.name.endsWith(".qml")) out.push(p);
	}
	return out;
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

console.log(`── 被控端 QML 部署约束（${path.relative(BASE, AGENT_QML).replace(/\\/g, "/")}）──`);

if (!fs.existsSync(AGENT_QML)) {
	console.log(`  FAIL  目录不存在：${AGENT_QML}`);
	process.exit(1);
}
const files = walk(AGENT_QML).sort();
console.log(`  （扫描 ${files.length} 个 .qml）`);

for (const f of files) {
	const rel = path.relative(BASE, f).replace(/\\/g, "/");
	const code = stripComments(fs.readFileSync(f, "utf8"));

	t(`${rel}：import 的模块都在部署白名单内（${OK_MODULES.join(" / ")} / 相对目录 / JS）`, () => {
		const bad = [];
		for (const line of code.split(/\r?\n/)) {
			const m = line.match(/^\s*import\s+([^\s;]+)(?:\s+\d+\.\d+)?\s*(?:as\s+\w+)?\s*$/);
			if (!m) continue;
			const mod = m[1];
			if (mod.startsWith('"') || mod.startsWith("'")) continue; // 相对目录 / JS
			if (!OK_MODULES.includes(mod)) bad.push(mod);
		}
		if (bad.length > 0)
			throw new Error(
				`用了部署白名单外的模块：${[...new Set(bad)].join(", ")}\n` +
				`        ⇒ 被控端部署里没有它（真机事故就是 QtQuick.Controls 这么来的）。` +
				`要用先确认 exe 旁有对应 dll，再把模块名加进本脚本的 OK_MODULES。`
			);
	});

	t(`${rel}：没有 QtQuick.Controls 专有类型（${CONTROLS_TYPES.length} 个黑名单）`, () => {
		const hits = [];
		for (const ty of CONTROLS_TYPES) {
			const re = new RegExp(`\\b${ty}\\b`);
			if (re.test(code)) hits.push(ty);
		}
		if (hits.length > 0)
			throw new Error(
				`出现 Controls 专有类型：${hits.join(", ")}\n` +
				`        ⇒ 该页会在被控端加载失败（"X is not a type"）。` +
				`改用 QtQuick 基础类型或共享 md3/ 组件（如 Md3Btn / Md3SwitchRow）。`
			);
	});
}

console.log(`\n${"─".repeat(60)}`);
console.log(`test-agent-qml-deps: ${pass} 通过 / ${fails.length} 失败`);
if (fails.length > 0) {
	console.log(`失败项：${fails.join(" / ")}`);
	process.exit(1);
}
