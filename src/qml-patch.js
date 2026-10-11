// 云端 QML 热更补丁（qml-patch，2026-10-11 · 一源响应式配套）
//
// 用户裁决：「手机定期拉取桌面端的做响应式」——手机端要能拿到与桌面
// 完全一致的 QML 树（Main.qml + components/ + schedule/ + mobile/），
// 下载解压后本地优先加载，改 UI 不用重出 APK。
//
// 实现：
//   · 打包源 = Stelarith-viewer-qt/qml/（桌面热更同源，天然一致）
//   · 版本   = viewer-qt git HEAD 前 7 位（QML 树有变 hash 就变）
//   · 产物   = assets/qml-patch/<ver>.zip（store 模式 zip，Node 内置 zlib 拼）
//   · 清单   = GET /api/qml-patch → { ok, version, url }（Bearer 鉴权）
//   · 下载   = GET /api/qml-patch/download → zip 二进制
//
// zip 格式手拼（store 模式，无压缩）：PNG 等 QML 引用的二进制资源已压缩过，
// QML 文本也不大，store 足够；避免引第三方 zip 依赖。

import { existsSync, readdirSync, statSync, readFileSync, writeFileSync, mkdirSync, unlinkSync } from 'node:fs';
import { join, relative } from 'node:path';
import { spawnSync } from 'node:child_process';
import { ASSETS_DIR } from './config.js';

/** 打包源：viewer-qt 的 qml/ 目录（与桌面热更同一棵树）。 */
const QML_SRC = process.env.CLOUD_QML_SRC || 'D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-viewer-qt/qml';

const PATCH_DIR = join(ASSETS_DIR, 'qml-patch');
const MANIFEST = join(PATCH_DIR, 'latest.json');

/** 版本 = viewer-qt git HEAD 前 7 位；拿不到（无 .git）就用 qml 树 mtime 和。 */
function currentVersion() {
  const r = spawnSync('git', ['-C', 'D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-viewer-qt', 'rev-parse', '--short=7', 'HEAD'], { encoding: 'utf8' });
  if (r.status === 0 && r.stdout.trim()) return r.stdout.trim();
  let sum = 0;
  const walk = (d) => {
    for (const n of readdirSync(d)) {
      const p = join(d, n);
      const s = statSync(p);
      sum += s.size + (s.mtimeMs | 0);
      if (s.isDirectory()) walk(p);
    }
  };
  try { walk(QML_SRC); } catch { return 'unknown'; }
  return 'm' + (sum % 0xffffffff).toString(16);
}

