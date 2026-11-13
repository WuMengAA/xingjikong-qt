// 噪音存证（2026-10-30 · P0-2 云端）
//
// 职责：接收被控端发来的 noise_incident 事件（三张图 base64 + 元数据），落盘；
//   · 图片仅保留 5 分钟（从触发时刻起算），到期后拉不到（读时判定 + 周期清理）；
//   · 元数据（节次 / 次数 / 读数 / 频次依赖）长期保留。
//
// 鉴权：复用 /recordings 同口径（Bearer CLOUD_VIEWER_TOKEN 或网站票据）+ 按班 scope（canAccess）。
//   权限语义：班主任（按班 scope）+ 管理员（平台超管 / 全权静态令牌）；
//   「年级管理员」语义未定 ⇒ 见下方单一常量 TODO，暂未启用（禁止自创角色模型）。
import { existsSync, readFileSync, statSync, readdirSync, mkdirSync, writeFileSync, unlinkSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));

const RETENTION_MS = 5 * 60 * 1000;   // 图片保留 5 分钟（从触发时刻起算）

// ⚠️ 权限：本批只实现「班主任（按班 scope）+ 管理员（平台超管 / 全权静态令牌）」，
//   与既有 /recordings 口径一致（canAccess + isPlatformIdent）。
//   「年级管理员」语义未定 ⇒ 留 TODO 并以**单一常量**集中标注；禁止自创角色模型。
export const GRADE_ADMIN_ROLE = 'grade_admin';  // TODO(权限): 年级管理员语义未定，暂不启用此角色

// 与 index.js 同源：CLOUD_RECORDINGS_DIR 可覆盖（隔离测试指到临时目录）。
const RECORDINGS_ROOT = process.env.CLOUD_RECORDINGS_DIR
  ? resolve(process.env.CLOUD_RECORDINGS_DIR)
  : join(__dirname, '..', 'recordings');
const EVIDENCE_DIR = join(RECORDINGS_ROOT, 'noise_evidence');   // 图片（5 分钟）
const META_DIR = join(RECORDINGS_ROOT, 'noise_events');          // 元数据（长期）

/**
 * 「有数就写，没数就不写」—— 2026-11-14 修的一类事故。
 *
 * 旧写法是 Number(payload.x) || 0：被控端把「没测到」序列化成 JSON null
 * （NaN 没法进 JSON），Number(null) 正好是 0，于是库里「没测到」变成了「测得 0 dB」。
 * 后果不是显示难看，而是**判据失效**：0 dB 是环境绝对静音，与真实读数混在一起之后，
 * 任何按 dB 做的阈值判断、排行、对照都没法信。
 *
 * 现在只接受「真的能解析成有限数」的输入，其余**整个字段不出现**。
 * 上层用 Object.hasOwn(meta, 'db') 区分「有读数」与「没有读数」，语义不再丢失。
 */
function numOrAbsent(target, key, raw) {
  if (raw === null || raw === undefined || raw === '') return;   // 明确没有
  const v = Number(raw);
  if (Number.isFinite(v)) target[key] = v;
}

function b64decode(s) {
  if (typeof s !== 'string' || !s) return null;
  try { return Buffer.from(s, 'base64'); } catch { return null; }
}

/**
 * 落盘一条噪音存证事件。
 * @param {string} uid   设备 uid（来自 WS 鉴权后已知，不信任自报）
 * @param {object} payload  被控端发来的 JSON：at / periodKey / hits / db / baselineDb /
 *                          thresholdDb / desktop(base64) / camera(base64) / *Error
 * @returns {{ok:boolean,id:string,expiry:number,savedImages:string[],errors:string[],meta:object}}
 */
