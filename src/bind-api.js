// 班级绑定的**唯一核心**（2026-11-05 · 用户点单「绑定逻辑多路接口统一成一个核心 API」）
//
// ── 为什么有这个文件 ──────────────────────────────────────────────────
// 在此之前"把一台设备绑进一个班"有**四条**彼此独立的实现：
//   ① POST /api/device/bind-class   （index.js 内联，**没有** canManageMembers、
//      **没有**重认证门 ⇒ 任何持 viewer 令牌/票据者能把任意设备塞进任意班，跨租户也拦不住）
//   ② POST /api/class/bind          （class-admin-classes.adminBindDevice，权限齐全）
//   ③ POST /api/device/activate     （设备输 6 位班级码，凭票不凭身份）
//   ④ POST /api/bind/confirm        （手机扫码选班，凭设备自助票）
// 四条各自解析班级码、各自解析租户、各自决定要不要 notifyClassBoundIfOnline。
// 口径一旦不同就是权限绕道（①就是活证据），而 review 时四段代码长得差不多，看不出来。
//
// 现在四条都收敛到本文件的 bindDeviceCore()：**一个入口、一套权限、一套返回结构**。
// 区别只剩「设备身份从哪来」（请求体 / 票）与「要不要按班权限」（后台路要，凭票路不要——
// 票本身就是发码人按班权限签出来的凭据）。
//
// ── 纪律 ──────────────────────────────────────────────────────────────
//   · 租户只从票 / 已鉴权 ident 来，**绝不读请求体的 tenantId**（否则客户端能跨校）；
//   · 有身份就必须过 canManageMembers；没身份又要按班权限的渠道一律拒（不许"没身份=放行"）；
//   · 一切失败都带 code + 人话 error，不返回 ok:true 的假成功；
//   · 本文件**不碰 HTTP 帧**（除了两个薄 handler 读 body），业务判定全部可直接单测。

import { getClass, listClassDir, resolveClassCode } from './class-dir.js';
import { bindDeviceToClass, notifyClassBoundIfOnline } from './registry.js';
import { canManageMembers } from './class-admin-shared.js';
import { getTicket, makeQrMatrix, markBound } from './bind-qr.js';
import { DEFAULT_TENANT, normalizeTenantId } from './tenant.js';

/** 渠道：后台直接绑 / 设备输码 / 扫码选设备（管理员扫设备码）/ 设备自助扫码选班 / 班级码扫码选设备。 */
export const BIND_CHANNELS = Object.freeze(['admin', 'code', 'qr', 'device', 'class-scan']);

/** 只有这些渠道凭票即可（票 = 发码人按班权限签出的凭据），其余必须有身份。 */
const TICKET_ONLY_CHANNELS = new Set(['code', 'device', 'class-scan']);

function normName(v) {
  return String(v ?? '').trim().toLowerCase().replace(/\s+/g, ' ');
}

function fail(code, error) {
  return { ok: false, code, error };
}

/**
 * 无副作用地看一眼"这次要绑的是哪个班"。
 *
 * 为什么不能直接用 resolveClassCode 探：resolveClassCode 遇到新名字会**建班**。
 * 先建班再判权 ⇒ 一个没权限的老师只要试一次就能往班级目录里塞垃圾班
 * （多租户下叫"跨租户污染"）。所以判权前只用只读查询。
 * @returns {{code:string, name:string}|null}
 */
function peekClass(tn, code, name) {
  const c = String(code ?? '').trim();
  if (c) {
    const hit = getClass(c, tn);
    if (hit) return { code: hit.code, name: hit.name || '' };
  }
  const n = normName(name);
  if (!n) return null;
  const hit = listClassDir(tn).find((x) => normName(x.name) === n);
  return hit ? { code: hit.code, name: hit.name || '' } : null;
}

