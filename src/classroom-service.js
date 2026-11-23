// 星集控云端 · 班级一站式服务（ClassroomService）· 2026-11-10
//
// 用户裁决：「班级 = 隔离单元」「新建并列服务 + 旧路标记退役」
//            「走现代化一站式服务模式，不要在旧基础上修改，直接重构片段」。
//
// ── 这个文件是什么 ────────────────────────────────────────────────────
// **新地基的唯一入口**：一个班 = 一个 ClassroomUnit，接入/权限/展示全部走这里。
// 旧世界 5 条绑定通道（bind-api.js BIND_CHANNELS）与 14 段班级路由保持原样、
// 只标记 deprecated（见 index.js 的退役守卫），本服务**不在旧实现上打补丁**。
//
// 分层：
//   classroom-unit.js  —— 纯域模型（零 IO、可单测）★ 唯一判据
//   classroom-service.js（本文件）—— 注册表 + 适配 + 一站式 API
//   index.js           —— 只挂一条新路由，其它不动
//
// 适配器边界：本服务**不 import 任何旧 class-*.js**，避免把旧的网状依赖
// 带进新地基。需要旧数据时由装配层注入 adapter（见 setLegacyAdapter）。
import {
  createClassroomUnit, unitFromLegacy, sameUnit,
  canReachUnit, canJoinUnit, unitSnapshot,
} from './classroom-unit.js';
import { randomBytes } from 'node:crypto';

const units = new Map();          // unit.id -> ClassroomUnit
let legacy = null;                // 旧世界只读适配器（装配层注入）
let audit = () => {};             // 审计钩子（默认静默）

/** 装配层注入旧世界适配：{ listClasses(), listDevices(), now() }。 */
export function setLegacyAdapter(a) { legacy = a || null; }
export function setAudit(fn) { audit = typeof fn === 'function' ? fn : () => {}; }

function now() { return legacy?.now?.() ?? Date.now(); }

// ── 单元注册表 ────────────────────────────────────────────────────────
export function upsertUnit(input) {
  const u = createClassroomUnit(input);
  units.set(u.id, u);
  return u;
}
export function getUnit(tenantId, classCode) {
  return units.get(`${String(tenantId || 'tn_default')}:${String(classCode || '')}`) || null;
}
export function listUnits() { return [...units.values()]; }
export function unitCount() { return units.size; }
export function clearUnits() { units.clear(); }

/**
 * 从旧世界灌入（迁移用，不改旧代码）：把 class-dir 的班 + knownDevices 的设备
 * 归一成隔离单元。幂等，可反复调用。
 */
export function hydrateFromLegacy() {
  if (!legacy?.listClasses) return { imported: 0, reason: '未注入旧适配器' };
  const rows = legacy.listClasses() || [];
  for (const r of rows) {
    try { upsertUnit(unitFromLegacy(r, r.tenantId)); }
    catch { /* 脏行跳过，不让一条坏数据炸掉整轮灌入 */ }
  }
  return { imported: units.size, rows: rows.length };
}

// ── 接入码：唯一签发/校验入口 ──────────────────────────────────────────
const JOIN_CODE_LEN = 6;
const JOIN_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';  // 去掉易混 I/O/0/1

function genCode() {
  const b = randomBytes(JOIN_CODE_LEN);
  let s = '';
  for (let i = 0; i < JOIN_CODE_LEN; i++) s += JOIN_ALPHABET[b[i] % JOIN_ALPHABET.length];
  return s;
}

/** 发码：覆盖旧 5 条通道里「按班发码」的语义，但只有一个实现。 */
export function issueJoinCode(tenantId, classCode, { ttlMs = 24 * 3600 * 1000 } = {}) {
  const old = getUnit(tenantId, classCode);
  if (!old) return { ok: false, error: '班级单元不存在' };
  const u = createClassroomUnit({ ...old, joinCode: genCode(),
                                  joinCodeIssuedAtMs: now(), joinCodeTtlMs: ttlMs });
  units.set(u.id, u);
  audit('info', '班级接入码已签发', { unitId: u.id });
  return { ok: true, code: u.joinCode, expiresAtMs: u.joinCodeIssuedAtMs + ttlMs };
}

