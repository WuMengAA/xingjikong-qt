#!/usr/bin/env node
/**
 * 打被控端绿色包（stelarith-agent-qt）
 *
 * 为什么要有这个脚本（2026-10-07 立）：
 *   被控端的绿色包一直是**手工拼的** —— 从 deploy/ 素材库挑文件复制，再压 zip。
 *   0.6.4 / 0.6.5 两次都漏了 `resources/`（icudtl.dat + qtwebengine_*.pak）
 *   与 `translations/qtwebengine_locales/`，因为 **deploy/ 素材库里根本没有这两个目录**
 *   （它们只在 build/ 里，来自 windeployqt）。
 *
 *   后果和 0.6.0 管理端那次一模一样：程序照常启动、照常连云端、帧也照推，
 *   只在"有人订阅画面 → 建 WebEngine 视图"那一刻崩：
 *     事件日志  stelarith-agent-qt.exe / 出错模块 ucrtbase.dll / 异常码 0xc0000409
 *               （偏移固定 0xa527e），进程只活 3~5 秒
 *     程序输出  The following paths were searched for Qt WebEngine resources: … but could not find any.
 *   用户看到的就是"被控端闪退"。
 *
 *   所以把"包里必须有什么"写成可执行清单：缺了当场失败。
 *   ⚠️ 以后新增任何一种必须配送的文件，加进 REQUIRED；忘了加 = 下次现场才发现。
 *
 * 用法：
 *   node scripts/make-portable-agent.mjs                    # 从 build/ + deploy/ 出包到 dist/
 *   node scripts/make-portable-agent.mjs --check <zip>      # 只检查一个已存在的 zip（不改任何东西）
 *
 * 退出码：0 成功 / 1 失败
 */
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BUILD = path.join(ROOT, "build");
const DEPLOY = path.join(ROOT, "deploy");
const SEVEN_ZIP = "C:\\Program Files\\7-Zip\\7z.exe";

/**
 * 包里**必须**有的运行时件。少任何一个都当场报错。
 * ⚠️ `resources/icudtl.dat` 与 `translations/qtwebengine_locales/en-US.pak`
 *    就是 0.6.4/0.6.5 漏掉的那几个 —— WebEngine（远程画面推流）的必需品。
 */
const REQUIRED = [
	"stelarith-agent-qt.exe",
	"Qt6Core.dll",
	"Qt6Gui.dll",
	"Qt6Widgets.dll",
	"Qt6Network.dll",
	"Qt6Qml.dll",
	"Qt6Quick.dll",
	"Qt6QuickWidgets.dll",
	"Qt6QmlMeta.dll",
	"Qt6QmlModels.dll",
	"Qt6QmlWorkerScript.dll",
	"Qt6WebChannel.dll",
	"Qt6WebEngineCore.dll",
	"Qt6WebEngineWidgets.dll",
	"Qt6WebSockets.dll",
	"Qt6Svg.dll",
	"Qt6Positioning.dll",
	"Qt6PrintSupport.dll",
	"Qt6OpenGL.dll",
	"QtWebEngineProcess.exe",
	// 2026-10-10 补：WebRTC 走 libdatachannel，其 DTLS 链的是 PostgreSQL 17 自带的
	// OpenSSL ⇒ exe 导入表含 libssl-3-x64.dll / libcrypto-3-x64.dll。windeployqt 不认识
	// 这两个名字（只认 Qt 全家），漏掉 = 干净机器解压双击即
	// 「stelarith-agent-qt.exe - 系统错误：找不到 libssl-3-x64.dll」。
	// ⚠️ 它们**不在 deploy/ 素材库里**（见 FROM_BUILD_FILES），所以必须点名 + 从 build/ 取。
	"libssl-3-x64.dll",
	"libcrypto-3-x64.dll",
	// ↓↓↓ 事故漏掉的三件套
	"resources/icudtl.dat",
	"resources/qtwebengine_resources.pak",
	"resources/v8_context_snapshot.bin",
	"translations/qtwebengine_locales/en-US.pak",
	// ↓↓↓ 插件目录
	"platforms/qwindows.dll",
	"tls/qschannelbackend.dll", // 连云端的 wss 靠它（schannel 是 Windows 自带证书链）
	"position/qtposition_nmea.dll", // Qt6Positioning.dll 已带，插件目录缺了配不上
	// ↓↓↓ 免安装运行必需
	"agent.env.example",
	"start-agent.bat",
];

