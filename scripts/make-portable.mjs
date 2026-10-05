#!/usr/bin/env node
/**
 * 打绿色包（被控端/管理端通用做法：从 windeployqt 的输出目录里挑出"运行时该带的"，压成一个 zip）。
 *
 * 为什么要有这个脚本（2026-10-06 立）：
 *   0.6.0 的管理端绿色包是**手工拼的**，结果包里同时存在两套东西：
 *     · 根层一套（viewer-qt.exe + platforms/ + tls/ + qml/ …）
 *     · 还嵌了一份 `dist\pkg-0.6.0\` 旧副本
 *   更糟的是**根层缺 `resources/`** —— QtWebEngine 的 icudtl.dat 与 qtwebengine_*.pak 全在
 *   那份嵌套副本里。WebEngine 的视图是**延迟创建**的（initRtcView 才建），所以程序照常启动、
 *   照常能连云端，只在"要看远程画面"时才炸 —— 这正是项目里反复出现的
 *   「打出来的包少了点东西、而且不报错」形态。
 *   所以把"包里必须有什么"写成可执行的清单：缺了就构建失败，不是靠下次有人想起来去比对。
 *
 * 用法：
 *   node scripts/make-portable.mjs                     # 从 build/ 出包，版本取 src/main.cpp 的 kViewerVersion
 *   node scripts/make-portable.mjs --check <zip>       # 只检查一个已存在的 zip（不改任何东西）
 *   node scripts/make-portable.mjs --from build --out dist
 *
 * 退出码：0 成功 / 1 失败
 */
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SEVEN_ZIP = "C:\\Program Files\\7-Zip\\7z.exe";

/**
 * 包里**必须**有的运行时件。少任何一个都要当场报错。
 * ⚠️ `resources/icudtl.dat` 就是 0.6.0 漏掉的那个 —— 它是 WebEngine（远程画面收流）的必需品。
 */
const REQUIRED = [
	"viewer-qt.exe",
	"Qt6Core.dll",
	"Qt6Gui.dll",
	"Qt6Widgets.dll",
	"Qt6Qml.dll",
	"Qt6Quick.dll",
	"Qt6QuickControls2.dll",
	"Qt6WebChannel.dll",
	"Qt6WebEngineCore.dll",
	"Qt6WebEngineWidgets.dll",
	"Qt6WebSockets.dll",
	"QtWebEngineProcess.exe",
	"resources/icudtl.dat",
	"platforms/qwindows.dll",
	"tls/qschannelbackend.dll" // 连云端的 WSS/TLS 靠它（schannel 是 Windows 自带证书链）
];

/** 要一并搬进包里的目录（windeployqt / QtWebEngine 产出的运行时目录）。 */
const INCLUDE_DIRS = [
	"Stelarith", // 我们自己 QML 模块的"磁盘副本"（运行期其实读的是编进 exe 的资源，见下）
	"generic",
	"iconengines",
	"imageformats",
	"networkinformation",
	"platforms",
	"position",
	"qml",
	"qmltooling",
	"resources", // ← WebEngine 的 icudtl.dat / *.pak
	"styles",
	"tls",
	"translations"
];

/** 构建内部产物，绝不该进包。 */
const EXCLUDE_NAMES = new Set([
	".ninja_deps",
	".ninja_log",
	".qt",
	".rcc",
	"CMakeCache.txt",
	"CMakeFiles",
	"build.ninja",
	"cmake_install.cmake",
	"meta_types",
	"qmltypes",
	"shots",
	"viewer-qt_autogen",
	"viewer-qt.exp",
	"viewer-qt.lib",
	"viewer-qt_qmltyperegistrations.cpp",
	"viewer-run.log",
	"agent-qt.log"
]);

/** 只为本地自检补的平台插件（offscreen），**不进包** —— 它是测试用的，不是产品的一部分。 */
const NEVER_SHIP = new Set(["platforms/qoffscreen.dll"]);