/**
 * ★ 单班接入：设备拿码接入，**唯一入口**。
 * 旧世界由 /api/device/activate、/api/bind/confirm、class-scan、qr 各自判定；
 * 新地基只调 canJoinUnit()，调用方不得再自己判一次。
 */
export function joinUnit({ tenantId, classCode, code, uid, ident } = {}) {
  const u = getUnit(tenantId, classCode);
  const j = canJoinUnit(u, code, now());
  if (!j.ok) {
    audit('warn', '单班接入被拒', { classCode, uid, reason: j.reason });
    return { ok: false, error: j.reason, ecode: 'JOIN_DENIED' };
  }
  // 接入动作本身由装配层执行（绑定设备归属），这里只做授权与回执。
  audit('info', '单班接入成功', { unitId: u.id, uid });
  return {
    ok: true,
    unit: { id: u.id, classCode: u.classCode, className: u.className, tenantId: u.tenantId },
    remainingMs: j.remainingMs,
  };
}

// ── 一站式快照：接入/展示/权限共用同一份派生 ──────────────────────────
/**
 * 一个请求拿到一个班的**全部**需要展示的东西（设备、成员数、课表时间、待审数）。
 * 旧世界要打 4~5 个端点分别取；新地基一次给全 ⇒ 天然不需要节流/去抖。
 */
export function snapshot(tenantId, classCode, ident = {}) {
  const u = getUnit(tenantId, classCode);
  const gate = canReachUnit(ident, u, now());
  if (!gate.ok) return { ok: false, error: gate.reason, ecode: 'NO_UNIT_PERMISSION' };
  const devices = legacy?.listDevices?.() || [];
  return {
    ok: true,
    scope: gate.scope,
    ...unitSnapshot(u, now(), {
      devices,
      memberCount: legacy?.memberCountOf?.(u) ?? 0,
      timetableUpdatedAt: legacy?.timetableUpdatedAtOf?.(u) ?? 0,
      pendingCount: legacy?.pendingCountOf?.(u) ?? 0,
    }),
  };
}

/** 路由入口（纯函数，便于 index.js 一行挂载 + 离屏单测）。 */
export function handleClassroomRequest(method, path, body = {}, ident = {}) {
  const m = String(method || '').toUpperCase();
  const parts = String(path || '').split('/').filter(Boolean);   // /api/classroom/:tenant/:code/:action
  if (parts[0] !== 'api' || parts[1] !== 'classroom')
    return { handled: false };
  // 全局动作 /api/classroom/units 不占 :tenant/:code 位 —— 否则 "units" 会被当成班码，
  // 走进「班级单元不存在」。这属于路由形状，必须先于路径参数解析判定。
  if (parts.length === 3 && parts[2] === 'units' && m === 'GET')
    return { handled: true, status: 200, body: { ok: true, units: listUnits() } };
  const tenantId = parts[2] || ident.tenantId || 'tn_default';
  const classCode = parts[3] || '';
  const action = parts[4] || 'snapshot';

  if (m === 'GET' && action === 'snapshot')
    return { handled: true, status: 200, body: snapshot(tenantId, classCode, ident) };
  if (m === 'POST' && action === 'join')
    return { handled: true, status: 200,
             body: joinUnit({ tenantId, classCode, code: body.code, uid: body.uid, ident }) };
  if (m === 'POST' && action === 'join-code') {
    if (canReachUnit(ident, getUnit(tenantId, classCode), now()).scope !== 'tenant'
        && ident.role !== 'classOwner')
      return { handled: true, status: 403, body: { ok: false, error: '无权签发接入码' } };
    const r = issueJoinCode(tenantId, classCode, { ttlMs: body.ttlMs });
    return { handled: true, status: r.ok ? 200 : 404, body: r };
  }
  return { handled: true, status: 404, body: { ok: false, error: '未知的班级服务动作' } };
}

export const CLASSROOM_SERVICE_VERSION = 1;
