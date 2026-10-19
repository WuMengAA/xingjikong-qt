#!/usr/bin/env node
/**
 * 版本号命名标准自检 —— 命名格式 + 比较语义 + 工程约束（真源 / include / 发布脚本）。
 *
 * 跑：node scripts/test-version-naming.mjs     （工作区根目录下）
 * 输出全绿才谈得上发版；任何一条挂 ⇒ 要么命名乱了，要么真源漂移了。
 *
 * 比较语义的**唯一实现**在 ./version-compare.mjs；被控端 C++ 侧（src/main.cpp 的
 * compareVersion）是同语义的另一份，这里只验 JS 侧，C++ 侧靠编译 + OTA 行为兜。
 * 标准全文见 docs/版本号命名规范-2026-10-07.md。
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { cmpVersion, isCandidate, isStable, parseVersion } from './version-compare.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ctrl = path.join(ROOT, 'Stelarith-control-qt');
const view = path.join(ROOT, 'Stelarith-viewer-qt');
const cloud = path.join(ROOT, 'Stelarith-cloud');

let pass = 0;
const fails = [];
function ok(cond, name) { if (cond) pass++; else fails.push(name); }
function read(p) { return fs.readFileSync(p, 'utf8'); }

// ── A. 命名格式 ──────────────────────────────────────────────────────────
ok(isStable('0.6.23'), 'A1 正式版 0.6.23 合规');
ok(isCandidate('0.6.23-rc.1'), 'A2 候选版 0.6.23-rc.1 合规');
ok(!isCandidate('0.6.23-rc.0'), 'A3 rc 号从 1 起（0.6.23-rc.0 不合规）');
ok(!isCandidate('0.6.23-rc'), 'A4 缺 rc 号不合规（0.6.23-rc）');
ok(!isStable('0.6.23.1'), 'A5 第四段不是后缀（0.6.23.1 不合规）');
ok(!isCandidate('0.6.23-Beta.1'), 'A6 渠道标记不进版本号（Beta 后缀不合规）');
ok(isStable('0.6.22') && isCandidate('0.6.22-rc.9'), 'A7 多位 rc 号可用');
// A8~A13：alpha 是 dsh 同款的另一套路阶段后缀（0.1.2-alpha.5 / 0.2.1-alpha.1 都真实存在）
ok(isCandidate('0.6.23-alpha.1'), 'A8 内测版 0.6.23-alpha.1 合规');
ok(!isCandidate('0.6.23-alpha.0'), 'A9 alpha 号也从 1 起（0.6.23-alpha.0 不合规）');
ok(!isCandidate('0.6.23-alpha'), 'A10 缺 alpha 号不合规（0.6.23-alpha）');
ok(!isCandidate('0.6.23-alpha.1-rc.1'), 'A11 不许两个阶段叠在一起（alpha.1-rc.1 不合规）');
ok(parseVersion('0.6.23-alpha.5').core === '0.6.23'
	&& parseVersion('0.6.23-alpha.5').stage === 'alpha'
	&& parseVersion('0.6.23-alpha.5').n === 5, 'A12 解析出 核心 / 阶段 / 号');
ok(parseVersion('0.6.23').stage === '' && parseVersion('0.6.23').n === -1,
	'A13 正式版没有阶段');

// ── B. 比较语义（semver 子集）────────────────────────────────────────────
const cases = [
	['0.6.23-rc.1', '0.6.23', -1, 'B1 候选 < 同核心正式版'],
	['0.6.23', '0.6.23-rc.1', 1, 'B2 正式版 > 候选'],
	['0.6.23-rc.2', '0.6.23-rc.1', 1, 'B3 同核心 rc 号递增'],
	['0.6.23-rc.10', '0.6.23-rc.9', 1, 'B4 rc 号也逐段比数字'],
	['0.6.23-rc.1', '0.6.22', 1, 'B5 核心大 ⇒ 候选也大'],
	['0.6.24-rc.1', '0.6.23', 1, 'B6 跨核心候选 > 旧正式'],
	['0.6.23', '0.6.24-rc.1', -1, 'B7 跨核心正式 < 新候选'],
	['0.6.23', '0.6.23', 0, 'B8 相等'],
	['0.10.0', '0.9.9', 1, 'B9 逐段比数字（不是字符串比）'],
	['0.4.0-v1', '0.4.0', 0, 'B10 老后缀当空气（0.4.0-v1 == 0.4.0）'],
	['1.0.0-rc.1', '0.99.99', 1, 'B11 主版本优先'],
	// B12~B18：alpha / rc 的跨阶段序（跟 dsh 真实例 0.1.2-alpha.5 → 0.1.2-rc.1 一致）
	['0.6.23-alpha.1', '0.6.23-rc.1', -1, 'B12 同核心：alpha 早于 rc'],
	['0.6.23-alpha.5', '0.6.23-rc.1', -1, 'B13 跨阶段只比阶段，不比号（alpha.5 < rc.1）'],
	['0.6.23-rc.1', '0.6.23-alpha.5', 1, 'B14 反向'],
	['0.6.23-alpha.1', '0.6.23-alpha.2', -1, 'B15 同核心同阶段比号'],
	['0.6.23-alpha.2', '0.6.23-alpha.1', 1, 'B16 同核心同阶段比号（反向）'],
	['0.6.23-alpha.9', '0.6.23', -1, 'B17 内测 < 同核心正式'],
	['0.6.24-alpha.1', '0.6.23-rc.9', 1, 'B18 核心大就是大（候选/内测都不降权）'],
];
for (const [a, b, want, name] of cases) {
	ok(cmpVersion(a, b) === want, `${name}  ← ${a} vs ${b}`);
}

// ── C. 真源一致（CMakeLists 与源码字面量）────────────────────────────────
function cmakeVer(dir, varname) {
	const m = read(path.join(dir, 'CMakeLists.txt'))
		.match(new RegExp(`set\\(${varname}\\s+"([^"]+)"\\)`));
	return m ? m[1] : null;
}
function cppVer(dir, name) {
	const m = read(path.join(dir, 'src', 'main.cpp'))
		.match(new RegExp(`${name}\\s*=\\s*"([^"]+)"`));
	return m ? m[1] : null;
}
const pairs = [
	[ctrl, 'AGENT_VERSION', 'kAppVersion', 'C1 被控端'],
	[view, 'VIEWER_VERSION', 'kViewerVersion', 'C2 管理端'],
];
for (const [dir, cv, sv, tag] of pairs) {
	const v1 = cmakeVer(dir, cv), v2 = cppVer(dir, sv);
	ok(v1 !== null && v1 === v2, `${tag}：CMake 真源(${v1}) == 源码回落值(${v2})`);
	ok(v1 !== null && (isStable(v1) || isCandidate(v1)), `${tag}：真源值合命名规范（${v1}）`);
}

// ── D. 二进制版本字段只吃四段纯数字 ─────────────────────────────────────
for (const [dir, prefix, rcIn] of [[ctrl, 'AGENT', 'AGENT_VERSION'], [view, 'VIEWER', 'VIEWER_VERSION']]) {
	const full = cmakeVer(dir, rcIn);
	ok(full === null || (isStable(full) || isCandidate(full)),
		`D1 ${path.basename(dir)} 版本串形态合法（${full}）`);
	// FILEVERSION 必须走核心段推导 —— 以前直接拆整个串，"0.6.23-rc.1" 会拆出 "22-rc"
	const src = read(path.join(dir, 'CMakeLists.txt'));
	ok(src.includes(`${prefix}_CORE`) && src.includes('RC_VERSION_QUAD'),
		`D2 ${path.basename(dir)} 的 FILEVERSION 走核心段（防 "22-rc" 混进四段）`);
	// 阶段名/号要能被 CMake 拆出来，才能打印"阶段 3"这种诊断、也便于以后按阶段做判断
	ok(src.includes(`${prefix}_STAGE`) && src.includes(`${prefix}_STAGE_N`),
		`D1b ${path.basename(dir)} 的 CMake 拆出了阶段名与阶段号`);
	// .rc 模板：字符串字段可带后缀，二进制字段必须吃 @RC_VERSION_QUAD@
	const tpl = read(path.join(dir, 'src', 'app.rc.in'));
	ok(/FILEVERSION\s+@RC_VERSION_QUAD@/.test(tpl)
		&& new RegExp(`VALUE "FileVersion",\\s+"@${prefix}_VERSION@"`).test(tpl),
		`D3 ${path.basename(dir)} 的 .rc：字符串字段带后缀、二进制字段不带`);
}

// ── E. 安装器版本常量由 CMake 出，不再手抄（2026-10-10 从 NSIS 迁到 Inno Setup）──
const iss = read(path.join(ctrl, 'installer.iss'));
ok(!/#define\s+MyAppVersion\s+"\s*\d/.test(iss),
	'E1 installer.iss 里没有手写的 #define MyAppVersion "<数字版本>"（真值由 STE_INSTALLER_VERSION env 传入）');
ok(iss.includes('STE_INSTALLER_VERSION') && iss.includes('GetEnv'),
	'E2 installer.iss 通过 GetEnv(STE_INSTALLER_VERSION) 接收版本（env 解析自 build/version.nsh）');
ok(read(path.join(ctrl, 'src', 'version.nsh.in')).includes('@AGENT_VERSION@'),
	'E3 version.nsh.in 的占位符对着 AGENT_VERSION');
ok(read(path.join(ctrl, 'build-agent-installer.sh')).includes('build/version.nsh'),
	'E4 打包脚本有 version.nsh 前置检查（缺文件直接 FATAL）');
ok(read(path.join(ctrl, 'CMakeLists.txt')).includes('version.nsh.in'),
	'E5 CMakeLists 会把 version.nsh.in 生成进 build/');

// ── F. 发布脚本认候选号、且没有第二份比较实现 ───────────────────────────
const pub = read(path.join(cloud, 'scripts', 'publish-release.mjs'));
// 阶段号必须收紧成 [1-9]\d*：写成 \d+ 会放进 0.6.23-alpha.0 —— 被控端解析阶段号要求 n>=1，
// 会把 alpha.0 当正式版 ⇒ 云端发了 alpha.0、教室机不升级，且之后再发 0.6.23 也推不下去。
// 2026-10-07 预演抓到（scripts/version-compare.mjs 的 isCandidate 与两端 CMake 都是收紧版，
// 只有发布脚本漏了）。这两条断言就是防止它再漏一次。
ok(pub.includes(String.raw`-(?:alpha|rc)\.[1-9]\d*`),
	'F1 发布脚本的 --version 正则放开 -alpha.N / -rc.N 且阶段号从 1 起');
ok(!/\\d\+\)?\$/.test(pub) && !pub.includes(String.raw`-(?:alpha|rc)\.\d+`),
	'F1b 发布脚本的阶段号不是 \\d+（否则 alpha.0 会被发进线上清单）');
ok(pub.includes("cmpVersion } from '../../scripts/version-compare.mjs'"),
	'F2 发布脚本用共享实现（不再留本地第二份 cmpVersion）');

// ── 输出 ────────────────────────────────────────────────────────────────
console.log(`\n版本号命名自检：${pass} 通过 / ${fails.length} 失败`);
if (fails.length) {
	for (const f of fails) console.log(`  ✗ ${f}`);
	process.exit(1);
}
console.log('✓ 全部通过（命名 / 比较语义 / 真源 / 安装器 / 发布脚本）');
