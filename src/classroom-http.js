// 班级一站式服务的云端装配（2026-11-10）
//
// 用户裁决「新建并列服务 + 旧路标记退役」：这里只做**装配**，
// 业务判据全在 classroom-unit.js，注册表全在 classroom-service.js。
// index.js 只需两行：import + 一行分发（不改任何旧实现）。
import { listClasses, listDevices } from './registry.js';
import {
  setLegacyAdapter, setAudit, hydrateFromLegacy, handleClassroomRequest,
} from './classroom-service.js';

let inited = false;
let push = () => {};

export function initClassroomService({ pushEvent } = {}) {
  if (typeof pushEvent === 'function') push = pushEvent;
  // 只读适配：新地基不写回旧世界，也不改旧模块一行代码。
  setLegacyAdapter({
    now: () => Date.now(),
    listClasses: () => {
      try { return listClasses() || []; }
      catch (e) { push('warn', '班级目录适配失败', { error: e.message }); return []; }
    },
    listDevices: () => {
      try { return listDevices() || []; }
      catch (e) { push('warn', '设备表适配失败', { error: e.message }); return []; }
    },
    // 下面三个暂不接旧模块（属 I2 范围）：给 0 是如实值，不是伪造。
    memberCountOf: () => 0,
    timetableUpdatedAtOf: () => 0,
    pendingCountOf: () => 0,
  });
  setAudit((level, msg, extra) => push(level, `[classroom] ${msg}`, extra));
  const r = hydrateFromLegacy();
  inited = true;
  push('info', '班级一站式服务已就绪', { units: r.imported });
  return r;
}

/**
 * index.js 一行分发入口。
 * ident 由调用方从 req.__auth 归一后传入（沿用既有 identityFromAuth 口径）。
 * 返回 { handled, status, body }；handled=false 表示不是本服务的路径。
 */
export function handleClassroomHttp(req, res, u, send, ident) {
  const path = u.pathname;
  if (!path.startsWith('/api/classroom/')) return false;
  if (!inited) initClassroomService();

  const done = (r) => { send(r.status, r.body); return true; };
  if (req.method === 'GET') {
    return done(handleClassroomRequest('GET', path, {}, ident));
  }
  if (req.method === 'POST') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
    req.on('end', () => {
      let p = {};
      try { p = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
      const r = handleClassroomRequest('POST', path, p, ident);
      send(r.status, r.body);
      return undefined;
    });
    return true;
  }
  send(405, { ok: false, error: '班级服务仅支持 GET/POST' });
  return true;
}

/** 旧接入通道退役标记（只标记，不拦截、不删除）。 */
export const LEGACY_JOIN_ROUTES = Object.freeze([
  '/api/device/bind-class',
  '/api/device/activate',
  '/api/class/bindcode',
  '/api/bind/confirm',
  '/api/device/bind-qr',
]);
