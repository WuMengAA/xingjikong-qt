// 云端管理台 /admin 的「用网站账号登录」——给浏览器用的 OAuth 回跳出口。
//
// 为什么要单独一个 127.0.0.1 上的临时端口，而不是让后台页面直接跳：
//   站点的 OAuth 客户端 xingjikong_native 是 loopback=true，站点只放行
//   `http://127.0.0.1:<任意端口>/oauth-callback`（站点 src/lib/server/oauth-clients.ts:257）。
//   后台页面自己 origin 是 http://<云端>:8788，不是 127.0.0.1，站点的回跳校验会硬拒。
//   所以在同一台云端的进程里开一个只绑回环的临时端口来接 code，绕开这一条。
//   端口用 0 让系统分配，不跟本机别的软件抢。
//
// 谁在用什么方式拿票：
//   浏览器 → 站点 /oauth/authorize（用户认可）→ 回跳本文件的 127.0.0.1:<port>/oauth-callback
//   → 这里拿 code 换网站会话（站点 /oauth/token）→ 用同一个 CLOUD_VIEWER_SECRET 签一张
//   管理台票据（ticket.js 的 mintViewerTicket，和桌面端一户通同一格式同一密钥）
//   → 通过 postMessage 交回 opener 页面（admin.html）。
//
// 纪律：
//   - state 一次性、5 分钟过期，别拿它当会话标识用；
//   - 这个 server 只服务那一条回跳路径，别的东西一律 404，不回显任何参数；
//   - 日志只记"有没有值 / 谁（uid）/ 失败原因"，绝不把密钥、会话、票本身写进日志。

import http from 'node:http';
import crypto from 'node:crypto';
import { mintViewerTicket } from './ticket.js';
import {
  SITE_URL, OAUTH_CLIENT_ID, OAUTH_CLIENT_SECRET, OAUTH_REDIRECT_PATH,
  ADMIN_TICKET_TTL_MS, ADMIN_TICKET_TTL_MAX_MS, VIEWER_SECRET,
} from './config.js';

/** state 存活时间：够用户从点登录到完成授权，又短到不值得留着当攻击面。 */
const STATE_TTL_MS = 5 * 60 * 1000;

const states = new Map();

function newState() {
  const state = crypto.randomBytes(16).toString('hex');
  states.set(state, Date.now());
  return state;
}

/** 顺手把过期的 state 清掉，免得 Map 只长不短。 */
function pruneStates() {
  const now = Date.now();
  for (const [k, t] of states) if (now - t > STATE_TTL_MS) states.delete(k);
}

/**
 * 造一张授权页地址。redirect_uri 必须与等会儿换 token 时带的完全一致，
 * 站点 /oauth/token 会拿它跟授权时存的比对，差一个字符就 invalid_grant。
 */
export function buildAuthorize(redirectUri) {
  const state = newState();
  pruneStates();
  const u = new URL('/oauth/authorize', SITE_URL);
  u.searchParams.set('client_id', OAUTH_CLIENT_ID);
  u.searchParams.set('redirect_uri', redirectUri);
  u.searchParams.set('response_type', 'code');
  u.searchParams.set('state', state);
  u.searchParams.set('scope', 'cims profile');
  return { authorizeUrl: u.toString(), state };
}

/** 拿到 code 之后调站点换会话。失败要如实冒泡，别吞成"签票成功"。 */
async function exchangeCode(code, redirectUri) {
  const body = new URLSearchParams();
  body.set('grant_type', 'authorization_code');
  body.set('client_id', OAUTH_CLIENT_ID);
  body.set('client_secret', OAUTH_CLIENT_SECRET);
  body.set('code', code);
  body.set('redirect_uri', redirectUri);
  body.set('expires_in', String(Math.round(ADMIN_TICKET_TTL_MS / 1000)));

  const r = await fetch(new URL('/oauth/token', SITE_URL), {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: body.toString(),
    signal: AbortSignal.timeout(10000),
  });
  if (!r.ok) {
    // 真实原因（密钥不匹配/码已用/回跳不一致）只存在于站点侧，我们这边如实带回来给用户看。
    throw new Error(`站点换令牌失败（HTTP ${r.status}），原因见站点 [oauth/token] 日志`);
  }
  const j = await r.json();
  const session = j.token || j.access_token || '';
  if (!session) throw new Error('站点换令牌没给回令牌');
  return session;
}