/**
 * 绑一台设备进一个班 —— 三条渠道共用的**唯一实现**。
 *
 * @param {object} o
 * @param {'admin'|'code'|'qr'|'device'} [o.channel] 渠道（只影响判权方式与回执字段）
 * @param {string} [o.uid] 设备 uid（输码/自助路可缺，缺则取票里的）
 * @param {string} [o.classCode] 班级码
 * @param {string} [o.className] 班级显示名（只给名字也允许，走 class-dir 归一）
 * @param {object|null} [o.ident] identityFromAuth() 的结果（后台/扫码路必有）
 * @param {object|null} [o.ticket] getTicket() 的票（输码/自助路必有）
 * @param {string|null} [o.tenantId] **只给服务端已知场景**（票里钉死的租户），不是给 HTTP 传的
 * @param {string} [o.source] 落 class-dir 的来源标记（admin/device/cloud）
 * @param {boolean} [o.requireManage] 是否强制按班权限（缺省 true）
 * @returns {{ok:boolean, code?:string, error?:string, channel?:string, uid?:string,
 *            classCode?:string, className?:string, tenantId?:string, merged?:boolean,
 *            onlineNotified?:boolean, ticketCode?:string, idempotent?:boolean}}
 */
export function bindDeviceCore({
  channel = 'admin', uid = '', classCode = '', className = '',
  ident = null, ticket = null, tenantId = null, source = 'cloud', requireManage = true,
} = {}) {
  const ch = String(channel || 'admin');
  const t = ticket || null;

  // ① 设备身份：后台/扫码路来自请求，输码/自助路来自票。两边都拿不到就是缺 uid。
  let u = String(uid ?? '').trim();
  if (!u && t && t.uid) u = String(t.uid).trim();
  if (!u) return fail('NO_UID', '缺 uid');

  // ② 票的一次性：已绑票只对**同一台设备**幂等返回（重输不报错，换设备必须拒）。
  if (t) {
    if (t.uid && String(t.uid) !== u) return fail('TICKET_OTHER_DEVICE', '这个绑定码属于另一台设备');
    if (t.state === 'bound') {
      return {
        ok: true, idempotent: true, channel: ch, uid: u,
        classCode: t.classCode || '', className: t.className || '',
        tenantId: normalizeTenantId(t.tenantId ?? tenantId), ticketCode: t.code || '',
      };
    }
  }

  // ③ 目标班级：票里钉死的优先（输码/自助路的班级来自发码时，不是来自请求体）。
  let code = String(classCode ?? '').trim();
  let name = String(className ?? '').trim();
  if (t && t.classCode) {
    code = String(t.classCode);
    if (!name) name = String(t.className || '');
  }
  if (!code && !name) return fail('NO_CLASS', '缺班级（班级码或班级名至少给一个）');

  // ④ 租户：票 > 服务端显式 > ident。**绝不读请求体**。
  const tn = normalizeTenantId(
    (t && t.tenantId) || tenantId || (ident && ident.tenantId) || DEFAULT_TENANT,
  );

  // ⑤ 判权（在解析之前：解析会建班，先判权才不会给没权限的人造班）。
  //    · 凭票渠道（code / device）：票本身就是发码人按班权限签出的凭据，不再要身份；
  //    · 其余渠道：必须有身份，且 must 过 canManageMembers（跨租户在 canAccess 里已被拒）。
  if (requireManage && !TICKET_ONLY_CHANNELS.has(ch)) {
    if (!ident) return fail('FORBIDDEN', '未授权：绑定设备需要管理端身份');
    const peek = peekClass(tn, code, name);
    const probe = peek ? peek.code : code;
    if (!canManageMembers(ident, probe)) return fail('FORBIDDEN', '无权限管理该班');
  }
  if (TICKET_ONLY_CHANNELS.has(ch) && !t) return fail('NO_TICKET', '该渠道需要绑定码');

  // ⑥ 归一 + 落盘（class-dir 是班级身份唯一真源，canonical 码永不变）。
  const resolved = resolveClassCode({ code, name, source, tenantId: tn });
  if (!resolved.ok) {
    return fail('BAD_CLASS', '班级码格式不合法（需 6-16 位字母数字或 -_）');
  }
  // 解析可能把本次的码并入某个已登记班（alias）⇒ 换成 canonical 后再判一次权，
  // 否则"先用别名绕过判权、再被归一进别班"就是一条绕道路径。
  if (requireManage && !TICKET_ONLY_CHANNELS.has(ch) && !canManageMembers(ident, resolved.code)) {
    return fail('FORBIDDEN', '无权限管理该班');
  }

  const r = bindDeviceToClass(resolved.code, u, name, source, tn);
  if (!r.ok) return fail('BIND_FAILED', r.error || '绑定失败');
  // G4（2026-10-24）：在线即下发 class-bound，被控端据此更新 classcode.txt，
  // 缩略图上报随即开工（它要求 classCode 非空）。离线设备等下次 register。
  const online = notifyClassBoundIfOnline(u, r.classCode, r.class.name);
  const finalName = r.class.name || name || r.classCode;
  if (t && t.code) markBound(t.code, { classCode: r.classCode, className: finalName, uid: u });

  return {
    ok: true, channel: ch, uid: u,
    classCode: r.classCode, className: finalName, tenantId: tn,
    merged: !!r.merged, onlineNotified: !!(online && online.online),
    ticketCode: (t && t.code) || '',
  };
}