export function storeNoiseIncident(uid, payload = {}) {
  const atStr = typeof payload.at === 'string' ? payload.at : '';
  const atMs = Date.parse(atStr);
  const validAt = Number.isFinite(atMs) ? atMs : Date.now();
  const periodKey = typeof payload.periodKey === 'string' ? payload.periodKey.slice(0, 64) : '';
  const id = `${uid}-${validAt}-${Math.random().toString(36).slice(2, 8)}`;
  const expiry = validAt + RETENTION_MS;   // 5 分钟（从触发时刻起算）

  const meta = {
    id, uid,
    at: new Date(validAt).toISOString(),
    atMs: validAt,
    periodKey,
    // hits 是有意义的自然数（0 = 没累计到），保持 0 兜底；
    // 三个 dB 读数不同 —— 缺失与 0 是两件事，交给 numOrAbsent 决定写不写。
    hits: Number(payload.hits) || 0,
    desktop: false,
    camera: false,
    // 声谱图（2026-11-14）：把触发那一窗的声音画成图。它与画面证据互补 ——
    // 屏幕在"教室很吵"这一刻常常毫无异常，而声音的形状已经把"是空调还是人"说清。
    spectrogram: false,
    desktopError: '',
    cameraError: '',
    spectrogramError: '',
    expiry,
  };

  numOrAbsent(meta, 'db', payload.db);
  numOrAbsent(meta, 'baselineDb', payload.baselineDb);
  numOrAbsent(meta, 'thresholdDb', payload.thresholdDb);

  // 声学指纹（「是什么声音」，2026-11-14 起被控端会带上）。原样透传，只做类型收窄：
  // 云端不认识声学，硬做校验只会把将来加的字段挡在门外。
  // 关键仍是「有没有」：没有 spectrum 字段 = 这台设备还没跑带指纹的版本，
  // 与「采到了但全是 0」必须能分开。
  if (payload.spectrum && typeof payload.spectrum === 'object') {
    meta.spectrum = payload.spectrum;
  }

  const savedImages = [];
  const errors = [];
  // 字段名 → 落盘后缀。三路一致：库里只看后缀认得出是哪一张，
  // 且 readNoiseEvidence / sweepExpiredNoise 用同一个正则，不会漏掉新加的一路。
  // ⚠️ 后缀必须与 readNoiseEvidence 的 /-(desk|cam|spec)\.jpg$/ 保持同步 ——
  //    两处不一致的后果是"图存了但永远读不出来"，且不会报任何错。
  const SUFFIX = { desktop: 'desk', camera: 'cam', spectrogram: 'spec' };
  const saveImg = (field, b64, errField) => {
    const buf = b64decode(payload[field]);
    if (buf && buf.length) {
      const fname = `${id}-${SUFFIX[field]}.jpg`;
      const dir = join(EVIDENCE_DIR, uid);
      mkdirSync(dir, { recursive: true });
      writeFileSync(join(dir, fname), buf);
      meta[field] = true;
      savedImages.push(fname);
    } else {
      const e = typeof payload[errField] === 'string' ? payload[errField] : '';
      meta[errField] = e;
      errors.push(e ? `${field}:${e}` : `${field}:未采集`);
    }
  };
  saveImg('desktop', payload.desktop, 'desktopError');
  saveImg('camera', payload.camera, 'cameraError');
  saveImg('spectrogram', payload.spectrogram, 'spectrogramError');

  // 元数据长期保留：即使两张图都抓不到，事件本身也要留（频次 / 排行依赖它）。
  const mdir = join(META_DIR, uid);
  mkdirSync(mdir, { recursive: true });
  writeFileSync(join(mdir, `${id}.json`), JSON.stringify(meta, null, 2));

  return { ok: true, id, expiry, savedImages, errors, meta };
}

export function listNoiseIncidents(uid) {
  const mdir = join(META_DIR, uid);
  if (!existsSync(mdir)) return [];
  return readdirSync(mdir)
    .filter((n) => n.endsWith('.json'))
    .map((n) => {
      try { return JSON.parse(readFileSync(join(mdir, n), 'utf8')); }
      catch { return null; }
    })
    .filter(Boolean)
    .sort((a, b) => b.atMs - a.atMs);
}

/**
 * 读图片：过期或不存在 ⇒ 返回 null（HTTP 层据此 404，证明"到期后拉不到"）。
 * 元数据里记了 expiry，读时即时判定，不依赖文件系统时间。
 */
export function readNoiseEvidence(uid, filename) {
  const base = filename.replace(/-(desk|cam|spec)\.jpg$/, '');
  const mfile = join(META_DIR, uid, `${base}.json`);
  if (!existsSync(mfile)) return null;
  let meta;
  try { meta = JSON.parse(readFileSync(mfile, 'utf8')); } catch { return null; }
  if (Date.now() > (meta.expiry || 0)) return null;   // 5 分钟到期
  const file = join(EVIDENCE_DIR, uid, filename);
  if (!existsSync(file)) return null;
  return { buffer: readFileSync(file), contentType: 'image/jpeg' };
}

/** 周期清理：删掉已过期的图片文件（元数据保留，供频次 / 排行）。 */
export function sweepExpiredNoise() {
  if (!existsSync(EVIDENCE_DIR)) return { removed: 0 };
  const now = Date.now();
  let removed = 0;
  for (const uid of readdirSync(EVIDENCE_DIR)) {
    const dir = join(EVIDENCE_DIR, uid);
    if (!statSync(dir).isDirectory()) continue;
    const mdir = join(META_DIR, uid);
    for (const f of readdirSync(dir)) {
      const m = /^(.*)-(desk|cam|spec)\.jpg$/.exec(f);
      if (!m) continue;
      let expired = true;
      const metaFile = join(mdir, `${m[1]}.json`);
      if (existsSync(metaFile)) {
        try { const meta = JSON.parse(readFileSync(metaFile, 'utf8')); expired = now > (meta.expiry || 0); }
        catch { expired = true; }
      }
      if (expired) { try { unlinkSync(join(dir, f)); ++removed; } catch { /* ignore */ } }
    }
  }
  return { removed };
}
