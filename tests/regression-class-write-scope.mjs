// 班级写操作按班授权 · 端到端复现/回归 —— 2026-10-26（大范围找活 · 审计面二）
//
// 守的是：/api/class/{rename,delete,unbind,bind} 这四个写操作必须按请求身份校验
// 「能否管理目标班级」，不能让"只属于 A 班的已登录者"去改/删任意班。
//
// 复现手法：mint 一张 scope 仅限 cls-write-own 的受限票据，先经 WS 登录登记 fresh
// 认证（绕过 2026-10-24 重认证门），再在窗口内发 HTTP 写 —— 此时重认证门放行，
// 请求抵达 class-admin 的 adminRenameClass 等函数，而这些函数**不接收 ident、不查
// canManageMembers**，于是越权成功（返回 200）。本测试断言必须 400（拒绝越权写）。
// 修复前会 FAIL（返回 200）= 漏洞实证；修复后 PASS。

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-wscope-'));

const CLOUD_PORT = 8795;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

const CLS_OWN = 'cls-write-own';
const CLS_VICTIM = 'cls-write-victim';
const CLS_CTRL = 'cls-write-ctrl';
const UID_DEV = 'TEST_WSCOPE_DEV';

process.env.CLOUD_VIEWER_SECRET = VIEWER_SECRET;
const { mintViewerTicket } = await import('../src/ticket.js');

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + '\n       ' + e.message); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function call(method, path, opts) {
  const auth = opts && opts.auth;
  const body = opts && opts.body;
  return new Promise((resolve, reject) => {
    const raw = body === undefined ? '' : JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: 'Bearer ' + auth } : {}),
        ...(body === undefined ? {} : { 'content-type': 'application/json', 'content-length': Buffer.byteLength(raw) }),
      },
      timeout: 8000,
    }, (res) => {
      let s = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { s += c; });
      res.on('end', () => {
        let json = null;
        try { json = JSON.parse(s); } catch { json = { raw: s }; }
        resolve({ status: res.statusCode, json });
      });
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', reject);
    if (raw) req.write(raw);
    req.end();
  });
}

function startCloud() {
  const child = spawn(process.execPath, [CLOUD_ENTRY], {
    cwd: WAR_DIR,
    env: {
      ...process.env,
      CLOUD_WS_PORT: String(CLOUD_PORT),
      CLOUD_WS_HOST: '127.0.0.1',
      CLOUD_WS_TOKEN: DEV_TOKEN,
      CLOUD_VIEWER_TOKEN: VIEWER_TOKEN,
      CLOUD_VIEWER_SECRET: VIEWER_SECRET,
      CLOUD_EVENTS_FILE: join(WAR_DIR, 'events.log'),
      CLOUD_QUEUE_DB_FILE: join(WAR_DIR, 'queue.db'),
      CLOUD_INTERACT_DB_FILE: join(WAR_DIR, 'interact.db'),
      CLOUD_CLASS_DIR_DB_FILE: join(WAR_DIR, 'class-dir.db'),
      CLOUD_CLASS_ROOM_DB_FILE: join(WAR_DIR, 'class-room.db'),
      CLOUD_TEST_WHITELIST_FILE: join(WAR_DIR, 'test-whitelist.json'),
      STE_REAUTH_INTERVAL_MS: '3600000',
      STE_QML_PATCH_DISABLE: '1',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  const log = { out: '', err: '' };
  child.stdout.on('data', (c) => { log.out += c; });
  child.stderr.on('data', (c) => { log.err += c; });
  return { child, log };
}

async function waitUp(timeoutMs = 15000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    try {
      const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
      if (r.status === 200) return true;
    } catch { /* 还没起来 */ }
    await sleep(250);
  }
  throw new Error('云端没能在超时内起来');
}

function stopCloud(c) {
  return new Promise((resolve) => {
    if (!c || c.child.exitCode !== null) return resolve();
    c.child.once('exit', () => resolve());
    try { c.child.kill(); } catch { resolve(); }
    setTimeout(resolve, 4000);
  });
}

function wsClient(path) {
  const ws = new WebSocket('ws://127.0.0.1:' + CLOUD_PORT + path);
  const inbox = [];
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 忽略 */ } });
  return { ws, inbox };
}
function opened(c) {
  return new Promise((resolve, reject) => {
    if (c.ws.readyState === 1) return resolve();
    c.ws.on('open', resolve);
    c.ws.on('error', reject);
  });
}
async function waitFor(inbox, pred, ms = 3000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    const m = inbox.find(pred);
    if (m) return m;
    await sleep(20);
  }
  return null;
}
async function connectViewer(ticketOrToken) {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'auth', id: 'auth-1',
    payload: ticketOrToken.startsWith('v1.') ? { ticket: ticketOrToken } : { token: ticketOrToken },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'viewer-ready');
  assert.ok(ack, '管理端鉴权后没收到 viewer-ready');
  return c;
}