/**
 * 这些目录**只在 build/ 里**（windeployqt 产出），deploy/ 素材库没有。
 * 手工拼包漏掉它们，就是这个脚本存在的理由 —— 所以从 build/ 显式取。
 *
 * ⚠️ `position` 是 2026-10-08 补进来的：REQUIRED 里一直列着
 *    `position/qtposition_nmea.dll`，但素材库 deploy/ 从没放过 position/，
 *    于是**每次出包都会失败在自检这一步**（这脚本的清单抓对了，取件列表却漏了）。
 *    和 resources/translations 同一性质：只在 build/ 有。
 */
const FROM_BUILD = ["resources", "translations", "position"];

/**
 * 只从 build/ 取、deploy/ 素材库里**没有**的散件（非目录）。
 *
 * ⚠️ 2026-10-10 补：libssl-3-x64.dll / libcrypto-3-x64.dll 是 libdatachannel 的 DTLS
 *    依赖（见 REQUIRED 注释）。既然 deploy/ 从没放过它们，"从 deploy/ 拷 DLL"
 *    这条老路就永远带不上 ⇒ 绿包在干净机器上启动即「找不到 libssl-3-x64.dll」。
 *    和 resources/translations 同一性质：只在 build/ 有，必须显式取。
 */
const FROM_BUILD_FILES = ["libssl-3-x64.dll", "libcrypto-3-x64.dll"];

/**
 * 绝不进发布包：
 *   agent.env      —— 每台机器独立配置，且含**真实设备令牌**，打进去等于谁下载谁能冒充设备
 *   TEST1-NOTES.txt —— 测试机笔记（deploy/ 里的调试残留）
 */
const NEVER_SHIP = ["agent.env", "TEST1-NOTES.txt"];

function fail(msg) {
	console.error(`✗ ${msg}`);
	process.exit(1);
}

function humanSize(n) {
	return `${(n / 1024 / 1024).toFixed(1)} MB`;
}

/** 版本号唯一真源：src/main.cpp 的 kAppVersion（别在脚本里再抄一份）。 */
function readVersion() {
	const src = fs.readFileSync(path.join(ROOT, "src", "main.cpp"), "utf8");
	const m = src.match(/kAppVersion\s*=\s*"([^"]+)"/);
	if (!m) fail("src/main.cpp 里找不到 kAppVersion");
	return m[1];
}

function copyDir(src, dst, exclude = []) {
	fs.mkdirSync(dst, { recursive: true });
	for (const e of fs.readdirSync(src, { withFileTypes: true })) {
		if (exclude.includes(e.name)) continue;
		const s = path.join(src, e.name);
		const d = path.join(dst, e.name);
		if (e.isDirectory()) copyDir(s, d, exclude);
		else fs.copyFileSync(s, d);
	}
}

/** 用 7z 列包内条目（正斜杠归一化）。 */
function listZip(zip) {
	const out = execFileSync(SEVEN_ZIP, ["l", "-ba", zip], {
		encoding: "utf8",
		maxBuffer: 64 * 1024 * 1024,
	});
	// 7z -ba 的列：date time attr size compressed name
	// ⚠️ 别用"取最后一个空格分隔词"—— 路径里有空格就会截断。按固定列数取剩余的整段。
	const RE = /^\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+\S+\s+\d+\s+\d+\s+(.*)$/;
	return out
		.split(/\r?\n/)
		.map((l) => {
			const m = l.match(RE);
			return m ? m[1].trim().replace(/\\/g, "/") : null;
		})
		.filter((s) => s && s !== "/");
}

