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
 *          [--notes "本次改了什么"] [--mandatory] [--base-url https://host] [--dry-run]
 *       算 sha256/大小 → **复制**（不是移动）到 assets/pkg/ → 只改 ota.json 里这一个产品
 *       → 回读校验 → 打印结果。
 *
 * 退出码：0 成功 / 1 失败（失败时不会留下半成品：ota.json 只在全部校验通过后才写）
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const MANIFEST = path.join(ROOT, process.env.CLOUD_OTA_MANIFEST_FILE || 'ota.json');
const EXAMPLE = path.join(ROOT, 'ota.json.example');
const PKG_DIR = path.join(ROOT, process.env.CLOUD_ASSETS_DIR || 'assets', 'pkg');
const PRODUCTS = ['agent', 'viewer'];

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

/** 按点分段比较版本，只认数字段（0.6.0 > 0.5.1）。非数字段一律当 0，避免 NaN 传染。 */
function cmpVersion(a, b) {
	const pa = String(a).split('.').map((x) => parseInt(x, 10) || 0);
	const pb = String(b).split('.').map((x) => parseInt(x, 10) || 0);
	for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
		const d = (pa[i] || 0) - (pb[i] || 0);
		if (d) return d > 0 ? 1 : -1;
	}
	return 0;
}

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
			log(`${tag} ✓ v${e.version}  ${humanSize(st.size)}  sha256 一致`);
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
async function cmdPublish() {
	const product = arg('product');
	const version = arg('version');
	const file = arg('file');
	const notes = arg('notes', '');
	const mandatory = has('mandatory');
	const dryRun = has('dry-run');

	if (typeof product !== 'string' || !PRODUCTS.includes(product)) {
		fail(`--product 必须是 ${PRODUCTS.join(' 或 ')}`);
	}
	if (typeof version !== 'string' || !/^\d+(\.\d+)*$/.test(version)) {
		fail('--version 必填，形如 0.6.0');
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

	if (dryRun) {
		log('\n--dry-run：不复制文件、不改清单。');
		return true;
	}

	// 复制（**不是移动**）：源文件留在原处，用户的规定。
	fs.mkdirSync(PKG_DIR, { recursive: true });
	const dest = path.join(PKG_DIR, baseName);
	if (fs.existsSync(dest)) {
		const sameSha = (await sha256File(dest)) === sha;
		log(`\n目标已存在 ${path.relative(ROOT, dest)}：${sameSha ? '内容相同，跳过复制' : '内容不同，覆盖'}`);
		if (!sameSha) fs.copyFileSync(file, dest);
	} else {
		fs.copyFileSync(file, dest);
		log(`\n已复制 → ${path.relative(ROOT, dest)}`);
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

	log('\n下一步可以做的验证：');
	log(`  · 本地体检：node scripts/publish-release.mjs check`);
	log(`  · 云端现读生效（不必重启）：curl "${origin}/api/public/ota?product=${product}"`);
	log(`  · 官网下载页会自动跟着变：它读的就是这个接口，站点无需重新构建`);

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
