#!/usr/bin/env node
/**
 * 星集控 · 版本号自动更迭（2026-10-09 立）
 *
 * 为什么要有它：
 *   两端的版本号各有**两个副本**，而且两边都不能少：
 *     · 管理端：Stelarith-viewer-qt/CMakeLists.txt 的 set(VIEWER_VERSION ...)
 *              Stelarith-viewer-qt/src/main.cpp   的 kViewerVersion
 *     · 被控端：Stelarith-control-qt/CMakeLists.txt 的 set(AGENT_VERSION ...)
 *              Stelarith-control-qt/src/main.cpp   的 kAppVersion
 *   两处 CMakeLists 都在配置期把 main.cpp 那份**读出来比对**，不一致直接 FATAL_ERROR
 *   （注释里写的"那份会自动跟随"是假的：只有比对，没有任何自动回写）。
 *   于是每发一版都要人手改 4 行，改漏一行就编不过、或者更糟 —— 编过但两处版本不同。
 *   本脚本把这 4 处当成**同一份事实的 4 个副本**统一读写，并在写完后回读校验。
 *
 * 命名标准（docs/版本号命名规范-2026-10-07.md，同 DeepSeek Harness）：
 *   正式   X.Y.Z            0.6.24
 *   候选   X.Y.Z-rc.N       0.6.24-rc.1   （N 从 1 起）
 *   内测   X.Y.Z-alpha.N    0.6.24-alpha.1
 *   阶段序：alpha < rc < 正式；号同核心递增、换核心归零、转正即剥离后缀。
 *
 * 用法：
 *   node scripts/bump-version.mjs show
 *       打印 4 处当前版本（并如实报出漂移）。
 *
 *   node scripts/bump-version.mjs bump [--stage rc|alpha] [--dry-run]
 *       自动进一位：
 *         · 候选版（-rc.N / -alpha.N）→ **同核心同阶段，N+1**（0.6.24-rc.1 → 0.6.24-rc.2）
 *         · 正式版（X.Y.Z）           → **补丁位 +1，仍是正式版**（0.6.24 → 0.6.25）
 *       --stage rc|alpha：切到该阶段（若当前已是该阶段则 N+1，否则 N=1）。
 *       例：`bump --stage rc` 把 0.6.24（正式）变成 0.6.24-rc.1；
 *           `bump --stage alpha` 把 0.6.24-rc.3 变成 0.6.24-alpha.1。
 *
 *   node scripts/bump-version.mjs set 0.6.25-rc.1 [--dry-run]
 *       直接设成指定版本（仍会过命名守门正则）。
 *
 *   node scripts/bump-version.mjs release [--dry-run]
 *       转正：剥掉 -rc.N / -alpha.N 后缀（0.6.24-rc.3 → 0.6.24）。
 *
 * 退出码：0 成功 / 1 失败。失败时**不写任何文件**（先全部算好、校验通过，最后一次性写）。
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseVersion, isCandidate, isStable } from './version-compare.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

/** 命名守门：与两端 CMakeLists 里的正则、publish-release.mjs 的 --version 校验**同形**。 */
const NAMING_RE = /^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.[1-9]\d*)?$/;

/**
 * 版本号的 4 个副本。每条一个「捕获组对」——旧值替换掉中间那段，前后文原样保留。
 * ⚠️ 每条都要求**恰好匹配 1 次**：多一处或少一处的都算地基变了，宁可报错也别瞎替换。
 */
