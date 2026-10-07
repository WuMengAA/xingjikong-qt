// 管理端（viewer）接入票据：给浏览器用的一次性短票。
//
// 为什么不是直接把令牌发到浏览器：
//   浏览器里放长期令牌 = 令牌公开（F12 就能抄走），等于没鉴权。
//   正解：站点（已登录、已过 viewConsole 权限）在服务端签一张 2 分钟有效的票，
//   浏览器拿票去连云端，云端用同一个密钥验签。票只在握手时用一次，
//   过期或被抄走也只能在很短的窗口内用。
//
// 票格式：v1.<expMs>.<uid(base64url)>.<nonce>.<hmac(base64url)>
//   签名内容 = "v1.<expMs>.<uid>.<nonce>"，HMAC-SHA256(secret)
//
// 纪律：密钥没配不许假装通过（那才是真漏洞），也不许因为没配就让整条通道死掉——
//       票据通道关闭时长期令牌通道仍可用（见 index.js 的 authorizeViewer）。

import crypto from 'node:crypto';
import { VIEWER_SECRET, VIEWER_TICKET_TTL_MS } from './config.js';

const b64u = (b) => Buffer.from(b).toString('base64url');

/**
 * 签一张管理端票据。云端自己用不上（签发方是站点），留在这里是为了
 * ① 格式只有一处定义，两边不会写歪；② 自测能验证"签得出来也验得过"。
 * @param {string} uid 签发对象（站点侧登录用户名，用于审计）
 * @param {number} ttlMs 有效期，缺省取 CLOUD_VIEWER_TICKET_TTL_MS
 */
export function mintViewerTicket(uid, ttlMs = VIEWER_TICKET_TTL_MS) {
  if (!VIEWER_SECRET) return { ok: false, reason: '未配置 CLOUD_VIEWER_SECRET，无法签票' };
  const exp = Date.now() + Number(ttlMs || VIEWER_TICKET_TTL_MS);
  const nonce = crypto.randomBytes(8).toString('hex');
  const payload = `v1.${exp}.${b64u(Buffer.from(String(uid || ''), 'utf8'))}.${nonce}`;
  const sig = b64u(crypto.createHmac('sha256', VIEWER_SECRET).update(payload).digest());
  return { ok: true, ticket: `${payload}.${sig}`, exp };
}

/**
 * 验一张管理端票据。
 * @returns {{ok:boolean, mode?:'ticket', uid?:string, exp?:number, reason?:string}}
 */
export function verifyViewerTicket(ticket) {
  if (!VIEWER_SECRET) return { ok: false, reason: '云端未配置 CLOUD_VIEWER_SECRET，票据通道关闭' };
  const parts = String(ticket || '').split('.');
  if (parts.length !== 5 || parts[0] !== 'v1') return { ok: false, reason: '票据格式不对（应为 v1.exp.uid.nonce.sig）' };

  const [, expRaw, uidB64, nonce, sig] = parts;
  const payload = `${parts[0]}.${expRaw}.${uidB64}.${nonce}`;
  const expect = Buffer.from(b64u(crypto.createHmac('sha256', VIEWER_SECRET).update(payload).digest()));
  const got = Buffer.from(String(sig || ''));
  // timingSafeEqual 长度不等会直接抛，先比长度
  if (expect.length !== got.length || !crypto.timingSafeEqual(expect, got)) {
    return { ok: false, reason: '票据签名不对（不是本站签发的，或密钥不一致）' };
  }

  const exp = Number(expRaw);
  if (!Number.isFinite(exp)) return { ok: false, reason: '票据有效期字段不可解析' };
  if (Date.now() > exp) return { ok: false, reason: '票据已过期' };

  let uid = '';
  try { uid = Buffer.from(uidB64, 'base64url').toString('utf8'); } catch { uid = ''; }
  return { ok: true, mode: 'ticket', uid, exp };
}