/** store 模式 zip：本地文件头 + 中央目录 + EOCD（无压缩，CRC 算一下）。 */
function crc32(buf) {
  let c = ~0;
  for (let i = 0; i < buf.length; ++i) {
    c ^= buf[i];
    for (let k = 0; k < 8; ++k) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

/** 递归收集 qml 树文件（跳过非 qml/目录、隐藏文件）。 */
function collectFiles(dir, out, base) {
  for (const n of readdirSync(dir)) {
    if (n.startsWith('.')) continue;
    const p = join(dir, n);
    const s = statSync(p);
    if (s.isDirectory()) collectFiles(p, out, base);
    else out.push({ abs: p, rel: relative(base, p).split('\\').join('/') });
  }
}

/** 生成/刷新补丁包。QML 树没变（版本同）就跳过。 */
export function ensureQmlPatch() {
  try {
    if (!existsSync(QML_SRC)) return null;
    const ver = currentVersion();
    mkdirSync(PATCH_DIR, { recursive: true });
    if (existsSync(MANIFEST)) {
      try {
        const cur = JSON.parse(readFileSync(MANIFEST, 'utf8'));
        if (cur.version === ver && existsSync(join(PATCH_DIR, ver + '.zip'))) return cur;
      } catch { /* 清单坏 → 重打 */ }
    }
    const files = [];
    collectFiles(QML_SRC, files, QML_SRC);
    const chunks = [];
    const central = [];
    let offset = 0;
    for (const f of files) {
      const data = readFileSync(f.abs);
      const name = Buffer.from(f.rel, 'utf8');
      const crc = crc32(data);
      const lh = Buffer.alloc(30);
      lh.writeUInt32LE(0x04034b50, 0);          // local file header sig
      lh.writeUInt16LE(20, 4);                  // version needed
      lh.writeUInt16LE(0, 6);                   // flags
      lh.writeUInt16LE(0, 8);                   // method = store
      lh.writeUInt16LE(0, 10);                  // mod time
      lh.writeUInt16LE(0, 12);                  // mod date
      lh.writeUInt32LE(crc, 14);
      lh.writeUInt32LE(data.length, 18);
      lh.writeUInt32LE(data.length, 22);
      lh.writeUInt16LE(name.length, 26);
      lh.writeUInt16LE(0, 28);                  // extra len
      chunks.push(lh, name, data);
      const ch = Buffer.alloc(46);
      ch.writeUInt32LE(0x02014b50, 0);          // central dir sig
      ch.writeUInt16LE(20, 4);                  // version made by
      ch.writeUInt16LE(20, 6);                  // version needed
      ch.writeUInt16LE(0, 8);                   // flags
      ch.writeUInt16LE(0, 10);                  // method
      ch.writeUInt16LE(0, 12);                  // mod time
      ch.writeUInt16LE(0, 14);                  // mod date
      ch.writeUInt32LE(crc, 16);
      ch.writeUInt32LE(data.length, 20);
      ch.writeUInt32LE(data.length, 24);
      ch.writeUInt16LE(name.length, 28);
      ch.writeUInt16LE(0, 30);                  // extra
      ch.writeUInt16LE(0, 32);                  // comment
      ch.writeUInt16LE(0, 34);                  // disk
      ch.writeUInt16LE(0, 36);                  // int attr
      ch.writeUInt32LE(0, 38);                  // ext attr
      ch.writeUInt32LE(offset, 42);             // local header offset
      central.push(ch, name);
      offset += lh.length + name.length + data.length;
    }
    const eocd = Buffer.alloc(22);
    eocd.writeUInt32LE(0x06054b50, 0);
    eocd.writeUInt16LE(0, 4);
    eocd.writeUInt16LE(0, 6);
    eocd.writeUInt16LE(files.length, 8);
    eocd.writeUInt16LE(files.length, 10);
    eocd.writeUInt32LE(Buffer.concat(central).length, 12);
    eocd.writeUInt32LE(offset, 16);
    eocd.writeUInt16LE(0, 20);
    const zip = Buffer.concat([...chunks, ...central, eocd]);
    const zipPath = join(PATCH_DIR, ver + '.zip');
    writeFileSync(zipPath, zip);
    const manifest = { version: ver, url: `/api/qml-patch/download`, size: zip.length, files: files.length, builtAt: new Date().toISOString() };
    writeFileSync(MANIFEST, JSON.stringify(manifest, null, 2));
    // 清旧包（只留当前）
    for (const n of readdirSync(PATCH_DIR)) {
      if (n.endsWith('.zip') && n !== ver + '.zip') {
        try { unlinkSync(join(PATCH_DIR, n)); } catch { /* 忽略 */ }
      }
    }
    return manifest;
  } catch (e) {
    console.error(`[cloud] FAIL qml-patch 打包失败: ${e.message}`);
    return null;
  }
}

/** 当前清单（不触发重打包，给 HTTP 用）。 */
export function qmlPatchManifest() {
  try {
    if (existsSync(MANIFEST)) return JSON.parse(readFileSync(MANIFEST, 'utf8'));
  } catch { /* 无 */ }
  return ensureQmlPatch();
}

/** 当前 zip 的磁盘路径（给下载用）。 */
export function qmlPatchZipPath() {
  const m = qmlPatchManifest();
  if (!m) return null;
  const p = join(PATCH_DIR, m.version + '.zip');
  return existsSync(p) ? p : null;
}