/**
 * 统一回执：把核心结果翻成 {status, body}（三种渠道回**同一套字段**，前端只写一套解析）。
 * 旧字段一个不少（ok/classCode/className + class），新字段只增不减。
 */
export function bindReply(core, { ticket = null } = {}) {
  if (!core || !core.ok) {
    const c = (core && core.code) || 'BIND_FAILED';
    return {
      status: c === 'FORBIDDEN' ? 403 : 400,
      body: { ok: false, code: c, error: (core && core.error) || '绑定失败', channel: core && core.channel },
    };
  }
  const body = {
    ok: true,
    channel: core.channel,
    uid: core.uid,
    classCode: core.classCode,
    // className 与旧路一致（旧路回 class.name，前端按 className 显示）
    className: core.className,
    name: core.className,
    tenantId: core.tenantId,
    merged: !!core.merged,
    onlineNotified: !!core.onlineNotified,
  };
  if (core.ticketCode) body.code = core.ticketCode;
  if (core.idempotent) body.idempotent = true;
  if (ticket) {
    body.expiresAt = ticket.expireAt;
    body.ttlSec = Math.max(0, Math.round((ticket.expireAt - Date.now()) / 1000));
  }
  return { status: 200, body };
}

/** 扫码地址：优先请求自身的 Host（反代会带 x-forwarded-*），必要时用 STE_BIND_PUBLIC_ORIGIN。 */
export function bindOriginOf(req) {
  const explicit = String(process.env.STE_BIND_PUBLIC_ORIGIN || '').trim();
  if (explicit) return explicit.replace(/\/+$/, '');
  if (!req || !req.headers) return '';
  const host = String(req.headers['x-forwarded-host'] || req.headers.host || '').trim();
  if (!host) return '';
  const proto = String(req.headers['x-forwarded-proto'] || '').split(',')[0].trim() || 'http';
  return `${proto}://${host}`;
}

/**
 * 班级接入码的二维码（用户点单：班级码要能扫）。
 *
 * 旧实现明确不回 matrix（class-admin-classes.js 注释：码是给设备"输"的）。现在手机扫
 * 也能落到同一个 /bind 页（页面对 kind=class 只显示 6 位码，让老师回大屏敲），
 * 成本是一次纯计算，收益是"贴一张纸就能同时支持扫码与口念"。
 * **矩阵生成失败不影响发码成功**（码本身已发出去）——只如实带回 qrError。
 */
export function bindQrOfTicket(ticket, origin, req = null) {
  const base = origin || bindOriginOf(req);
  const scanUrl = base ? `${base}/bind?c=${ticket.code}` : '';
  try {
    const qr = makeQrMatrix(scanUrl || ticket.code);
    return { matrix: qr.rows, size: qr.size, version: qr.version, scanUrl };
  } catch (e) {
    return { matrix: null, size: 0, version: 0, scanUrl, qrError: String((e && e.message) || e) };
  }
}

