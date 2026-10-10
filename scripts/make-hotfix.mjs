#!/usr/bin/env node
/**
 * 星集控 · 极小热修包（hotfix，2026-10-09 立）
 *
 * 与「全量包」的分工：
 *   · **NSIS 安装包**（stelarith-<端>-setup-*.exe）＝ 首次装机 / 大版本 / 运行时（DLL、
 *     Qt 插件、resources）有变时用。管理端全量 128MB、被控端全量 114MB。
 *   · **热修包**（本脚本产出）＝ 只含**会随代码变化的少数文件**，走 OTA 推给已经装好的
 *     机器。管理端 ~0.6MB、被控端 ~1MB —— 差两个数量级。
 *
 * 两端的形态**不一样**，这是被各自的 OTA 链路决定的，不是随意选的：
 *   · 管理端 OTA：下载 zip → 解压 → robocopy 覆盖安装目录 → 重启
 *     （Stelarith-viewer-qt/src/updater.cpp 的 extractZip/launchReplacer）
 *     ⇒ 热修包 = **zip**；且解压后**必须含 viewer-qt.exe**，否则它自己那道
 *       「解压出来的东西真像管理端包吗」自检会拒绝覆盖（这是好事，保留）。
 *   · 被控端 OTA：下载 → 校验 sha256 → **静默执行 /S**
 *     （Stelarith-control-qt/src/main.cpp 的 startSelfUpdate/otaOnFinished）
 *     ⇒ 热修包 = **exe**（NSIS，见 Stelarith-control-qt/make-hotfix.nsi）；
 *       zip 它根本跑不起来。
 *
 * 热修包含什么（**当前口径，写死在下面 CHANGED 里**）：
 *   · 管理端：viewer-qt.exe + qml/Stelarith/**（界面层 —— QML 是磁盘加载的，正是
 *     2026-10-06 那次 spike 想要的效果：界面能随补丁单独下发）
 *   · 被控端：stelarith-agent-qt.exe
 *   ⚠️ 运行时（Qt6*.dll / platforms / resources / position / translations …）**不进热修包**：
 *      它们在一整个版本线里不变；真变了就必须走全量安装包（DLL 换一半比不换更危险）。
 *      这条限制是**故意的**，写在这里免得下次有人以为漏了。
 *
 * 用法：
 *   node scripts/make-hotfix.mjs                 # 两端都出
 *   node scripts/make-hotfix.mjs --end viewer
 *   node scripts/make-hotfix.mjs --end agent
 *   node scripts/make-hotfix.mjs --dry-run       # 只报会产出什么，不写盘
 *   node scripts/make-hotfix.mjs --check <包>    # 只检查一个已存在的包
 *
 * 退出码：0 成功 / 1 失败。
 * ⚠️ 涉及 fs.rmSync（清暂存目录），沙箱下请带 CODEBUDDY_SAFE_DELETE_ENABLED=0 跑。
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SEVEN_ZIP = 'C:\\Program Files\\7-Zip\\7z.exe';
const MAKENSIS = 'C:\\Program Files (x86)\\NSIS\\makensis.exe';

const VIEWER = path.join(ROOT, 'Stelarith-viewer-qt');
const AGENT = path.join(ROOT, 'Stelarith-control-qt');

const argv = process.argv.slice(2);
const has = (n) => argv.includes(`--${n}`);
function arg(n, def = undefined) {
	const i = argv.indexOf(`--${n}`);
	if (i < 0) return def;
	const v = argv[i + 1];
	return v === undefined || v.startsWith('--') ? true : v;
}

function fail(m) {
	console.error(`\n✗ ${m}\n`);
	process.exit(1);
}
const log = (m) => console.log(m);

function humanSize(n) {
	if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
	return `${(n / 1024 / 1024).toFixed(2)} MB`;
}
function sha256(file) {
	const h = crypto.createHash('sha256');
	h.update(fs.readFileSync(file));
	return h.digest('hex');
}
/** 版本号从各自的**唯一真源**读（与 CMake 校验的是同一行），别让包名和 exe 各说各话。 */
function readVersion(end) {
	const [proj, re, what] = end === 'viewer'
		? [VIEWER, /kViewerVersion\s*=\s*"([^"]+)"/, 'kViewerVersion']
		: [AGENT, /kAppVersion\s*=\s*"([^"]+)"/, 'kAppVersion'];
	const src = fs.readFileSync(path.join(proj, 'src', 'main.cpp'), 'utf8');
	const m = src.match(re);
	if (!m) fail(`从 ${path.relative(ROOT, proj)}/src/main.cpp 里读不到 ${what}`);
	return m[1];
}
function listZip(zip) {
	const out = execFileSync(SEVEN_ZIP, ['l', '-ba', zip], { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
	const RE = /^\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+\S+\s+\d+\s+\d+\s+(.*)$/;
	return out.split(/\r?\n/)
		.map((l) => { const m = l.match(RE); return m ? m[1].trim().replace(/\\/g, '/') : null; })
		.filter((s) => s && s !== '/');
}

/**
 * OpenSSL 3 运行库（2026-10-10 补）：两侧的 WebRTC 都走 libdatachannel，它的 DTLS
 * 链的是 PostgreSQL 17 自带的 OpenSSL ⇒ exe 导入表里含 libssl-3-x64.dll /
 * libcrypto-3-x64.dll。windeployqt 不认识它们，全量安装包靠 .nsi 显式点名；
 * **热修包也必须带上** —— 否则只换 exe、老机器上没有这两个 DLL，热修完照样
 * 「stelarith-agent-qt.exe - 系统错误：找不到 libssl-3-x64.dll」。
 */
const OPENSSL_DLLS = ['libssl-3-x64.dll', 'libcrypto-3-x64.dll'];

/** 管理端热修包**必须**含这些：exe 是 OTA 自检门槛，Main.qml 是界面本体，DLL 是运行库。 */
const VIEWER_MUST = ['viewer-qt.exe', 'qml/Stelarith/Main.qml', ...OPENSSL_DLLS];

/** 把 OpenSSL 运行库从 build/ 拷进暂存目录；缺了直接失败（别发出跑不起来的热修包）。 */
function stageOpenSsl(box, stage) {
	for (const dll of OPENSSL_DLLS) {
		const src = path.join(box, dll);
		if (!fs.existsSync(src)) {
			fail(
				`找不到 ${path.relative(ROOT, src)} —— 全量包/热修包都必须带它；` +
					'先把它从 "C:\\Program Files\\PostgreSQL\\17\\bin" 拷进 build/'
			);
		}
		fs.copyFileSync(src, path.join(stage, dll));
	}
}

// ── 管理端：zip ──────────────────────────────────────────────────────────────
function buildViewer({ dryRun }) {
	const ver = readVersion('viewer');
	const box = path.join(VIEWER, 'build');
	const exe = path.join(box, 'viewer-qt.exe');
	const qmlSrc = path.join(box, 'qml', 'Stelarith');
	if (!fs.existsSync(exe)) fail(`找不到 ${path.relative(ROOT, exe)}（先构建管理端）`);
	if (!fs.existsSync(qmlSrc)) fail(`找不到 ${path.relative(ROOT, qmlSrc)}（QML 磁盘副本，先构建）`);

	// 磁盘副本 vs 源码：两边不一致说明这份 QML 是旧的，热修发出去界面就不是你以为的那个
	const srcQml = path.join(VIEWER, 'qml');
	let stale = 0;
	const walk = (dir, rel = '') => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const r = rel ? `${rel}/${e.name}` : e.name;
			if (e.isDirectory()) walk(path.join(dir, e.name), r);
			else {
				const a = path.join(srcQml, r), b = path.join(qmlSrc, r);
				if (!fs.existsSync(b) || sha256(a) !== sha256(b)) { log(`  ⚠ 副本陈旧：${r}`); stale++; }
			}
		}
	};
	if (fs.existsSync(srcQml)) walk(srcQml);
	if (stale === 0) log('  · qml 副本与源码一致 ✓');

	const stage = path.join(VIEWER, 'dist', `hotfix-${ver}`);
	const out = path.join(VIEWER, 'dist', `stelarith-viewer-hotfix-${ver}.zip`);
	log(`管理端热修 → ${path.relative(ROOT, out)}`);
	log(`  含 viewer-qt.exe + qml/Stelarith/**（源码 ${fs.readdirSync(srcQml).length} 项）+ ${OPENSSL_DLLS.join(' / ')}`);

	if (dryRun) {
		log('  --dry-run：不写盘');
		return { ver, out: null };
	}
	if (!fs.existsSync(SEVEN_ZIP)) fail(`找不到 7-Zip：${SEVEN_ZIP}`);
	fs.rmSync(stage, { recursive: true, force: true });
	fs.mkdirSync(stage, { recursive: true });
	fs.copyFileSync(exe, path.join(stage, 'viewer-qt.exe'));
	fs.cpSync(qmlSrc, path.join(stage, 'qml', 'Stelarith'), { recursive: true });
	stageOpenSsl(box, stage);
	// ⚠️ 从暂存目录**里面**压（cwd=stage，`.\*`），让 zip 根层就是应用目录，
	//    与 make-portable.mjs 同一口径 —— 管理端 OTA 是把包解开后 robocopy 覆盖安装目录的。
	fs.rmSync(out, { force: true });
	execFileSync(SEVEN_ZIP, ['a', '-tzip', '-mx=5', out, '.\\*'], { cwd: stage, stdio: 'inherit' });
	fs.rmSync(stage, { recursive: true, force: true });

	const entries = new Set(listZip(out));
	const missing = VIEWER_MUST.filter((f) => !entries.has(f));
	if (missing.length) fail(`热修包里缺必需件：${missing.join(', ')}（管理端 OTA 会拒绝这个包）`);
	return { ver, out, size: fs.statSync(out).size, sha: sha256(out), entries: entries.size };
}

