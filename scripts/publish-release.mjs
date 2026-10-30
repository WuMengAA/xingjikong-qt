#!/usr/bin/env node
/**
 * 发版脚本 —— 一条命令把安装包发布到教室机（OTA）与官网（下载中心）。
 *
 * 为什么要它（2026-10-06 立）：
 *   1. 手工改 ota.json 会出事，而且今晚真的差点出事 —— 用编辑器/Set-Content 存出来的
 *      文件带了 UTF-8 BOM，JSON.parse 直接抛错，接口表现成假"未发布"（latest:null）。
 *      这类错误在界面上看不出来，"被控端说没有新版"和"清单是坏的"长得一模一样。
 *   2. 版本号 / 大小 / sha256 是**同一份事实的三处副本**（ota.json、实际文件、下载页显示）。
 *      人肉同步必然漂移：文件重打过、清单没改，教室机就会下到一个 sha256 对不上的包，
 *      然后被自己的校验拦下来 —— 现场表现是"升级点了没反应"。
 *   3. 本项目的发布对象是**全校教室机**：一旦把低版本当成最新推出去，
 *      所有机器会集体"升级"到旧版本。所以这里对版本倒退默认直接拒绝。
 *
 * 用法：
 *   node scripts/publish-release.mjs check
 *       体检：把 ota.json 里每条记录与实际文件核对（存在性 / 大小 / sha256）。
 *       发布前跑一次，发布后跑一次。**不改任何文件。**
 *
 *   node scripts/publish-release.mjs show
 *       打印当前清单与对应文件状态。
 *
 *   node scripts/publish-release.mjs publish --product agent --version 0.6.0 \
 *          --file ../Stelarith-control-qt/dist/stelarith-agent-setup-0.6.0.exe \
 *          [--notes "本次改了什么"] [--mandatory] [--base-url https://host] [--dry-run] [--no-verify]
 *       算 sha256/大小 → **复制**（不是移动）到 assets/pkg/ → 只改 ota.json 里这一个产品
 *       → 回读校验 → 发布后自验（GET 公网站点确认这一版取得到；--no-verify 跳过）→ 打印结果。
 *
 *   灰度渠道（2026-10-10 加）：加 --channel gray 即把这一版本发到 ota-gray.json
 *       （而不是 ota.json）。stable 设备读不到它；只有 register 带 channel=gray 的设备才拿得到。
 *       用法：… publish --product agent --version 0.6.0-rc.4 --channel gray \
 *             --file …/stelarith-agent-setup-0.6.24-rc.4.exe --base-url https://control.245959623.xyz
 *       验证够稳后，把同一包再 --channel stable 发一次（全量推送）。
 *
 * 退出码：0 成功 / 1 失败（护栏不通过时不会留下半成品：ota.json 只在全部校验通过后才写；
 *   唯一的例外是发布后自验失败——那时清单已写入，脚本以 1 退出要求人工核对公网端点）
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
// 版本解析/比较的唯一实现（2026-10-07 抽出来）：工作区根 scripts/version-compare.mjs。
// 被控端 C++ 侧是同语义的另一份（Stelarith-control-qt/src/main.cpp 的 compareVersion），
// 两边靠 scripts/test-version-naming.mjs 的断言对齐。
import { cmpVersion } from '../../scripts/version-compare.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
// ⚠️ 必须 path.resolve 而不是 path.join：join 不把第二参当绝对路径，
//    CLOUD_OTA_MANIFEST_FILE 给绝对路径（隔离测试就这么用）时会拼出
//    ROOT/C:/… 这种四不像路径 ⇒ 写到莫名其妙的地方去。resolve 两种都认。
let MANIFEST = path.resolve(ROOT, process.env.CLOUD_OTA_MANIFEST_FILE || 'ota.json');
const EXAMPLE = path.join(ROOT, 'ota.json.example');
const PKG_DIR = path.join(ROOT, process.env.CLOUD_ASSETS_DIR || 'assets', 'pkg');
const PRODUCTS = ['agent', 'viewer'];

/**
 * 每个产品包里**必须**有的运行时件 —— 发布前解包自检，缺件直接拒发。
 *
 * 为什么要它（2026-10-10 事故）：
 *   OTA 曾把「缺 libssl-3-x64.dll / libcrypto-3-x64.dll」的包发布给全校教室机 ——
 *   那两个 DLL 是 exe 的**静态导入**（libdatachannel 的 DTLS 链了 PG17 的 OpenSSL），
 *   windeployqt 不认识、`.nsi` 的 `Qt6*.dll` 通配也吃不到 ⇒ 装完双击即
 *   「stelarith-agent-qt.exe - 系统错误：找不到 libssl-3-x64.dll」。
 *   ⚠️ 本脚本原来的 check 只核对「清单 vs 磁盘文件」（size/sha256）——
 *   它**从不看包里面有什么**，于是"文件与清单一致"的坏包被评为「一致 ✓」，
 *   一路发到教室机。这里补上这道内容自检：宁可发不出去，也不发一个装完起不来的包。
 *   新增必需件时往这里加 —— 与各出包脚本的 REQUIRED 清单保持同源。
 */
