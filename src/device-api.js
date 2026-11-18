// 设备侧只读 API 统一入口（2026-11-06 · 用户 m08463 第 3 项）
// ───────────────────────────────────────────────────────────────────────────
// 为什么合并成一个入口，而不是在 index.js 里再挂一条 if：
//   `index.js` 有规模门禁（台账 3562 行，白名单内再长 1 行就 error）。设备侧
//   只读端点还要陆续加（课表、班级清单…），每加一条就往 index.js 塞一行迟早撑爆。
//   所以这里做**一次分派**：index.js 只留一行「是 /api/device/ 开头的 GET 就交给
//   deviceGet()」，之后新增设备端点只改本文件，index.js 行数恒定。
//
// 鉴权口径（与 /api/recordings/upload、/api/device/timetable 完全一致，不另造）：
//   Authorization: Bearer <设备令牌>（静态 DEV_TOKEN 或 agent ticket）
//   · agent ticket 验通 ⇒ uid 必须等于票里签的 uid（不许自报身份）
//   · 静态令牌是**运维/回归**用，没有绑定任何 uid，故允许用 ?uid= 指定（与原有行为一致）
//
// 边界：本文件只读，不写任何 store；结构翻译归被控端 TimetableApply，云端不解释。

import { timingSafeEqual } from 'node:crypto';

import { getTimetable } from './class-data.js';
import { deviceTimetable } from './device-timetable.js';
import { getDeviceClassCode, getDeviceTenant, getDevice, listClasses } from './registry.js';
import { clientProfile } from './device-ledger.js';

export const DEVICE_GET_PREFIX = '/api/device/';
export const PATH_TIMETABLE = '/api/device/timetable';
export const PATH_CLASSES = '/api/device/classes';
export const PATH_SELF = '/api/device/self';

/** 定长比较（长度不同直接 false，不泄露长度）。 */
function teq(a, b) {
  const x = Buffer.from(String(a || ''), 'utf8');
  const y = Buffer.from(String(b || ''), 'utf8');
  if (!x.length || x.length !== y.length) return false;
  try {
    return timingSafeEqual(x, y);
  } catch {
    return false;
  }
}

/** 从 Authorization 头取 Bearer 令牌（大小写不敏感，与既有实现同口径）。 */
export function bearerOf(headers) {
  const at = String((headers && headers.authorization) || '');
  return at.toLowerCase().startsWith('bearer ') ? at.slice(7).trim() : '';
}

/**
 * 鉴权：返回 { ok, uid, via }。
 * via = 'ticket'（票里带的 uid 权威）| 'token'（静态令牌，uid 由调用方从 query 取）
 */
export function authDevice({ headers, devToken, verifyTicket, fp }) {
  const tk = bearerOf(headers);
  if (!tk) return { ok: false, uid: '', via: '' };
  let tv = { ok: false };
  if (typeof verifyTicket === 'function') {
    try {
      tv = verifyTicket(tk, String(fp || '')) || { ok: false };
    } catch {
      tv = { ok: false };
    }
  }
  if (tv && tv.ok && tv.uid) return { ok: true, uid: String(tv.uid), via: 'ticket' };
  if (teq(tk, devToken)) return { ok: true, uid: '', via: 'token' };
  return { ok: false, uid: '', via: '' };
}

/**
 * 班级清单（本租户）。给「点课表组件 → 弹班级列表 → 选一个班看」用。
 *
 * 只回脱敏三件：code / name / onlineCount。**不回 devices 数组**——
 * 设备侧没有查看别班设备清单的需求，多给一份 uid 列表就是多一份泄露面。
 */
