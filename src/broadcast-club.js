// 广播社成员（云端自管账号）+ VoiceHub 排期写代理（2026-10-23）
// ─────────────────────────────────────────────────────────────────────────────
// 设计边界（用户拍板 · 推荐方案）：
//   · 账号体系自包含在云端（用户名 + scrypt 哈希，落 club-members.json），
//     不依赖站点 OAuth、不碰 VoiceHub 后端（红线外工程）。
//   · 成员只能管自己归属的 station 的排期；写操作经云端代理到 VoiceHub
//     （用云端持有的写密钥），云端不把密钥下发给成员；每次写落审计。
//   · 成员令牌复用 ticket.js 的 HMAC 票（claims 里带 roles:['broadcast_club'] + station），
//     与现有 viewer 票同一签名密钥，便于复用 verifyViewerTicket。
//
// ⚠️ 安全纪律：
//   · 密码用 node:crypto.scrypt（盐 16B），恒定时间比较，防时序侧信道。
//   · 密钥/密码明文绝不进日志；审计只记 {who, when, station, before, after, ok}。
//   · 成员令牌在管理面鉴权门被显式拒绝（见 index.js），防止越权进管理面。

import crypto from 'node:crypto';
import { readFileSync, renameSync, writeFileSync, existsSync, mkdirSync, appendFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mintViewerTicket, verifyViewerTicket } from './ticket.js';
import {
  VOICEHUB_WRITE_BASE, VOICEHUB_WRITE_KEY, CLUB_MEMBERS_FILE, CLUB_AUDIT_FILE, CLUB_TOKEN_TTL_MS,
} from './config.js';

const DEFAULT_MEMBERS = join(dirname(fileURLToPath(import.meta.url)), '..', CLUB_MEMBERS_FILE);
const auditFile = CLUB_AUDIT_FILE;

// username(lower) -> { username, pwHash, pwSalt, station, createdAt, disabled, createdBy }
const members = new Map();
let membersFile = DEFAULT_MEMBERS;
let initialized = false;

/** 初始化（幂等）：加载已有成员库，损坏时退化为空存储。 */
export function initBroadcastClub(filePath = DEFAULT_MEMBERS) {
  membersFile = filePath;
  members.clear();
  try {
    if (existsSync(membersFile)) {
      const raw = JSON.parse(readFileSync(membersFile, 'utf8'));
      const list = raw && raw.members && typeof raw.members === 'object' ? raw.members : {};
      for (const [u, m] of Object.entries(list)) {
        if (!u || !m || typeof m !== 'object') continue;
        if (!m.pwHash || !m.pwSalt) continue; // 缺哈希的脏行跳过
        members.set(String(u).toLowerCase(), {
          username: m.username || u,
          pwHash: m.pwHash, pwSalt: m.pwSalt,
          station: typeof m.station === 'string' ? m.station : '',
          createdAt: m.createdAt || null,
          disabled: !!m.disabled,
          createdBy: m.createdBy || 'unknown',
        });
      }
    }
  } catch (e) {
    members.clear(); // 损坏退化空存储，行为=无成员
  }
  initialized = true;
  return { loaded: members.size, file: membersFile };
}

function ensureInit() { if (!initialized) initBroadcastClub(); }

function persist() {
  const snapshot = { v: 1, updatedAt: new Date().toISOString(), members: Object.fromEntries(members) };
  const tmp = `${membersFile}.tmp`;
  try {
    const dir = dirname(membersFile);
    if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
    writeFileSync(tmp, JSON.stringify(snapshot, null, 2), 'utf8');
    renameSync(tmp, membersFile);
    return true;
  } catch {
    return false;
  }
}

function hashPassword(pw, salt) {
  return crypto.scryptSync(String(pw), salt, 64).toString('hex');
}

// ── 账号管理（管理面调用）───────────────────────────────────────────────────

export function createClubMember(username, password, station, by = 'unknown') {
  ensureInit();
  const u = String(username || '').trim().toLowerCase();
  if (!u) return { ok: false, error: '用户名不能为空' };
  if (!/^[a-z0-9_.-]{3,32}$/.test(u)) return { ok: false, error: '用户名限 3-32 位字母数字 _ . -' };
  if (!password || String(password).length < 6) return { ok: false, error: '密码至少 6 位' };
  const st = String(station || '').trim();
  if (!st) return { ok: false, error: '必须指定归属 station（班级/广播站码）' };
  if (members.has(u)) return { ok: false, error: '该用户名已存在' };
  const salt = crypto.randomBytes(16).toString('hex');
  const pwHash = hashPassword(password, salt);
  members.set(u, {
    username: u, pwHash, pwSalt: salt, station: st,
    createdAt: new Date().toISOString(), disabled: false, createdBy: by,
  });
  const persisted = persist();
  return { ok: true, username: u, station: st, persisted };
}

export function disableClubMember(username) {
  ensureInit();
  const u = String(username || '').trim().toLowerCase();
  const m = members.get(u);
  if (!m) return { ok: false, error: '成员不存在' };
  m.disabled = true;
  const persisted = persist();
  return { ok: true, username: u, persisted };
}