const SOURCES = [
	{
		label: 'viewer/CMakeLists.txt  VIEWER_VERSION',
		file: 'Stelarith-viewer-qt/CMakeLists.txt',
		re: /(set\(VIEWER_VERSION[ \t]+")([^"]*)("\))/,
	},
	{
		label: 'viewer/src/main.cpp     kViewerVersion',
		file: 'Stelarith-viewer-qt/src/main.cpp',
		re: /(kViewerVersion[ \t]*=[ \t]*")([^"]*)(")/,
	},
	{
		label: 'control/CMakeLists.txt AGENT_VERSION',
		file: 'Stelarith-control-qt/CMakeLists.txt',
		re: /(set\(AGENT_VERSION[ \t]+")([^"]*)("\))/,
	},
	{
		label: 'control/src/main.cpp    kAppVersion',
		file: 'Stelarith-control-qt/src/main.cpp',
		re: /(kAppVersion[ \t]*=[ \t]*")([^"]*)(")/,
	},
];

const argv = process.argv.slice(2);
const cmd = argv[0] || 'show';
const has = (n) => argv.includes(`--${n}`);
function arg(n, def = undefined) {
	const i = argv.indexOf(`--${n}`);
	if (i < 0) return def;
	const v = argv[i + 1];
	if (v === undefined || v.startsWith('--')) return true;
	return v;
}
const DRY = has('dry-run');

function fail(m) {
	console.error(`\n✗ ${m}\n`);
	process.exit(1);
}
function log(m) { console.log(m); }

/** 读一处副本的当前值；命中数不为 1 直接失败。 */
function readOne(src) {
	const abs = path.join(ROOT, src.file);
	if (!fs.existsSync(abs)) fail(`找不到 ${src.file}`);
	const text = fs.readFileSync(abs, 'utf8');
	const all = new RegExp(src.re.source, 'g');
	const hits = text.match(all) || [];
	if (hits.length !== 1) {
		fail(`${src.file} 里「${src.label}」匹配到 ${hits.length} 处（应为 1 处）—— ` +
			`文件结构变了，脚本的锚点需要重新确认，先别改。`);
	}
	const m = src.re.exec(text);
	return { abs, text, value: m[2] };
}

function readAll() {
	return SOURCES.map((s) => ({ src: s, ...readOne(s) }));
}

/** 4 处必须一致；不一致就是已经漂了，先让人看见。 */
function currentVersion(rows) {
	const vals = [...new Set(rows.map((r) => r.value))];
	if (vals.length !== 1) {
		log('✗ 4 处版本号已经漂移：');
		for (const r of rows) log(`    ${r.src.label} = ${r.value}`);
		fail('先手工对齐（或 `set <版本>` 强制统一）再做别的。');
	}
	return vals[0];
}

function cmdShow() {
	const rows = readAll();
	for (const r of rows) log(`  ${r.src.label} = ${r.value}`);
	const vals = [...new Set(rows.map((r) => r.value))];
	if (vals.length === 1) {
		log(`\n当前版本：${vals[0]}（4 处一致 ✓）`);
		return true;
	}
	log(`\n✗ 漂移：${vals.join(' / ')}`);
	return false;
}

/** 算下一个版本号。 */
function nextVersion(cur, { stage, release }) {
	const { core, stage: st, n } = parseVersion(cur);
	const [X, Y, Z] = core.split('.').map((x) => parseInt(x, 10));
	if (release) {
		if (!isCandidate(cur)) fail(`${cur} 已经不是候选版，无需转正`);
		return core;
	}
	if (stage) {
		if (stage === st) return `${core}-${stage}.${n + 1}`;
		return `${core}-${stage}.1`;   // 换阶段：号归 1
	}
	if (isCandidate(cur)) return `${core}-${st}.${n + 1}`;
	if (isStable(cur)) return `${X}.${Y}.${Z + 1}`;
	fail(`不认识当前版本形态：${cur}`);
}

function apply(newVer) {
	if (!NAMING_RE.test(newVer)) {
		fail(`算出来的版本 ${newVer} 不合命名规范（${NAMING_RE}）`);
	}
	const rows = readAll();
	const cur = currentVersion(rows);
	if (newVer === cur) fail(`新版本与当前版本相同（${cur}），没有可改的`);
	log(`版本号：${cur}  →  ${newVer}`);

	// 先全部算出新内容并逐条自检，**一条不通过就整体不写**（避免写了一半的半成品）。
	const planned = rows.map((r) => {
		const text = r.text.replace(r.src.re, `$1${newVer}$3`);
		if (text === r.text) fail(`${r.src.file} 替换后内容未变，锚点失效`);
		return { ...r, text };
	});
	for (const p of planned) {
		const m = p.src.re.exec(p.text);
		if (!m || m[2] !== newVer) fail(`${p.src.file} 替换结果回读不符（${m ? m[2] : '无匹配'}）`);
	}

	if (DRY) {
		log('\n--dry-run：不写文件。');
		for (const p of planned) log(`  would update ${p.src.file}`);
		return true;
	}
	for (const p of planned) {
		fs.writeFileSync(p.abs, Buffer.from(p.text, 'utf8'));   // 显式 Buffer ⇒ 不带 BOM
	}
	// 落盘后回读（防"写出去的东西不是我以为的"）
	const after = readAll().map((r) => r.value);
	if (after.some((v) => v !== newVer)) {
		fail(`写入后回读不一致：${after.join(' / ')}`);
	}
	log(`\n✓ 4 处已统一为 ${newVer}（回读校验通过）`);
	log('  下一步：重编两端（版本要编进 exe 资源）→ 出包 → publish-release.mjs 更新 ota.json。');
	return true;
}

let ok = false;
if (cmd === 'show') {
	ok = cmdShow();
} else if (cmd === 'bump') {
	const rows = readAll();
	const cur = currentVersion(rows);
	const stage = typeof arg('stage') === 'string' ? arg('stage') : null;
	if (stage && !['rc', 'alpha'].includes(stage)) fail('--stage 只能是 rc 或 alpha');
	ok = apply(nextVersion(cur, { stage }));
} else if (cmd === 'release') {
	const rows = readAll();
	const cur = currentVersion(rows);
	ok = apply(nextVersion(cur, { release: true }));
} else if (cmd === 'set') {
	const v = arg('to') || argv[1];
	if (typeof v !== 'string' || !NAMING_RE.test(v)) {
		fail('用法：node scripts/bump-version.mjs set 0.6.25-rc.1（版本须合命名规范）');
	}
	ok = apply(v);
} else {
	log(`未知命令：${cmd}`);
	log('可用：show | bump [--stage rc|alpha] | release | set <版本>   （都可加 --dry-run）');
	process.exit(1);
}
process.exit(ok ? 0 : 1);