/* ───── 班级码扫码选设备（2026-11-05 · 用户 m01202 裁决）─────────────────
 * 管理员按班发码 → 出二维码 → 手机扫 → 选一台未分班设备 → 确认绑定。
 * 与"设备自助扫码选班"（kind=device）互为镜像：那边票里带设备、手机选班；
 * 这边票里带班级、手机选设备。两边都只认云端权威记录，绝不读请求体里的
 * classCode / tenantId / 设备归属 —— 否则匿名手机页就是一条跨租户绕道。
 * 下面三个函数都不碰 HTTP 帧，可直接单测（tests/regression-bind-unify.mjs）。
 */

/** /api/bind/info 用：同租户下还没分班的已知设备清单（脱敏：只回 uid + 显示名 + 在线态）。 */
export function classScanCandidates(devices, ticket) {
  if (!ticket || ticket.kind !== 'class' || ticket.state === 'bound') return [];
  const tn = normalizeTenantId(ticket.tenantId);
  return (devices || [])
    .filter((d) => d && d.uid && !d.classCode && normalizeTenantId(d.tenantId) === tn)
    .map((d) => ({ uid: d.uid, name: d.name || d.uid, online: !!d.online }));
}

/**
 * 纯判定：手机挑的这台设备能不能用这张班级码绑。
 * device 必须来自服务端 listDevices() 的权威记录，不能是请求体拼出来的对象。
 * @returns {{ok:boolean, code?:string, error?:string}}
 */
export function classScanDecide(ticket, uid, device, now = Date.now()) {
  if (!ticket || ticket.kind !== 'class') return fail('BAD_TICKET', '这个绑定码不是班级接入码');
  if (ticket.state === 'bound') return fail('TICKET_USED', '这个绑定码已经用过了，请重新生成');
  if (ticket.expireAt && now > ticket.expireAt) return fail('TICKET_EXPIRED', '绑定码已过期，请回到大屏刷新');
  const u = String(uid ?? '').trim();
  if (!u) return fail('NO_UID', '请先选择一台设备');
  if (!device || String(device.uid) !== u) return fail('DEVICE_UNKNOWN', '找不到这台设备，请刷新页面重新选择');
  if (device.classCode) return fail('DEVICE_BOUND', '这台设备已经属于别的班了');
  if (normalizeTenantId(device.tenantId) !== normalizeTenantId(ticket.tenantId)) {
    return fail('CROSS_TENANT', '这台设备不在本班所属的学校里');
  }
  return { ok: true };
}

/** 校验 + 落盘一次做完：入口层只负责取权威设备清单与回执，不重复任何判据。 */
export function classScanBind(ticket, uid, devices) {
  const u = String(uid ?? '').trim();
  const device = (devices || []).find((d) => d && String(d.uid) === u);
  const gate = classScanDecide(ticket, u, device);
  if (!gate.ok) return gate;
  return bindDeviceCore({
    channel: 'class-scan', uid: u, ticket,
    tenantId: normalizeTenantId(ticket.tenantId), source: 'class-scan', requireManage: false,
  });
}

/**
 * HTTP：POST /api/device/rename 一类"设备身份"动作共用的小工具（body 读取）。
 * 与 class-admin-shared.readJsonBody 同款上限，但**不引入它的依赖**（避免循环 import）。
 */
export function readBody(req, cb, maxBytes = 1e4) {
  let raw = '';
  req.on('data', (c) => { raw += c; if (raw.length > maxBytes) req.destroy(); });
  req.on('end', () => {
    let body;
    try { body = JSON.parse(raw || '{}'); } catch { return cb(null); }
    cb(body);
  });
}

// `getTicket` 在这里再导出：三个渠道的 HTTP 薄壳都要用它，少一处 import 就少一处漂移。
export { getTicket };