function checkZip(zip) {
	if (!fs.existsSync(zip)) fail(`找不到包：${zip}`);
	const names = new Set(listZip(zip));
	const missing = REQUIRED.filter((r) => !names.has(r));
	console.log(`\n包：${path.basename(zip)}  ${humanSize(fs.statSync(zip).size)}  条目 ${names.size}`);
	console.log(`必需件：${REQUIRED.length - missing.length}/${REQUIRED.length}`);
	if (missing.length) {
		console.error("✗ 缺必需件：");
		for (const m of missing) console.error(`    ${m}`);
		fail("包不完整 —— 别发这种包。");
	}
	console.log("检查通过 ✓");
}

function build() {
	if (!fs.existsSync(SEVEN_ZIP)) fail(`找不到 7-Zip：${SEVEN_ZIP}`);
	if (!fs.existsSync(BUILD)) fail(`找不到构建目录：${BUILD}（先跑 scripts/build-agent.sh）`);
	if (!fs.existsSync(DEPLOY)) fail(`找不到素材目录：${DEPLOY}`);

	const ver = readVersion();
	const outDir = path.join(ROOT, "dist", `agent-portable-${ver}`);
	const zip = path.join(ROOT, "dist", `stelarith-agent-portable-${ver}.zip`);
	const exe = path.join(BUILD, "stelarith-agent-qt.exe");
	if (!fs.existsSync(exe)) fail(`找不到 exe：${exe}`);

	console.log(`版本 ${ver}（读自 src/main.cpp 的 kAppVersion）`);
	console.log(`组装 → ${outDir}`);

	// ① deploy/ 素材库（DLL / 插件目录 / 免安装件），剔除绝不外发的
	copyDir(DEPLOY, outDir, NEVER_SHIP);

	// ② exe 一律用 build/ 里**刚编出来的**那份（deploy/ 里那份是旧快照）
	fs.copyFileSync(exe, path.join(outDir, "stelarith-agent-qt.exe"));

	// ③ WebEngine 运行时资源 —— 只在 build/ 里，deploy/ 没有。（就是这次事故漏的东西）
	for (const d of FROM_BUILD) {
		const s = path.join(BUILD, d);
		if (!fs.existsSync(s)) fail(`build/${d} 不存在 —— windeployqt 没跑过？`);
		copyDir(s, path.join(outDir, d));
		console.log(`  + ${d}/（来自 build/，deploy/ 没有）`);
	}

	// ③b OpenSSL 运行库散件 —— 同样只在 build/，deploy/ 没有（2026-10-10 补）
	for (const f of FROM_BUILD_FILES) {
		const s = path.join(BUILD, f);
		if (!fs.existsSync(s)) {
			fail(`build/${f} 不存在 —— 它不是 windeployqt 产出的，需手工从 ` +
				`"C:\\Program Files\\PostgreSQL\\17\\bin" 拷进 build/（见 REQUIRED 注释）`);
		}
		fs.copyFileSync(s, path.join(outDir, f));
		console.log(`  + ${f}（来自 build/，deploy/ 没有）`);
	}

	// ④ 硬校验（在打包**之前**，别等打完才发现）
	for (const r of REQUIRED) {
		if (!fs.existsSync(path.join(outDir, r))) fail(`必需件缺失：${r}`);
	}
	console.log(`必需件预检：${REQUIRED.length}/${REQUIRED.length} 齐`);

	// ⑤ 压包。7z 直接吃目录内容 ⇒ 包内是扁平结构（顶层即文件/目录名）
	execFileSync(SEVEN_ZIP, ["a", "-tzip", "-mx=1", zip, ".\\*"], {
		cwd: outDir,
		stdio: "inherit",
	});

	const size = fs.statSync(zip).size;
	const sha = crypto.createHash("sha256").update(fs.readFileSync(zip)).digest("hex");
	console.log(`\n包：${path.basename(zip)}  ${size} (${humanSize(size)})`);
	console.log(`sha256 ${sha}`);

	checkZip(zip);
	console.log(`\n自检：node scripts/make-portable-agent.mjs --check "${zip}"`);
}

const args = process.argv.slice(2);
if (args[0] === "--check") {
	if (!args[1]) fail("用法：--check <zip>");
	checkZip(path.resolve(args[1]));
} else {
	build();
}