/** 问站点这个会话是谁 —— 票据里带 uid 是为了审计"谁在管这台云端"，不能是unknown 也至少要有。 */
async function sessionUser(session) {
  try {
    const r = await fetch(new URL('/oauth/userinfo', SITE_URL), {
      headers: { authorization: `Bearer ${session}` },
      signal: AbortSignal.timeout(8000),
    });
    if (!r.ok) return 'unknown';
    const j = await r.json();
    return j.username || j.preferred_username || 'unknown';
  } catch {
    return 'unknown';
  }
}

/** 给浏览器看的收尾页：把结果交给 opener，让用户关掉。 */
function resultPage(ok, data) {
  const send = ok
    ? `opener && opener.postMessage(${JSON.stringify({
        type: 'xjk-admin-oauth', ok: true,
        ticket: data.ticket, exp: data.exp, user: data.user,
      })}, '*');`
    : `opener && opener.postMessage(${JSON.stringify({
        type: 'xjk-admin-oauth', ok: false, error: data.error,
      })}, '*');`;
  const msg = ok ? '登录成功，可以关掉这个页面了' : `登录失败：${data.error}`;
  const color = ok ? '#22c55e' : '#ef4444';
  // charset 必须显式给 utf8：这台机器上的中文不写 charset 会变乱码。
  return `<!doctype html><meta charset="utf-8"><title>${ok ? '已登录' : '登录失败'}</title>
<style>body{margin:0;height:100vh;display:flex;align-items:center;justify-content:center;
font:14px/1.7 system-ui,"Microsoft YaHei",sans-serif;background:#0e1117;color:#e5e7eb}
.box{text-align:center;max-width:420px;padding:24px}
.dot{width:44px;height:44px;border-radius:50%;margin:0 auto 14px;border:3px solid ${color}}
.hint{margin-top:10px;font-size:12px;color:#9ca3af}</style>
<div class="box"><div class="dot"></div><div>${msg.replace(/</g, '&lt;')}</div>
<div class="hint">这个页面会自动关闭；没关的话请手动关掉</div></div>
<script>${send}setTimeout(()=>{try{opener&&opener.focus();window.close();}catch(e){}},800);<\/script>`;
}

/**
 * 起回跳 server。返回 null = 没配全，调用方（index.js）照旧用手敲静态令牌的登录页。
 * 绑 127.0.0.1、端口 0（系统分配），只认一条路径。
 */