/**
 * 验一张「设备接入票」（被控端连云端时用，2026-10-06 设备绑定通道）。
 *
 * 格式：v1.<expMs>.<devUidB64>.<fpB64>.<nonce>.<ownerB64>.<sig>（7 段）。
 * 与 viewer 票（5 段）同密钥（CLOUD_VIEWER_SECRET）、同 HMAC-SHA256，
 * 但段数不同 ⇒ 云端按段数区分两种票，互不混用。签名内容 = 前 6 段用 `.` 连接。
 * 签发方是站点 /api/device/bind（登录态下给名下设备签票），此处只验不签。
 *
 * fp（设备指纹）对照是「绑定即授权」不退化成「票可以到处贴」的唯一防线，因此
 * **强制**：票里签了 fp ⇒ 被控端注册时必须声明同一 fp，否则拒绝。
 * 两种漏法都被堵：① 票拷到别的机器（fp 对不上）② 拿到别人的票但不报 fp（直接拒）。
 * 票里没有 fp（早期/无指纹签发）时不做对照，保留兼容。
 *
 * @param {string} ticket 设备票原文
 * @param {string} fp 被控端注册时声明的设备指纹（对照票里的 fp）
 * @returns {{ok:boolean, mode?:'agent-ticket', uid?:string, owner?:string, exp?:number, reason?:string}}
 */
export function verifyAgentTicket(ticket, fp) {
  if (!VIEWER_SECRET) return { ok: false, reason: '云端未配置 CLOUD_VIEWER_SECRET，设备票通道关闭' };
  const parts = String(ticket || '').split('.');
  if (parts.length !== 7 || parts[0] !== 'v1') {
    return { ok: false, reason: '设备票格式不对（应为 v1.exp.uid.fp.nonce.owner.sig）' };
  }
  const [ver, expRaw, uidB64, fpB64, nonce, ownerB64, sig] = parts;
  const payload = [ver, expRaw, uidB64, fpB64, nonce, ownerB64].join('.');
  const expect = Buffer.from(b64u(crypto.createHmac('sha256', VIEWER_SECRET).update(payload).digest()));
  const got = Buffer.from(String(sig || ''));
  // timingSafeEqual 长度不等会直接抛，先比长度
  if (expect.length !== got.length || !crypto.timingSafeEqual(expect, got)) {
    return { ok: false, reason: '设备票签名不对（不是本站签发的，或密钥不一致）' };
  }
  const exp = Number(expRaw);
  if (!Number.isFinite(exp)) return { ok: false, reason: '设备票有效期字段不可解析' };
  if (Date.now() > exp) return { ok: false, reason: '设备票已过期' };

  let uid = '', owner = '', ticketFp = '';
  try { uid = Buffer.from(uidB64, 'base64url').toString('utf8'); } catch { uid = ''; }
  try { owner = Buffer.from(ownerB64, 'base64url').toString('utf8'); } catch { owner = ''; }
  try { ticketFp = Buffer.from(fpB64, 'base64url').toString('utf8'); } catch { ticketFp = ''; }

  // fp 对照（强制）：票里签了 fp，被控端就必须声明同一 fp。
  // 被控端不报 fp 时也拒——否则「拿到别人的票但不报 fp」就能绕过绑定。
  // 票里本来没有 fp（早期/无指纹签发）才跳过对照，保留兼容。
  if (ticketFp) {
    if (!fp) {
      return { ok: false, reason: '设备票绑定了设备指纹，注册时必须上报指纹' };
    }
    if (ticketFp.toLowerCase() !== String(fp).toLowerCase()) {
      return { ok: false, reason: '设备票与设备指纹不匹配' };
    }
  }
  return { ok: true, mode: 'agent-ticket', uid, owner, exp };
}

/**
 * 签一张设备接入票（**仅自测用**）。生产签发方是站点 /api/device/bind，
 * 云端不签发；留在这里只为 ① 票格式只有一处定义，测试不会写歪；
 * ② 自测能验证「签得出来也验得过」的往返闭环。
 *
 * @param {string} devUid 设备 uid
 * @param {string} fp 设备指纹（8-64 位小写字母数字_-）
 * @param {string} owner 绑定者用户名（进审计）
 * @param {number} ttlMs 有效期，缺省 30 天（与站点 signDeviceTicket 一致）
 */
export function mintAgentTicket(devUid, fp, owner, ttlMs = 30 * 24 * 60 * 60 * 1000) {
  if (!VIEWER_SECRET) return { ok: false, reason: '未配置 CLOUD_VIEWER_SECRET，无法签票' };
  const exp = Date.now() + Number(ttlMs);
  const nonce = crypto.randomBytes(8).toString('hex');
  const payload = `v1.${exp}.${b64u(Buffer.from(String(devUid || ''), 'utf8'))}.`
    + `${b64u(Buffer.from(String(fp || ''), 'utf8'))}.${nonce}.`
    + `${b64u(Buffer.from(String(owner || ''), 'utf8'))}`;
  const sig = b64u(crypto.createHmac('sha256', VIEWER_SECRET).update(payload).digest());
  return { ok: true, ticket: `${payload}.${sig}`, exp };
}
