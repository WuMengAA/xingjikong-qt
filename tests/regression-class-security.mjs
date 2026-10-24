// 班级安全二件套 · 端到端回归 —— 2026-10-24
//
// 守的是今天新加的「云端白名单」与「重认证门」：
//   ① 环境白名单（env-gate.js + registry.env）：
//      管理员把设备加进白名单 → 该设备 env='test'（devices 广播可见，不用等重连）；
//      移出白名单 → 立即回落 'prod'；不在白名单 = 生产设备。
//  ② 重认证门（reauth.js）：
//      票据（人）在窗口内 → 敏感操作放行；
//      超时 → subscribe_class / instruction 被 REAUTH_REQUIRED 拒，**连接不断**；
//      重新登录（断线重连 + 新票据）→ 刷新验证时刻 → 放行；
//      静态令牌（系统身份）永不受拦。
//
// 跑法：node tests/regression-class-security.mjs
// 注意：本测试把 STE_REAUTH_INTERVAL_MS 压到 1.5s 让"过期"在测试里可等。

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-sec-'));

const CLOUD_PORT = 8796;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';
const REAUTH_MS = 1500;              // 压到 1.5s，让"过期"在测试里可等

const CLASS_MAIN = 'cls-sec01';
const UID_DEV = 'TEST_SEC_DEV';
const UID_TEACHER = 'teacher-sec';

process.env.CLOUD_VIEWER_SECRET = VIEWER_SECRET;
const { mintViewerTicket } = await import('../src/ticket.js');

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function call(method, path, { auth, body } = {}) {
  return new Promise((resolve, reject) => {
    const raw = body === undefined ? '' : JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: `Bearer ${auth}` } : {}),
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
      STE_REAUTH_INTERVAL_MS: String(REAUTH_MS),
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
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  const state = { closed: false };
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 忽略 */ } });
  ws.on('close', () => { state.closed = true; });
  return { ws, inbox, state };
}
function opened(c) {
  return new Promise((resolve, reject) => {
    if (c.ws.readyState === 1) return resolve();
    c.ws.on('open', resolve);
    c.ws.on('error', reject);
  });
}
function waitFor(inbox, pred, ms = 2500, from = 0) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const tick = () => {
      const i = inbox.findIndex((m, idx) => idx >= from && pred(m));
      if (i >= 0) { resolve(inbox[i]); return; }
      if (Date.now() - t0 > ms) { resolve(null); return; }
      setTimeout(tick, 20);
    };
    tick();
  });
}