export function classesForDevice({ uid, tenantOf, list }) {
  const u = String(uid || '').trim();
  if (!u) return { status: 400, body: { ok: false, error: '缺少设备标识' } };
  const tn = String(tenantOf(u) || '').trim();
  let all = [];
  try {
    all = list(tn) || [];
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读班级列表失败：${e && e.message ? e.message : e}` } };
  }
  return {
    status: 200,
    body: {
      ok: true,
      uid: u,
      tenantId: tn,
      classes: all.map((c) => ({
        code: String(c.code || ''),
        name: String(c.name || ''),
        onlineCount: Number(c.onlineCount) || 0,
      })),
    },
  };
}

/**
 * 选班预览：读**同租户**任意班的课表。
 *
 * 与「读本班」的区别只有一点：classCode 由调用方给。所以必须校验
 * 「这个班号在本租户的班级清单里」——不校验就等于给设备开了一个
 * 按班号枚举全校（乃至别校）课表的口子。
 */
export function timetableOfClass({ uid, classCode, tenantOf, list, read }) {
  const u = String(uid || '').trim();
  if (!u) return { status: 400, body: { ok: false, error: '缺少设备标识' } };
  const code = String(classCode || '').trim();
  if (!code) return { status: 400, body: { ok: false, error: '缺少班级号' } };
  const tn = String(tenantOf(u) || '').trim();

  let hit = null;
  try {
    hit = (list(tn) || []).find((c) => String(c.code || '') === code) || null;
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读班级列表失败：${e && e.message ? e.message : e}` } };
  }
  if (!hit) {
    // 不存在与跨租户**回同一个 code**：区分开就等于告诉调用方
    // 「这个班号在别校是真的存在」，是个不该开的探测面。
    return { status: 403, body: { ok: false, code: 'NO_SUCH_CLASS', error: '本校区没有这个班级' } };
  }

  let r;
  try {
    r = read(code, tn);
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读课表失败：${e && e.message ? e.message : e}` } };
  }
  if (!r || r.ok === false) {
    return { status: 400, body: { ok: false, error: (r && r.error) || '读课表失败' } };
  }
  if (!r.profile) {
    return {
      status: 200,
      body: {
        ok: true, uid: u, classCode: code, className: String(hit.name || ''),
        profile: null, updatedAt: 0, reason: 'NO_TIMETABLE',
      },
    };
  }
  return {
    status: 200,
    body: {
      ok: true, uid: u, classCode: code, className: String(hit.name || ''),
      profile: r.profile, updatedAt: Number(r.updatedAt) || 0,
      updatedBy: String(r.updatedBy || ''), reason: '',
    },
  };
}

/**
 * 设备查自己的客户端档案（2026-11-18 · 被控端主体地位）。
 *
 * 这是「每一台被控端都是一个客户端」在协议层的落点：设备启动后不必等管理端，
 * 自己就能确认「云端认我是谁、我叫什么、我归哪个班/哪所学校、我是第几次接入、
 * 我的补充信息（名称/班级/校区/用途）还缺哪几项」。
 *
 * 返回里的 initialized 四标志是给 OOBE 用的：缺哪项就引导补哪项，全齐了就不再打扰。
 * 没接入过的 uid（查无档案）回 404 + NO_PROFILE —— **不凭空造档案**，
 * 避免设备自报一个 uid 就让云端长出一条记录（那正是被灌爆内存的老路）。
 */
export function selfForDevice({ uid, onlineOf, profileOf }) {
  const u = String(uid || '').trim();
  if (!u) return { status: 400, body: { ok: false, error: '缺少设备标识' } };
  let prof = null;
  try {
    prof = (profileOf || clientProfile)(u, (onlineOf || getDevice)(u));
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读客户端档案失败：${e && e.message ? e.message : e}` } };
  }
  if (!prof) {
    return { status: 404, body: { ok: false, code: 'NO_PROFILE', uid: u, error: '这台设备还没有注册记录' } };
  }
  return { status: 200, body: { ok: true, ...prof } };
}

/**
 * 设备侧 GET 总入口。
 * @returns {{status:number, body:object}|null} null = 不归本模块处理（调用方继续往下走）
 */
export function deviceGet(u, req, deps = {}) {
  const path = String((u && u.pathname) || '');
  if (path !== PATH_TIMETABLE && path !== PATH_CLASSES && path !== PATH_SELF) return null;

  const params = (u && u.searchParams) || new URLSearchParams();
  const a = authDevice({
    headers: (req && req.headers) || {},
    devToken: deps.devToken,
    verifyTicket: deps.verifyTicket,
    fp: params.get('fp') || '',
  });
  if (!a.ok) return { status: 401, body: { ok: false, code: 'TOKEN_INVALID', error: '拉课表需设备令牌' } };

  // 票里带 uid 时以票为准（不许用 query 覆盖）；只有静态令牌才允许 ?uid=
  const uid = a.uid || String(params.get('uid') || '').trim();

  if (path === PATH_SELF) {
    return selfForDevice({ uid, onlineOf: deps.onlineOf, profileOf: deps.profileOf });
  }

  if (path === PATH_CLASSES) {
    return classesForDevice({
      uid,
      tenantOf: deps.tenantOf || getDeviceTenant,
      list: deps.list || listClasses,
    });
  }

  const classCode = String(params.get('classCode') || '').trim();
  if (classCode) {
    return timetableOfClass({
      uid, classCode,
      tenantOf: deps.tenantOf || getDeviceTenant,
      list: deps.list || listClasses,
      read: deps.read || getTimetable,
    });
  }

  // 没给 classCode = 原语义「读自己班」
  return deviceTimetable({
    uid,
    classCodeOf: deps.classCodeOf || getDeviceClassCode,
    tenantOf: deps.tenantOf || getDeviceTenant,
    read: deps.read || getTimetable,
  });
}