async function connectAgent(uid, classCode) {
  const ws = new WebSocket('ws://127.0.0.1:' + CLOUD_PORT + '/ws/agent');
  await new Promise((res, rej) => { ws.on('open', res); ws.on('error', rej); });
  ws.send(JSON.stringify({
    v: 1, type: 'register', id: 'reg-' + uid,
    payload: { uid, token: DEV_TOKEN, fp: 'fp-' + uid, version: '9.9.9', name: '机-' + uid, actions: [], classCode, className: '受害班' },
  }));
  await new Promise((res) => setTimeout(res, 400));
  return ws;
}

/* ─────────────── 开始 ─────────────── */

const cloud = startCloud();
const agents = [];
const viewers = [];

try {
  await waitUp();

  // 三个班 + 一台受害者设备（在受害班里）
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLS_OWN, className: '我的班' } });
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLS_VICTIM, className: '受害班' } });
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLS_CTRL, className: '对照班' } });
  const ag = await connectAgent(UID_DEV, CLS_VICTIM);
  agents.push(ag);

  // 受限票据：scope 仅 cls-write-own（攻击者只属于 A 班）
  const attackerTicket = mintViewerTicket('attacker', 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLS_OWN }],
  }).ticket;

  // 先经 WS 登录登记 fresh 认证，绕过 2026-10-24 重认证门（否则所有票据写都被 REAUTH 拦，
  // 反而掩盖了真正的按班越权）。登录后重认证窗内请求会抵达 class-admin 漏洞代码。
  const av = await connectViewer(attackerTicket);
  viewers.push(av);

  await ok('越权退班：scope 仅 A 班的票据把 B 班设备移出 → 必须 400（修复前会 200=漏洞）', async () => {
    const r = await call('POST', '/api/class/unbind', { auth: attackerTicket, body: { classCode: CLS_VICTIM, uid: UID_DEV } });
    assert.equal(r.status, 400, '期望 400，实际 ' + r.status + ' ' + JSON.stringify(r.json));
  });

  await ok('越权入班：scope 仅 A 班的票据把设备塞进 B 班 → 必须 400（修复前会 200=漏洞）', async () => {
    const r = await call('POST', '/api/class/bind', { auth: attackerTicket, body: { classCode: CLS_VICTIM, uid: UID_DEV } });
    assert.equal(r.status, 400, '期望 400，实际 ' + r.status + ' ' + JSON.stringify(r.json));
  });

  await ok('越权改名：scope 仅 A 班的票据改 B 班 → 必须 400（修复前会 200=漏洞）', async () => {
    const r = await call('POST', '/api/class/rename', { auth: attackerTicket, body: { classCode: CLS_VICTIM, className: '被改名的班' } });
    assert.equal(r.status, 400, '期望 400，实际 ' + r.status + ' ' + JSON.stringify(r.json));
  });

  await ok('越权删班：scope 仅 A 班的票据删 B 班 → 必须 400（修复前会 200=漏洞）', async () => {
    const r = await call('POST', '/api/class/delete', { auth: attackerTicket, body: { classCode: CLS_VICTIM, strategy: 'release' } });
    assert.equal(r.status, 400, '期望 400，实际 ' + r.status + ' ' + JSON.stringify(r.json));
  });

  // 正向控制：同一张票据能管自己的班（cls-write-own）
  await ok('正向控制：scope=A 班的票据改自己班 → 200', async () => {
    const r = await call('POST', '/api/class/rename', { auth: attackerTicket, body: { classCode: CLS_OWN, className: '我的班-改名' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });

  // 正向控制：静态令牌（全权）能删任意班（用独立对照班，避免与上面越权 delete 相互干扰）
  await ok('正向控制：静态令牌（全权）删对照班 → 200', async () => {
    const r = await call('POST', '/api/class/delete', { auth: VIEWER_TOKEN, body: { classCode: CLS_CTRL, strategy: 'release' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });
} catch (e) {
  fail++;
  console.error('  FAIL 脚手架异常：' + e.message);
} finally {
  for (const a of agents) { try { a.close(); } catch { /* 忽略 */ } }
  for (const v of viewers) { try { v.ws.close(); } catch { /* 忽略 */ } }
  await stopCloud(cloud);
  console.log('\n云端日志尾部：\n' + (cloud.log.out + cloud.log.err).split('\n').slice(-10).join('\n'));
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log('\n结果：' + pass + ' 过 / ' + fail + ' 败');
process.exit(fail === 0 ? 0 : 1);