async function connectAgent(uid, classCode = '') {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: {
      uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9',
      name: `机器-${uid}`, actions: [],
      ...(classCode ? { classCode, className: '安全测试班' } : {}),
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 注册没收到 registered 回执`);
  return c;
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

async function subscribe(c, code) {
  const from = c.inbox.length;
  c.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: `sub-${code}`, payload: { code } }));
  return waitFor(c.inbox, (x) => x.type === 'class-subscribed'
    || (x.type === 'error' && (x.payload?.code === 'NO_DEVICE_PERMISSION'
        || x.payload?.code === 'REAUTH_REQUIRED' || x.payload?.ecode === 'REAUTH_REQUIRED'))
    || (x.type === 'error' && /reauth/i.test(x.payload?.message || '')), 3000, from);
}

/* ─────────────── 开始 ─────────────── */

const cloud = startCloud();
const agents = [];

try {
  await waitUp();

  /* ── ① 环境白名单 ── */

  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, className: '安全测试班' } });
  const agent = await connectAgent(UID_DEV, CLASS_MAIN);
  agents.push(agent);

  await ok('白名单基线：未加白名单的设备 env=prod（devices 广播可见）', async () => {
    const v = await connectViewer(VIEWER_TOKEN);
    try {
      const msg = await waitFor(v.inbox, (m) => m.type === 'devices' && m.payload?.devices?.length >= 1);
      const dev = (msg.payload.devices || []).find((d) => d.uid === UID_DEV);
      assert.equal(dev?.env, 'prod', `期望 prod，实际 ${JSON.stringify(dev)}`);
    } finally { try { v.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('白名单加机：管理员把设备加进白名单，立即生效（不用等重连）', async () => {
    const r = await call('POST', '/api/env/test-whitelist', {
      auth: VIEWER_TOKEN, body: { uid: UID_DEV, test: true },
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.test, true);
    const v = await connectViewer(VIEWER_TOKEN);
    try {
      const msg = await waitFor(v.inbox, (m) => m.type === 'devices');
      const dev = (msg.payload.devices || []).find((d) => d.uid === UID_DEV);
      assert.equal(dev?.env, 'test', `期望 test，实际 ${JSON.stringify(dev)}`);
    } finally { try { v.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('白名单移除：回到 prod', async () => {
    const r = await call('POST', '/api/env/test-whitelist', {
      auth: VIEWER_TOKEN, body: { uid: UID_DEV, test: false },
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    const v = await connectViewer(VIEWER_TOKEN);
    try {
      const msg = await waitFor(v.inbox, (m) => m.type === 'devices');
      const dev = (msg.payload.devices || []).find((d) => d.uid === UID_DEV);
      assert.equal(dev?.env, 'prod', `期望 prod，实际 ${JSON.stringify(dev)}`);
    } finally { try { v.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('白名单权限：普通教师票改白名单 → 403', async () => {
    const t = mintViewerTicket('teacher-low', 3600_000, {
      schoolId: null, roles: ['teacher'], scopes: [],
    }).ticket;
    const r = await call('POST', '/api/env/test-whitelist', {
      auth: t, body: { uid: UID_DEV, test: true },
    });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  /* ── ② 重认证门 ── */

  const teacherTicket = mintViewerTicket(UID_TEACHER, 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_MAIN }],
  }).ticket;

  const v1 = await connectViewer(teacherTicket);

  await ok('重认证：窗口内敏感操作放行', async () => {
    const r = await subscribe(v1, CLASS_MAIN);
    assert.equal(r?.type, 'class-subscribed', JSON.stringify(r));
  });

  await ok('重认证：超过窗口 → 订阅班级被 REAUTH_REQUIRED 拒', async () => {
    await sleep(REAUTH_MS + 300);
    const r = await subscribe(v1, CLASS_MAIN);
    // message 是中文（"需要重新验证密码…"），reauth 字样在 code/ecode —— 三个字段分开断言
    const p = r?.payload || {};
    const hit = /reauth/i.test(p.message || '') || /reauth/i.test(p.code || '') || /reauth/i.test(p.ecode || '');
    assert.ok(r && r.type === 'error' && hit, `期望 error/REAUTH，实际 ${JSON.stringify(r)}`);
  });

  await ok('重认证：被拒后**连接不断**（还能发消息、还能收 devices 广播）', async () => {
    assert.equal(v1.ws.readyState, 1, '连接应保持 open');
    const from = v1.inbox.length;
    v1.ws.send(JSON.stringify({ v: 1, type: 'devices', id: 'd1', payload: {} }));
    const msg = await waitFor(v1.inbox, (m) => m.type === 'devices', 3000, from);
    assert.ok(!!msg, '过期后仍应能收到 devices（只拦敏感操作，不拦只读）');
  });

  await ok('重认证：重新登录（重连+新票据）→ 验证时刻刷新 → 恢复放行', async () => {
    try { v1.ws.close(); } catch { /* 忽略 */ }
    const v2 = await connectViewer(teacherTicket);
    try {
      const r = await subscribe(v2, CLASS_MAIN);
      assert.equal(r?.type, 'class-subscribed', JSON.stringify(r));
    } finally { try { v2.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('重认证：静态令牌（系统身份）永不受拦', async () => {
    // 拿静态令牌订阅（此时距第一次 auth 已远超 1.5s 窗口，验证令牌不查重认证）
    const v = await connectViewer(VIEWER_TOKEN);
    try {
      const r = await subscribe(v, CLASS_MAIN);
      assert.equal(r?.type, 'class-subscribed', JSON.stringify(r));
    } finally { try { v.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('重认证：HTTP 班级写操作过期 → 403 REAUTH_REQUIRED', async () => {
    const t = mintViewerTicket(UID_TEACHER, 3600_000, {
      schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_MAIN }],
    }).ticket;
    // 先连一次 auth 记下验证时刻，等过期
    const vv = await connectViewer(t);
    await sleep(REAUTH_MS + 300);
    try { vv.ws.close(); } catch { /* 忽略 */ }
    const r = await call('POST', '/api/class/member-add', {
      auth: t, body: { classCode: CLASS_MAIN, uid: 'someone', role: 'viewer' },
    });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status}`);
    assert.match(String(r.json.code || r.json.ecode), /reauth/i);
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  for (const a of agents) { try { a.ws.close(); } catch { /* 忽略 */ } }
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-6).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);