const REQUIRED_ENTRIES = {
	agent: ['stelarith-agent-qt.exe', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll'],
	viewer: ['viewer-qt.exe', 'qml/Stelarith/Main.qml', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll']
};
const SEVEN_ZIP = process.env.SEVEN_ZIP || 'C:\\Program Files\\7-Zip\\7z.exe';

const argv = process.argv.slice(2);
const cmd = argv[0] || 'check';

function arg(name, def = undefined) {
	const i = argv.indexOf(`--${name}`);
	if (i < 0) return def;
	const v = argv[i + 1];
	if (v === undefined || v.startsWith('--')) return true; // 布尔开关
	return v;
}
const has = (name) => argv.includes(`--${name}`);

// 更新通道（2026-10-10 加）：stable 走 ota.json；其它通道（如 gray）走同目录的 ota-<channel>.json。
// 灰度发布 = 把候选版本发到独立清单，stable 设备读不到、灰度设备（register 带 channel=gray）才读得到。
// 这样"推出灰度更新渠道"不惊动全网，验证够稳再手动把同一包 publish 到 stable（全量）。
const CHANNEL = String(arg('channel', 'stable') || 'stable').trim();
if (CHANNEL && CHANNEL !== 'stable') {
	MANIFEST = path.resolve(ROOT, `ota-${CHANNEL}.json`);
}

const log = (m) => console.log(m);
function fail(m) {
	console.error(`\n✗ ${m}\n`);
	process.exit(1);
}

// ── 清单读写 ────────────────────────────────────────────────────────────────
/** 读清单。**不带 BOM** 是硬要求：带 BOM 的 JSON.parse 会抛错 → 接口假"未发布"。 */
function readManifest({ allowMissing = false } = {}) {
	if (!fs.existsSync(MANIFEST)) {
		if (allowMissing) return { products: {} };
		fail(`找不到清单 ${path.relative(ROOT, MANIFEST)}（首次发布请先跑 publish）`);
	}
	const buf = fs.readFileSync(MANIFEST);
	if (buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf) {
		fail(
			`${path.basename(MANIFEST)} 带 UTF-8 BOM（EF BB BF）—— JSON.parse 会失败，` +
				`云端会把它当成"没发布过版本"。请另存为「UTF-8 无 BOM」后重试。`
		);
	}
	try {
		return JSON.parse(buf.toString('utf8'));
	} catch (e) {
		fail(`${path.basename(MANIFEST)} 不是合法 JSON：${e.message}`);
	}
}

/** 写清单：UTF-8 无 BOM，写完立刻回读解析（防手滑写出坏 JSON 却没发现）。 */
function writeManifest(obj) {
	const text = JSON.stringify(obj, null, 2) + '\n';
	fs.writeFileSync(MANIFEST, Buffer.from(text, 'utf8')); // 显式 Buffer ⇒ 绝不带 BOM
	const back = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
	if (!back || typeof back !== 'object') fail('写回的清单解析异常，已中止');
	return text.length;
}

// ── 工具 ────────────────────────────────────────────────────────────────────
function sha256File(p) {
	return new Promise((resolve, reject) => {
		const h = crypto.createHash('sha256');
		const s = fs.createReadStream(p);
		s.on('error', reject);
		s.on('data', (d) => h.update(d));
		s.on('end', () => resolve(h.digest('hex')));
	});
}

function humanSize(n) {
	if (!n || n <= 0) return '-';
	if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
	if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
	return `${(n / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

// ── 包内容自检（2026-10-10 事故后补）────────────────────────────────────────
/**
 * 用 7z 列包内条目名（正斜杠归一化）。NSIS exe 与 zip 都能列。
 *
 * ⚠️ 必须用 `-slt`（逐条目 `Path = …`），**不能**用 `-ba` + 固定列正则：
 *    NSIS 的输出列是 `date time attr size name`，zip 是 `… size compressed name` ——
 *    列数不同，一种正则覆盖不了两种（10-10 实测：用 zip 的正当解析 NSIS 会丢光所有行 ⇒ 全判"缺件"误报）。
 */
function listArchiveEntries(file) {
	const out = execFileSync(SEVEN_ZIP, ['l', '-slt', file], {
		encoding: 'utf8',
		maxBuffer: 256 * 1024 * 1024
	});
	const paths = [];
	for (const l of out.split(/\r?\n/)) {
		const m = l.match(/^Path = (.*)$/);
		if (m) paths.push(m[1].trim().replace(/\\/g, '/'));
	}
	return paths.slice(1); // 第一行 Path 是归档自身，剔除
}

/**
 * 校验一个包是否含该产品的必需运行时件。
 * 返回 { ok, missing, error }：error 非空表示"没能检查"（7z 缺失 / 格式读不了）。
 */
function verifyPackageContent(product, file) {
	const need = REQUIRED_ENTRIES[product];
	if (!need) return { ok: true, missing: [] };
	if (!fs.existsSync(SEVEN_ZIP)) {
		return { ok: false, missing: [], error: `找不到 7-Zip（${SEVEN_ZIP}）—— 无法解包校验` };
	}
	// Inno Setup 自解压安装器**不是** 7z 可识别的归档格式（NSIS 的 exe 是，Inno 的不是），
	// 7z 对 .exe 安装器报 "Cannot open the file as archive"。内容正确性已由 ISCC 编译期
	// 保证（[Files] 点名取运行时，编译失败即缺件），故跳过条目列表检查，仅保留
	// size/sha256 与"文件存在"的发布前校验。
	if (path.extname(file).toLowerCase() === '.exe') {
		return { ok: true, missing: [], warning:
			'Inno Setup 安装器：7z 无法列内容，跳过条目检查（依赖 ISCC 编译期验证）' };
	}
	let entries;
	try {
		entries = new Set(listArchiveEntries(file));
	} catch (e) {
		return { ok: false, missing: [], error: `无法读取包内容：${e.message}` };
	}
	const missing = need.filter((x) => !entries.has(x));
	return { ok: missing.length === 0, missing };
}

// 版本解析与比较统一走 ../../scripts/version-compare.mjs（cmpVersion 从那里 import）。
// 本地不再留第二份实现 —— 两份实现迟早分叉，而分叉的表现是"云端说有新版、被控端说没有"。

function pkgPathFromUrl(url) {
	if (!url) return null;
	try {
		const u = new URL(url);
		const base = path.basename(u.pathname);
		return base ? path.join(PKG_DIR, decodeURIComponent(base)) : null;
	} catch {
		return null;
	}
}

/** 从已有条目推出「主机 + 目录前缀」，这样连发多个版本不必每次手写域名。 */
function inferBase(entry) {
	if (!entry || !entry.url) return null;
	try {
		const u = new URL(entry.url);
		return { origin: u.origin, dir: path.posix.dirname(u.pathname) };
	} catch {
		return null;
	}
}

// ── 体检 ────────────────────────────────────────────────────────────────────
async function cmdCheck() {
	const m = readManifest({ allowMissing: true });
	const entries = m.products && typeof m.products === 'object' ? Object.entries(m.products) : [];
	if (!entries.length) {
		log('清单里还没有任何产品 —— 也就是"还没发布过版本"。');
		log('被控端会如实提示「云端还没发布」，官网会显示「暂时取不到版本信息」。');
		return true;
	}

	log(`清单：${path.relative(ROOT, MANIFEST)}（${entries.length} 个产品）\n`);
	let bad = 0;
	for (const [name, e] of entries) {
		const tag = `[${name}]`;
		if (!PRODUCTS.includes(name)) log(`${tag} 注意：不在已知产品列表 ${PRODUCTS.join('/')} 里，仍照常体检`);
		if (!e.version) {
			log(`${tag} ✗ 缺 version`);
			bad++;
			continue;
		}
		if (!/^[0-9a-f]{64}$/.test(String(e.sha256 || ''))) {
			log(`${tag} ✗ sha256 不是 64 位小写十六进制 —— 被控端会拒绝这次更新（不做无校验更新）`);
			bad++;
			continue;
		}
		const p = pkgPathFromUrl(e.url);
		if (!p) {
			log(`${tag} ✗ url 无法解析：${e.url}`);
			bad++;
			continue;
		}
		if (!fs.existsSync(p)) {
			log(`${tag} ✗ 清单指向的文件不在磁盘上：${path.relative(ROOT, p)}`);
			log(`      （云端无法对外提供这个包；教室机会下载失败）`);
			bad++;
			continue;
		}
		const st = fs.statSync(p);
		const got = await sha256File(p);
		const sizeOk = Number(e.size) === st.size;
		const shaOk = String(e.sha256) === got;
		if (sizeOk && shaOk) {
			const v = verifyPackageContent(name, p);
			if (v.error) {
				log(`${tag} ⚠ v${e.version}  ${humanSize(st.size)}  sha256 一致，但**未能校验包内容**：${v.error}`);
			} else if (!v.ok) {
				log(`${tag} ✗ v${e.version}  ${humanSize(st.size)}  sha256 一致，但包内**缺必需件**：${v.missing.join(', ')}`);
				log(`      后果：教室机装上会直接启动失败（缺运行时 DLL）。清单与文件都对、包本身却是坏的。`);
				log(`      修法：重新出包（见各出包脚本的 REQUIRED 清单）后重发。`);
				bad++;
			} else {
				log(`${tag} ✓ v${e.version}  ${humanSize(st.size)}  sha256 一致，包内容齐 ✓`);
			}
			continue;
		}
		log(`${tag} ✗ v${e.version} 与磁盘文件不一致 —— 文件重打过但清单没跟着改：`);
		if (!sizeOk) log(`      大小：清单 ${e.size} / 实际 ${st.size}`);
		if (!shaOk) log(`      sha256：清单 ${e.sha256} / 实际 ${got}`);
		log(`      后果：教室机会下到一个校验不过的包 → 自动删除并报升级失败。`);
		log(`      修法：node scripts/publish-release.mjs publish --product ${name} --version ${e.version} --file "${p}"`);
		bad++;
	}

	log('');
	if (bad) {
		log(`体检结果：${bad} 处不一致 ✗`);
		return false;
	}
	log('体检结果：全部一致 ✓');
	return true;
}

// ── 打印 ────────────────────────────────────────────────────────────────────
async function cmdShow() {
	const m = readManifest({ allowMissing: true });
	const entries = m.products && typeof m.products === 'object' ? Object.entries(m.products) : [];
	if (!entries.length) {
		log('（清单为空：还没发布过版本）');
		return true;
	}
	for (const [name, e] of entries) {
		const p = pkgPathFromUrl(e.url);
		const exists = p && fs.existsSync(p);
		log(`[${name}] v${e.version}${e.mandatory ? '（强制升级）' : ''}`);
		log(`  url    ${e.url}`);
		log(`  size   ${e.size} (${humanSize(e.size)})`);
		log(`  sha256 ${e.sha256}`);
		log(`  本地   ${exists ? path.relative(ROOT, p) : '（不在磁盘上）'}`);
		if (e.notes) log(`  notes  ${e.notes}`);
		log('');
	}
	return true;
}

// ── 发布 ────────────────────────────────────────────────────────────────────
/**
 * 把包落进 assets/pkg/。默认**复制**（不是移动，源文件留在原处 —— 用户的规定）。
 * `--hardlink`：同盘 NTFS 下先试硬链接（零额外磁盘占用）；跨盘或不支持一律 catch 回落复制。
 * ⚠️ 硬链接的两个名字指向**同一份数据**：删除其中任一个，另一个也就没了。
 *    所以它不等于"复制出了两份独立文件"——清理旧包时别按复制的直觉以为 dist/ 那份还在。
 */
function linkOrCopy(src, dest, hardlink) {
	if (hardlink && src !== dest) {
		try {
			fs.linkSync(src, dest);
			log(`  已硬链接 → ${path.relative(ROOT, dest)}（零额外占用）`);
			return;
		} catch (e) {
			log(`  硬链接不可用（${e.code || e.message}），回落复制`);
		}
	}
	fs.copyFileSync(src, dest);
	log(`  已复制 → ${path.relative(ROOT, dest)}`);
}

async function cmdPublish() {
	const product = arg('product');
	const version = arg('version');
	const file = arg('file');
	const notes = arg('notes', '');
	const mandatory = has('mandatory');
	const dryRun = has('dry-run');
	// 2026-10-06 新增：同盘下优先用硬链接落盘，省掉一份 125MB 的复制。
	const useHardlink = has('hardlink');

	if (typeof product !== 'string' || !PRODUCTS.includes(product)) {
		fail(`--product 必须是 ${PRODUCTS.join(' 或 ')}`);
	}
	// 2026-10-07：放开候选号后缀（0.6.23-rc.1 / 0.6.23-alpha.1 也发得出去）。规则见版本号命名规范。
	// ⚠️ 这条正则必须和 scripts/version-compare.mjs 的 isCandidate() 同形 —— 阶段号从 1 起。
	//    原先这里是 \d+，会放进 0.6.23-alpha.0：被控端 compareVersion 解析阶段号时要求 n>=1，
	//    于是 alpha.0 被当成"无阶段 = 正式版" ⇒ 云端发了 alpha.0、教室机判定"已是最新"不升级，
	//    等正式版 0.6.23 再发时比较结果也是相等 ⇒ **同样的版本推不下去**。2026-10-07 预演抓到。
	if (typeof version !== 'string' || !/^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.[1-9]\d*)?$/.test(version)) {
		fail('--version 必填，形如 0.6.0（正式）、0.6.23-alpha.1（内测）或 0.6.23-rc.1（候选）；'
			+ '见 docs/版本号命名规范-2026-10-07.md');
	}
	if (typeof file !== 'string') fail('--file 必填：要发布的安装包路径');
	if (!fs.existsSync(file)) fail(`文件不存在：${file}`);
	const st = fs.statSync(file);
	if (!st.isFile() || st.size === 0) fail(`不是一个有效文件（大小 ${st.size}）：${file}`);

	const baseName = path.basename(file);
	if (!baseName.includes(version)) {
		log(
			`⚠ 文件名 ${baseName} 里没有版本号 ${version}。\n` +
				`  建议改名成带版本的形式（如 stelarith-agent-setup-${version}.exe）再发 ——\n` +
				`  否则 assets/pkg/ 里会堆出同名文件互相覆盖，事后无法从文件名分辨是哪个版本。`
		);
	}

	const m = readManifest({ allowMissing: true });
	m.products = m.products && typeof m.products === 'object' ? m.products : {};
	const prev = m.products[product] || null;

	// 版本倒退是危险动作：会被推给全校教室机 → 默认拒绝，必须显式 --allow-downgrade
	if (prev && prev.version && cmpVersion(version, prev.version) < 0 && !has('allow-downgrade')) {
		fail(
			`拒绝发布：${product} 当前最新是 v${prev.version}，这次要发的 v${version} 更低。\n` +
				`  这会把所有教室机"升级"到旧版本。确属回滚请加 --allow-downgrade。`
		);
	}

	// 主机与目录：命令行 > 环境变量 > 沿用上一版 URL 里的写法
	const inferred = inferBase(prev);
	const baseUrl = typeof arg('base-url') === 'string' ? arg('base-url') : process.env.CLOUD_PUBLIC_BASE || null;
	let origin = inferred ? inferred.origin : null;
	let dir = inferred ? inferred.dir : '/assets/pkg';
	if (baseUrl) {
		try {
			const u = new URL(baseUrl);
			origin = u.origin;
			dir = u.pathname && u.pathname !== '/' ? path.posix.dirname(u.pathname) : dir;
		} catch {
			fail(`--base-url / CLOUD_PUBLIC_BASE 不是合法地址：${baseUrl}`);
		}
	}
	if (!origin) {
		fail(
			'不知道对外地址。请任选一种：\n' +
				'  · 加 --base-url https://你的域名\n' +
				'  · 或设环境变量 CLOUD_PUBLIC_BASE=https://你的域名\n' +
				'  · 或先手工建一份带 url 的 ota.json（之后会自动沿用）'
		);
	}
	const url = `${origin}${dir.replace(/\/$/, '')}/${encodeURIComponent(baseName)}`;

	log(`发布 ${product} v${version}`);
	log(`  源文件  ${file}`);
	log(`  体积    ${st.size} (${humanSize(st.size)})`);
	log(`  目标 URL ${url}`);

	log('\n计算 sha256 …');
	const sha = await sha256File(file);
	log(`  sha256  ${sha}`);

	// ── 发布前包内容自检（2026-10-10 事故）：缺运行时件一律拒发 ──────────────
	// 与 size/sha256 校验互补：那两个只保证"清单描述的是磁盘上这个文件"，
	// 不保证"这个文件装上去能跑"。缺 libssl/libcrypto-3-x64.dll 的包就是这样发出去的。
	const chk = verifyPackageContent(product, file);
	if (chk.error) {
		fail(
			`发布前包内容自检无法进行：${chk.error}\n` +
				`  这是硬门槛 —— 宁可发不出去，也不发一个装完起不来的包（7z 路径可用 SEVEN_ZIP 覆盖）。`
		);
	}
	if (chk.warning) {
		log(`  ⚠ ${chk.warning}`);
	}
	if (!chk.ok) {
		fail(
			`拒绝发布：${path.basename(file)} 里**缺必需件** —— ${chk.missing.join(', ')}\n` +
				`  这些是 exe 启动即加载的运行时件（如 OpenSSL 3 的 libssl-3-x64.dll /\n` +
				`  libcrypto-3-x64.dll，由 libdatachannel 的 DTLS 引入）。装上去会直接\n` +
				`  「系统错误：找不到 xxx.dll」。修法：检查 .nsi / make-portable*.mjs 的打包\n` +
				`  清单后重新出包（见各脚本的 REQUIRED）。`
		);
	}
	log(`  包内容自检 ${chk.warning ? '⚠ 跳过（Inno 安装器，信任编译期验证）' : `✓（${REQUIRED_ENTRIES[product].length} 个必需件齐）`}`);

	if (dryRun) {
		log('\n--dry-run：不复制文件、不改清单。');
		return true;
	}

	// 落盘（**不是移动**）：源文件留在原处，用户的规定。
	fs.mkdirSync(PKG_DIR, { recursive: true });
	const dest = path.join(PKG_DIR, baseName);
	if (fs.existsSync(dest)) {
		const sameSha = (await sha256File(dest)) === sha;
		log(`\n目标已存在 ${path.relative(ROOT, dest)}：${sameSha ? '内容相同，跳过复制' : '内容不同，覆盖'}`);
		// ⚠️ 覆盖场景**禁硬链接**（第三参传 false）：
		//    硬链接的两个名字指向同一份数据，覆盖 dest 就等于原地改写 dist/ 那份源包 —— 那不叫复制。
		if (!sameSha) linkOrCopy(file, dest, false);
	} else {
		// 新包：同盘 NTFS 下优先硬链接（零额外占用）。走的是链接还是回落成复制，linkOrCopy 内部已 log。
		linkOrCopy(file, dest, useHardlink);
	}

	// 回读目标文件校验：复制也可能出岔子（磁盘满、被占用），清单必须描述真实存在的东西
	const destSt = fs.statSync(dest);
	const destSha = await sha256File(dest);
	if (destSt.size !== st.size || destSha !== sha) {
		fail(`复制后校验失败（大小 ${destSt.size}/${st.size}，sha256 ${destSha === sha ? '一致' : '不一致'}）`);
	}

	m.products[product] = {
		version,
		url,
		sha256: sha,
		size: destSt.size,
		notes: notes || `v${version}`,
		mandatory
	};
	// 顶层元信息：只在这一层补，不动 products
	m.generatedAt = new Date().toISOString();
	m.generatedBy = 'scripts/publish-release.mjs';

	const len = writeManifest(m);
	log(`\n已写入 ${path.relative(ROOT, MANIFEST)}（UTF-8 无 BOM，${len} 字节，回读解析通过）`);

	// ── 发布后自验（2026-10-20 加）：清单写完了不算完，要能真的取到 ──────────
	// 为什么：ota.json 只是"权威声明"，从声明到生效还有一层 —— 云端进程**每请求现读磁盘**。
	// BOM、进程 cwd 与清单目录不符、CDN 缓存，都会让"写对了的清单"表现为接口
	// 假"未发布"（latest:null），而发布脚本自己是全绿的。这类假成功等到教室机
	// 来报"没有新版"才暴露，比发布时直接炸晚得多。所以发布后立刻打一次公网站点。
	// --no-verify：内网发布/临时网络不可达时跳过（只警告，不把"没网"渲染成"发版失败"）。
	if (has('no-verify')) {
		log('\n--no-verify：跳过发布后自验');
	} else {
		const probe = `${origin}/api/public/ota?product=${encodeURIComponent(product)}&channel=${encodeURIComponent(CHANNEL)}`;
		log(`\n发布后自验：GET ${probe}`);
		try {
			const resp = await fetch(probe, { signal: AbortSignal.timeout(20000) });
			const j = await resp.json().catch(() => null);
			const got = j && j.latest && j.latest.version;
			if (got !== version) {
				fail(
					`发布后自验失败：公网返回 v${got || '(未发布)'}，不是刚发的 v${version}。\n`
					+ `  清单已写入 ${path.relative(ROOT, MANIFEST)}，但端点取到的不是它 —— 典型原因：\n`
					+ `  云端进程 cwd 与清单目录不一致 / 文件带 BOM / CDN 还在缓存（private 60s）。\n`
					+ `  人工核对：curl "${probe}"`
				);
			}
			if (j.latest.sha256 !== sha) {
				fail(
					`发布后自验失败：版本号对上了，但 sha256 不一致 —— 公网清单里 v${version} 的包不是刚发的这个。\n`
					+ `  典型原因：同一版本号发过两个不同的包（并发发布/重打包）。人工核对：curl "${probe}"`
				);
			}
			log(`  ✓ 公网已生效：v${got}，sha256 一致`);
		} catch (e) {
			// 网络不可达 ≠ 发布失败：清单已落盘，云端现读即生效。警告 + 给人工核对命令。
			log(`  ⚠ 自验请求失败（${e.message}）—— 清单已写入，但未能确认公网生效。`);
			log(`     内网发布/临时网络问题属预期；确认无碍后可加 --no-verify 跳过。`);
			log(`     人工核对：curl "${probe}"`);
		}

		// ── 发布后资产可达性自验（2026-10-31 加 · 404 事故闭环）──────────────
		// 为什么必须：上面的 /api/public/ota 自验只证明"清单能取到"，不证明
		// 「清单 url 指向的文件真的能下载」。10-30 事故 = assets/pkg/ 被清空，
		// 清单完全正常、接口完全正常、教室机下载却 404 —— 这类故障在"自验"的眼皮
		// 底下溜过去，等教室机报"更新失败"才暴露。这里直接 HEAD/GET 资产 URL：
		//   · 非 200 → 硬失败（清单指向的文件不可达，发了也白发）；
		//   · content-length 与清单 size 不符 → 硬失败（CDN/代理吐了残缺体）；
		// 网络不可达仍算警告（与上方同口径，--no-verify 一并跳过）。
		try {
			const assetProbe = `${origin}${dir.replace(/\/$/, '')}/${encodeURIComponent(baseName)}`;
			log(`  资产自验：GET ${assetProbe}（HEAD，不下载包体）`);
			const aresp = await fetch(assetProbe, { method: 'HEAD', signal: AbortSignal.timeout(20000) });
			if (aresp.status !== 200) {
				fail(
					`资产自验失败：GET ${assetProbe} 返回 HTTP ${aresp.status}（期望 200）。\n`
					+ `  清单已声明 url，但文件取不到 —— 这等于"发了但教室机永远下不动"。\n`
					+ `  人工核对：curl -I "${assetProbe}"；确认文件已在 ${path.relative(ROOT, dest)}。`
				);
			}
			const gotLen = Number(aresp.headers.get('content-length') || 0);
			if (gotLen && gotLen !== st.size) {
				fail(
					`资产自验失败：${assetProbe} 的 content-length=${gotLen}，清单 size=${st.size}。\n`
					+ `  要么 CDN/代理缓存了残缺体，要么服务器返回了别的文件 —— 教室机会校验失败。`
				);
			}
			log(`  ✓ 资产可达：HTTP 200，content-length 一致（${humanSize(st.size)}）`);
		} catch (e) {
			log(`  ⚠ 资产自验请求失败（${e.message}）—— 未确认资产可达（网络问题或 --no-verify 场景）。`);
			log(`     人工核对：curl -I "${origin}${dir.replace(/\/$/, '')}/${encodeURIComponent(baseName)}"`);
		}
	}

	log('\n下一步可以做的验证：');
	log(`  · 本地体检：node scripts/publish-release.mjs check${CHANNEL !== 'stable' ? ` --channel ${CHANNEL}` : ''}`);
	log(`  · 官网下载页会自动跟着变：它读的就是这个接口，站点无需重新构建`);
	if (CHANNEL !== 'stable') {
		log(`  · 灰度只下发给 register 带 channel=${CHANNEL} 的设备；要全量请同包再 --channel stable 发一次`);
	}

	log(
		`\n提醒：其它产品条目未被改动` +
			(Object.keys(m.products).length > 1 ? `（当前还有：${Object.keys(m.products).filter((k) => k !== product).join(', ')}）` : '') +
			'。'
	);
	return true;
}

// ── 入口 ────────────────────────────────────────────────────────────────────
let ok = false;
if (cmd === 'check') ok = await cmdCheck();
else if (cmd === 'show') ok = await cmdShow();
else if (cmd === 'publish') ok = await cmdPublish();
else {
	log(`未知命令：${cmd}`);
	log('可用：check | show | publish');
	process.exit(1);
}
process.exit(ok ? 0 : 1);