const log = (m) => console.log(m);
function fail(m) {
	console.error(`\n✗ ${m}\n`);
	process.exit(1);
}

function arg(name, def = undefined) {
	const i = process.argv.indexOf(`--${name}`);
	if (i < 0) return def;
	const v = process.argv[i + 1];
	return v === undefined || v.startsWith("--") ? true : v;
}
const has = (name) => process.argv.includes(`--${name}`);

/** 版本号从 C++ 那份唯一真源里读 —— 别让包名和 exe 里的版本号各说各话。 */
function readVersion() {
	const src = fs.readFileSync(path.join(ROOT, "src", "main.cpp"), "utf8");
	const m = src.match(/kViewerVersion\s*=\s*"([^"]+)"/);
	if (!m) fail("从 src/main.cpp 里读不到 kViewerVersion");
	return m[1];
}

function sha256(file) {
	const h = crypto.createHash("sha256");
	h.update(fs.readFileSync(file));
	return h.digest("hex");
}

function humanSize(n) {
	return `${(n / 1024 / 1024).toFixed(1)} MB`;
}

/** 用 7z 列包内条目（正斜杠归一化），用于 --check。 */
function listZip(zip) {
	const out = execFileSync(SEVEN_ZIP, ["l", "-ba", zip], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
	// 7z -ba 的列：date time attr size compressed name
	// ⚠️ 别用"取最后一个空格分隔词"的写法 —— 路径里有空格就会截断。按固定列数取剩余的整段。
	const RE = /^\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+\S+\s+\d+\s+\d+\s+(.*)$/;
	return out
		.split(/\r?\n/)
		.map((l) => {
			const m = l.match(RE);
			return m ? m[1].trim().replace(/\\/g, "/") : null;
		})
		.filter((s) => s && s !== "/");
}

// ── 检查一个包 ──────────────────────────────────────────────────────────────
function checkZip(zip) {
	if (!fs.existsSync(zip)) fail(`找不到包：${zip}`);
	const entries = new Set(listZip(zip));
	const problems = [];

	for (const need of REQUIRED) {
		if (!entries.has(need)) problems.push(`缺必需件：${need}`);
	}
	// 包里嵌了 dist/ 之类的旧副本 → 体积白涨、而且会让人误以为"包里有另一套"
	for (const e of entries) {
		if (/^dist\//.test(e)) {
			problems.push(`包里嵌了 dist/ 目录（旧副本？）：${e}`);
			break;
		}
	}
	// 自检用的 offscreen 插件不该出现在产品包里
	for (const e of NEVER_SHIP) {
		if (entries.has(e)) problems.push(`不该进包的测试件：${e}`);
	}

	log(`包：${path.basename(zip)}  ${humanSize(fs.statSync(zip).size)}  条目 ${entries.size}`);
	log(`必需件：${REQUIRED.length - problems.filter((p) => p.startsWith("缺必需件")).length}/${REQUIRED.length} 齐`);
	if (problems.length) {
		log("\n检查未通过：");
		for (const p of problems) log(`  · ${p}`);
		return false;
	}
	log("检查通过 ✓");
	return true;
}

// ── 出包 ────────────────────────────────────────────────────────────────────
function make(fromDir, outDir) {
	const version = readVersion();
	if (!fs.existsSync(fromDir)) fail(`源目录不存在：${fromDir}（先构建）`);
	if (!fs.existsSync(SEVEN_ZIP)) fail(`找不到 7-Zip：${SEVEN_ZIP}`);

	// 1) 先按清单核源目录（在拷之前就报错，别拷一半）
	const missing = REQUIRED.filter((f) => !fs.existsSync(path.join(fromDir, f)));
	if (missing.length) {
		fail(`构建输出里就缺这些必需件，先修构建再出包：\n  · ${missing.join("\n  · ")}`);
	}

	// 2) 组装干净的一层目录（不嵌套、不带旧副本）
	const pkg = path.join(outDir, `pkg-${version}`);
	fs.rmSync(pkg, { recursive: true, force: true });
	fs.mkdirSync(pkg, { recursive: true });
	log(`组装 → ${path.relative(ROOT, pkg)}`);

	const copied = [];
	for (const ent of fs.readdirSync(fromDir, { withFileTypes: true })) {
		if (EXCLUDE_NAMES.has(ent.name)) continue;
		const src = path.join(fromDir, ent.name);
		const dst = path.join(pkg, ent.name);
		if (ent.isDirectory()) {
			if (!INCLUDE_DIRS.includes(ent.name)) {
				log(`  跳过未列入清单的目录：${ent.name}`);
				continue;
			}
			fs.cpSync(src, dst, { recursive: true });
			copied.push(ent.name + "/");
		} else if (ent.isFile()) {
			if (ent.name.endsWith(".dll") || ent.name.endsWith(".exe") || ent.name.endsWith(".pak") || ent.name.endsWith(".dat")) {
				fs.copyFileSync(src, dst);
				copied.push(ent.name);
			}
		}
	}

	// 3) 拿掉"绝不进包"的东西（比如我为了自检补进去的 qoffscreen.dll）
	for (const rel of NEVER_SHIP) {
		const p = path.join(pkg, rel);
		if (fs.existsSync(p)) {
			fs.rmSync(p);
			log(`  移除测试件：${rel}`);
		}
	}

	// 4) Stelarith/qml 的磁盘副本必须与源码一致 —— 0.6.0 那次它停在 4 天前
	//    （运行期读的是编进 exe 的资源，所以这份不一致不会立刻出事，只会让"包里有旧 QML"误导人）
	const srcQml = path.join(ROOT, "qml");
	const pkgQml = path.join(pkg, "Stelarith", "qml");
	if (fs.existsSync(pkgQml)) {
		for (const f of fs.readdirSync(srcQml)) {
			const a = path.join(srcQml, f);
			const b = path.join(pkgQml, f);
			if (!fs.existsSync(b)) {
				log(`  ⚠ Stelarith/qml 里缺 ${f}（运行期不读它，但会让包内容与源码不一致）`);
				continue;
			}
			if (sha256(a) !== sha256(b)) log(`  ⚠ Stelarith/qml/${f} 与源码不一致（陈旧副本）`);
		}
	}

	// 5) 压缩：**从包目录里面**压内容，让 zip 根层就是应用目录（不嵌套一层）
	fs.mkdirSync(outDir, { recursive: true });
	const zip = path.join(outDir, `stelarith-viewer-portable-${version}.zip`);
	fs.rmSync(zip, { force: true });
	log(`压缩 → ${path.relative(ROOT, zip)}（-mx=1，快压；这是个 170MB 级的包）`);
	execFileSync(SEVEN_ZIP, ["a", "-tzip", "-mx=1", zip, ".\\*"], { cwd: pkg, stdio: "inherit" });

	const st = fs.statSync(zip);
	log(`\n完成：${zip}`);
	log(`  版本   ${version}（读自 src/main.cpp 的 kViewerVersion）`);
	log(`  体积   ${st.size} (${humanSize(st.size)})`);
	log(`  sha256 ${sha256(zip)}`);
	log(`\n自检：node scripts/make-portable.mjs --check "${zip}"`);

	// 6) 出完立刻自检 —— 只会通过的脚本等于没有脚本
	return checkZip(zip);
}

// ── 入口 ────────────────────────────────────────────────────────────────────
const argv = process.argv.slice(2);
let ok;
if (argv[0] === "--check") {
	ok = checkZip(argv[1] || "");
} else {
	const from = typeof arg("from") === "string" ? path.resolve(ROOT, arg("from")) : path.join(ROOT, "build");
	const out = typeof arg("out") === "string" ? path.resolve(ROOT, arg("out")) : path.join(ROOT, "dist");
	ok = make(from, out);
}
process.exit(ok ? 0 : 1);