// ── 被控端：exe（NSIS 极小安装器）─────────────────────────────────────────────
function buildAgent({ dryRun }) {
	const ver = readVersion('agent');
	const box = path.join(AGENT, 'build');
	const exe = path.join(box, 'stelarith-agent-qt.exe');
	const nsi = path.join(AGENT, 'make-hotfix.nsi');
	if (!fs.existsSync(exe)) fail(`找不到 ${path.relative(ROOT, exe)}（先构建被控端）`);
	if (!fs.existsSync(nsi)) fail(`找不到 ${path.relative(ROOT, nsi)}`);
	const out = path.join(AGENT, 'dist', `stelarith-agent-hotfix-${ver}.exe`);
	log(`被控端热修 → ${path.relative(ROOT, out)}`);
	log(`  含 stelarith-agent-qt.exe（${humanSize(fs.statSync(exe).size)}）+ ${OPENSSL_DLLS.join(' / ')}`);

	if (dryRun) {
		log('  --dry-run：不写盘');
		return { ver, out: null };
	}
	// 暂存：make-hotfix.nsi 里的 `File "hotfix-staging\stelarith-agent-qt.exe"` 就指这儿
	const stage = path.join(AGENT, 'hotfix-staging');
	fs.rmSync(stage, { recursive: true, force: true });
	fs.mkdirSync(stage, { recursive: true });
	fs.copyFileSync(exe, path.join(stage, 'stelarith-agent-qt.exe'));
	stageOpenSsl(box, stage);
	fs.mkdirSync(path.join(AGENT, 'dist'), { recursive: true });
	fs.rmSync(out, { force: true });
	execFileSync(MAKENSIS, ['make-hotfix.nsi'], { cwd: AGENT, stdio: 'inherit' });
	fs.rmSync(stage, { recursive: true, force: true });

	if (!fs.existsSync(out)) fail('makensis 没产出预期文件（看上面的编译输出）');
	return { ver, out, size: fs.statSync(out).size, sha: sha256(out) };
}