export function createAdminOAuthServer(log) {
  if (!SITE_URL || !OAUTH_CLIENT_SECRET || !OAUTH_REDIRECT_PATH.startsWith('/')) {
    log('[admin-oauth] 没配全 STE_SITE_URL / STE_OAUTH_CLIENT_SECRET —— 管理台仍走手敲静态令牌，不是故障');
    return null;
  }
  if (!VIEWER_SECRET) {
    // 签不出票 = 这条新登录方式不管用，说清楚，别让 UI 以为成功。
    log('[admin-oauth] 没配 CLOUD_VIEWER_SECRET，签不出管理台票据 —— 管理台仍走手敲静态令牌');
    return null;
  }
  if (!/^https?:\/\//i.test(SITE_URL)) {
    log(`[admin-oauth] STE_SITE_URL 不是 http/https（${SITE_URL}）—— 管理台仍走手敲静态令牌`);
    return null;
  }

  const server = http.createServer(async (req, res) => {
    const u = new URL(req.url, `http://127.0.0.1`);
    if (u.pathname !== OAUTH_REDIRECT_PATH) {
      res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
      return res.end('not found');
    }

    const state = u.searchParams.get('state') || '';
    const code = u.searchParams.get('code') || '';
    const err = u.searchParams.get('error') || '';
    // 回跳地址要跟授权时带的一字不差（站点会比对），所以现算，不缓存、不另造 state。
    const redirectUri = currentRedirectUri(server);

    // state 要么不存在要么过期，一律当没发生过（一次性，用掉就删）
    const issued = states.get(state);
    states.delete(state);

    if (err) {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      return res.end(resultPage(false, { error: `站点拒绝了授权（${err}），请在浏览器里再点一次「用网站账号登录」` }));
    }
    if (!code) {
      res.writeHead(400, { 'content-type': 'text/html; charset=utf-8' });
      return res.end(resultPage(false, { error: '回跳没带 code（不是从授权页回来的？）' }));
    }
    if (!issued) {
      res.writeHead(400, { 'content-type': 'text/html; charset=utf-8' });
      return res.end(resultPage(false, { error: 'state 校验没过（链接过期或不是本站签发的），请重新登录' }));
    }

    try {
      const session = await exchangeCode(code, redirectUri);
      const uid = await sessionUser(session);
      const ttlMs = Math.min(Math.max(Number(ADMIN_TICKET_TTL_MS) || 0, 60 * 1000), ADMIN_TICKET_TTL_MAX_MS);
      const r = mintViewerTicket(uid, ttlMs);
      if (!r.ok) {
        res.writeHead(500, { 'content-type': 'text/html; charset=utf-8' });
        return res.end(resultPage(false, { error: r.reason || '云端签票失败' }));
      }
      log(`[admin-oauth] 管理台登录成功 uid=${uid} ttl=${Math.round(ttlMs / 3600000)}h`);
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      return res.end(resultPage(true, { ticket: r.ticket, exp: r.exp, user: uid }));
    } catch (e) {
      log(`[admin-oauth] 换票失败：${e.message}`);
      res.writeHead(502, { 'content-type': 'text/html; charset=utf-8' });
      return res.end(resultPage(false, { error: e.message }));
    }
  });

  server.on('error', (e) => {
    // 临时端口理论上不会占，真占了也不能让整个云端跟着挂 —— 后台还有老登录方式能用。
    log(`[admin-oauth] 回跳端口起不来（${e.message}）—— 管理台仍走手敲静态令牌`);
  });

  server.listen(0, '127.0.0.1', () => {
    log(`[admin-oauth] 回跳已就绪：${buildRedirectUri(server.address().port)}（站点的 ${OAUTH_CLIENT_ID} 客户端）`);
  });

  return server;
}

function buildRedirectUri(port) {
  // 端口 0 是 listen 之前拿到的哨兵值，listen 之后 address().port 才是真端口，
  // 所以这里每次现算，不缓存。
  return `http://127.0.0.1:${port}${OAUTH_REDIRECT_PATH}`;
}

/** 给 index.js 用：当前回跳地址（还没起 server 时返回 null）。 */
export function currentRedirectUri(server) {
  if (!server || !server.listening) return null;
  const port = server.address()?.port;
  if (!port) return null;
  return buildRedirectUri(port);
}

/** 给 index.js 用：发一枚新 state 并给出完整登录参数。 */
export function beginAuthorization(server, log) {
  if (!server) return { enabled: false, reason: '没配 STE_SITE_URL / STE_OAUTH_CLIENT_SECRET，管理台不走网站账号登录' };
  const redirectUri = currentRedirectUri(server);
  if (!redirectUri) return { enabled: false, reason: '回跳端口还没起来' };
  const { authorizeUrl, state } = buildAuthorize(redirectUri);
  log(`[admin-oauth] 后台发起登录 state=${state.slice(0, 8)}…`);
  return { enabled: true, authorizeUrl, state, redirectUri, clientId: OAUTH_CLIENT_ID, siteUrl: SITE_URL };
}
