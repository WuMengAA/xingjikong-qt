// 噪音存证（2026-10-30 · P0-2 云端）
//
// 职责：接收被控端发来的 noise_incident 事件（两张图 base64 + 元数据），落盘；
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
    hits: Number(payload.hits) || 0,
    db: Number(payload.db) || 0,
    baselineDb: Number(payload.baselineDb) || 0,
    thresholdDb: Number(payload.thresholdDb) || 0,
    desktop: false,
    camera: false,
    desktopError: '',
    cameraError: '',
    expiry,
  };

  const savedImages = [];
  const errors = [];
  const saveImg = (field, b64, errField) => {
    const buf = b64decode(payload[field]);
    if (buf && buf.length) {
      const fname = `${id}-${field === 'desktop' ? 'desk' : 'cam'}.jpg`;
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
  const base = filename.replace(/-(desk|cam)\.jpg$/, '');
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
      const m = /^(.*)-(desk|cam)\.jpg$/.exec(f);
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