// ── 检查一个已存在的热修包 ────────────────────────────────────────────────────
function check(target) {
	if (!fs.existsSync(target)) fail(`找不到 ${target}`);
	const size = fs.statSync(target).size;
	log(`包：${path.basename(target)}  ${humanSize(size)}  sha256 ${sha256(target)}`);
	if (target.toLowerCase().endsWith('.zip')) {
		const entries = new Set(listZip(target));
		const missing = VIEWER_MUST.filter((f) => !entries.has(f));
		if (missing.length) fail(`缺必需件：${missing.join(', ')}`);
		log(`条目 ${entries.size}，必需件齐 ✓`);
		if (size > 20 * 1024 * 1024) log('⚠ 体积偏大 —— 热修包应远小于全量包，确认没把 Qt 运行时塞进来');
	} else {
		// exe 形态（被控端）：exe ~0.7MB + OpenSSL 两枚（压缩后约 2MB）
		if (size > 20 * 1024 * 1024) log('⚠ 体积偏大 —— 被控端热修包应约 3MB（exe + OpenSSL 两枚）');
		else log('体积合理 ✓');
	}
	return true;
}

// ── 入口 ────────────────────────────────────────────────────────────────────
const checkArg = arg('check');
let ok = true;
if (typeof checkArg === 'string') {
	ok = check(checkArg);
} else {
	const end = typeof arg('end') === 'string' ? arg('end') : 'both';
	if (!['viewer', 'agent', 'both'].includes(end)) fail('--end 只能是 viewer / agent / both');
	const dryRun = has('dry-run');
	const results = [];
	if (end === 'viewer' || end === 'both') results.push(['管理端', buildViewer({ dryRun })]);
	if (end === 'agent' || end === 'both') results.push(['被控端', buildAgent({ dryRun })]);
	if (!dryRun) {
		const vv = new Set(results.map(([, r]) => r.ver));
		if (vv.size > 1) log(`\n⚠ 两端版本号不一致：${[...vv].join(' / ')} —— 先跑 scripts/bump-version.mjs show 看看`);
		log('\n── 结果 ──');
		for (const [name, r] of results) {
			log(`${name} v${r.ver}  ${path.relative(ROOT, r.out)}`);
			log(`        ${humanSize(r.size)}  sha256 ${r.sha}`);
		}
	}
}
process.exit(ok ? 0 : 1);