export function listClubMembers() {
  ensureInit();
  return [...members.values()]
    .map((m) => ({ username: m.username, station: m.station, disabled: m.disabled, createdAt: m.createdAt, createdBy: m.createdBy }))
    .sort((a, b) => (a.username < b.username ? -1 : 1));
}

// 恒定时间比较：无论用户存不存在都走一遍哈希，防时序侧信道
export function verifyClubMember(username, password) {
  ensureInit();
  const u = String(username || '').trim().toLowerCase();
  const m = members.get(u);
  const hash = m ? m.pwHash : '0'.repeat(128);
  const salt = m ? m.pwSalt : '0'.repeat(32);
  const want = hashPassword(password || '', salt);
  const ok = crypto.timingSafeEqual(Buffer.from(want), Buffer.from(hash));
  if (!m || m.disabled || !ok) return { ok: false };
  return { ok: true, username: m.username, station: m.station };
}

// ── 令牌（复用 viewer 票，claims 带角色与 station）─────────────────────────

export function mintClubToken(username, station) {
  const r = mintViewerTicket(username, CLUB_TOKEN_TTL_MS, { roles: ['broadcast_club'], station });
  if (!r.ok) return r;
  return { ok: true, token: r.ticket, exp: r.exp, expiresInMs: CLUB_TOKEN_TTL_MS, station };
}

export function verifyClubToken(token) {
  const v = verifyViewerTicket(token);
  if (!v.ok) return { ok: false, reason: v.reason };
  const claims = v.claims;
  const roles = Array.isArray(claims?.roles) ? claims.roles : [];
  if (!roles.includes('broadcast_club')) return { ok: false, reason: '令牌不含 broadcast_club 角色' };
  const station = claims?.station || '';
  if (!station) return { ok: false, reason: '令牌缺少 station' };
  return { ok: true, uid: v.uid, station, exp: v.exp };
}

// ── VoiceHub 代理（云端持有写密钥，成员令牌只用于云端鉴权）──────────────────

function vhBase() { return (VOICEHUB_WRITE_BASE || '').replace(/\/+$/, ''); }
function vhKey() { return VOICEHUB_WRITE_KEY || ''; }

export function voicehubConfigured() { return !!vhBase() && !!vhKey(); }

export async function proxyGetSchedules() {
  const base = vhBase();
  if (!base) return { ok: false, code: 0, error: '云端未配置 VoiceHub 写地址（STE_VOICEHUB_WRITE_BASE）' };
  try {
    const r = await fetch(`${base}/api/songs/public`, { headers: { accept: 'application/json' }, signal: AbortSignal.timeout(12000) });
    const text = await r.text();
    if (!r.ok) return { ok: false, code: r.status, error: `VoiceHub 返回 HTTP ${r.status}` };
    return { ok: true, data: text }; // 原样透传，客户端按既有契约解析数组
  } catch (e) {
    return { ok: false, code: 0, error: `拉取排期失败：${e.message}` };
  }
}

export async function proxyReorder(pairs) {
  const base = vhBase();
  const key = vhKey();
  if (!base || !key) return { ok: false, code: 0, error: '云端未配置 VoiceHub 写地址/密钥' };
  const arr = [];
  for (const p of (pairs || [])) {
    if (!p || typeof p !== 'object') continue;
    const id = Number(p.id);
    const seq = Number(p.sequence);
    if (!Number.isFinite(id) || !Number.isFinite(seq)) continue;
    arr.push({ id, sequence: seq });
  }
  if (!arr.length) return { ok: false, code: 400, error: '没有可保存的排期' };
  try {
    const r = await fetch(`${base}/api/open/schedules/reorder`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-api-key': key },
      body: JSON.stringify({ schedules: arr }),
      signal: AbortSignal.timeout(12000),
    });
    const text = await r.text();
    let msg = '';
    try { const j = JSON.parse(text); msg = j && j.message ? j.message : ''; } catch {}
    if (!r.ok) return { ok: false, code: r.status, error: msg || `VoiceHub 返回 HTTP ${r.status}` };
    return { ok: true, data: text };
  } catch (e) {
    return { ok: false, code: 0, error: `保存排期失败：${e.message}` };
  }
}

// ── 审计（写操作落盘，供追溯）───────────────────────────────────────────────

export function auditReorder({ uid, station, clientIp, before, after, ok, error }) {
  try {
    const line = JSON.stringify({
      ts: new Date().toISOString(), uid, station, clientIp: clientIp || null,
      before: before || [], after: after || [], ok: !!ok, error: ok ? null : (error || ''),
    });
    appendFileSync(auditFile, line + '\n', 'utf8');
    return true;
  } catch {
    return false;
  }
}

export function readAudit(limit = 200) {
  try {
    if (!existsSync(auditFile)) return [];
    const lines = readFileSync(auditFile, 'utf8').split('\n').filter(Boolean);
    return lines.slice(-limit)
      .map((l) => { try { return JSON.parse(l); } catch { return null; } })
      .filter(Boolean);
  } catch {
    return [];
  }
}
