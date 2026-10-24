// 班级成员（人可以被添加）· 端到端回归 —— 2026-10-24
//
// 守的是「加人进班 → 立刻生效」这条链（class-admin.js + class-room.js + class-events.js）：
//   ① 基线：scoped 票（scopes 只含别的班）订本班 → 被拒
//   ② 通过 /api/class/member-add 把该账号加进班（teacher）
//   ③ **同一个连接不重连、不重签票**，再订阅 → 成功
//      ← 这是整个改动的核心价值：站点签的票是静态的，加人必须能立刻生效
//   ④ 快照带 myRole，且能看到该班设备（票里 scopes 并不含这个班）
//   ⑤ 移出后立刻失去权限（订阅被拒）
//   ⑥ 提权防护：成员身份（teacher）不能把自己/别人设成 owner
//
// 跑法：node tests/regression-class-members.mjs（自带临时目录 + 自动端口）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-members-'));

const CLOUD_PORT = 8797;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

const CLASS_MAIN = 'cls-main01';       // 要加人的班
const CLASS_OTHER = 'cls-other1';      // 票里 scopes 指向的班（用来证明"原本没权限"）
const UID_DEV = 'TEST_CM_DEV';
const UID_TEACHER = 'teacher-li';
const UID_TEACHER2 = 'teacher-wang';

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

async function connectAgent(uid, classCode) {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: {
      uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9',
      name: `机器-${uid}`, actions: [],
      ...(classCode ? { classCode, className: '测试班' } : {}),
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 注册没收到 registered 回执`);
  return c;
}

async function connectViewer(ticket) {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: { ticket } }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'viewer-ready');
  assert.ok(ack, '管理端鉴权后没收到 viewer-ready');
  return c;
}

/** 发 subscribe_class 并取回执（class-subscribed 或 error）。
 *  ⚠️ 云端 sndErr 的 error 通道**不带请求 id**（见 index.js sndErr），
 *  只能用 from 起点 + payload.code 区分是哪次请求的回执。 */
async function subscribe(c, code) {
  const from = c.inbox.length;
  c.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: `sub-${code}`, payload: { code } }));
  const m = await waitFor(c.inbox, (x) => x.type === 'class-subscribed'
    || (x.type === 'error' && x.payload?.code === 'NO_DEVICE_PERMISSION'), 3000, from);
  return m;
}

/** 结果是不是拒绝。 */
const isDenied = (m) => !!(m && m.type === 'error' && m.payload?.code === 'NO_DEVICE_PERMISSION');

/* ─────────────── 开始 ─────────────── */

const cloud = startCloud();
const agents = [];
let viewer = null;

try {
  await waitUp();

  const agent = await connectAgent(UID_DEV, CLASS_MAIN);
  agents.push(agent);

  // 建班（静态令牌 = 全权）并把设备绑进去
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, className: '高二3班' } });
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLASS_OTHER, className: '高二9班' } });
  const bind = await call('POST', '/api/class/bind', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, uid: UID_DEV } });
  assert.equal(bind.status, 200, `绑设备失败：${JSON.stringify(bind.json)}`);

  // 老师票：scopes 只有"别的班"
  const ticket = mintViewerTicket(UID_TEACHER, 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_OTHER }],
  }).ticket;

  viewer = await connectViewer(ticket);

  await ok('基线：票里 scopes 不含本班 → 订阅被拒（证明后面成功不是白给的）', async () => {
    const r = await subscribe(viewer, CLASS_MAIN);
    assert.ok(isDenied(r), `期望被拒，实际 ${JSON.stringify(r)}`);
  });

  await ok('加人：管理员把该账号加入本班（teacher）', async () => {
    const r = await call('POST', '/api/class/member-add', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, uid: UID_TEACHER, role: 'teacher' },
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.role, 'teacher');
    assert.equal(r.json.added, true);
  });

  await ok('核心：同一连接、不重签票、再订阅 → 成功', async () => {
    const r = await subscribe(viewer, CLASS_MAIN);
    assert.ok(!isDenied(r), `期望成功，实际 ${JSON.stringify(r)}`);
    assert.equal(r.type, 'class-subscribed');
  });

  await ok('快照带 myRole=teacher', async () => {
    const r = await subscribe(viewer, CLASS_MAIN);
    assert.equal(r.payload?.myRole, 'teacher', JSON.stringify(r.payload));
  });

  await ok('成员能看到该班设备（票里 scopes 并不含这个班）', async () => {
    const r = await subscribe(viewer, CLASS_MAIN);
    const uids = (r.payload?.devices || []).map((d) => d.uid);
    assert.ok(uids.includes(UID_DEV), `设备清单里没有 ${UID_DEV}：${JSON.stringify(uids)}`);
  });

  await ok('提权防护：成员（teacher）不能把别人设成 owner', async () => {
    const t2 = mintViewerTicket(UID_TEACHER2, 3600_000, {
      schoolId: null, roles: ['teacher'], scopes: [],
    }).ticket;
    const v2 = await connectViewer(t2);
    try {
      const r = await call('POST', '/api/class/member-add', {
        auth: t2, body: { classCode: CLASS_MAIN, uid: UID_TEACHER2, role: 'owner' },
      });
      assert.equal(r.status, 400, `期望 400 被拒，实际 ${r.status} ${JSON.stringify(r.json)}`);
    } finally { try { v2.ws.close(); } catch { /* 忽略 */ } }
  });

  await ok('成员清单可查（members 路由）', async () => {
    const r = await call('POST', '/api/class/members', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    const uidList = (r.json.members || []).map((m) => m.id);
    assert.ok(uidList.includes(UID_TEACHER), JSON.stringify(uidList));
  });

  await ok('我的班级：按账号反查（管理端启动不用全量拉目录）', async () => {
    const r = await call('POST', '/api/class/my-classes', { auth: ticket, body: {} });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    const codes = (r.json.classes || []).map((c) => c.classCode);
    assert.ok(codes.includes(CLASS_MAIN), JSON.stringify(codes));
  });

  await ok('移出后立刻失权：再订阅被拒（不是等下次重连才失效）', async () => {
    const rm = await call('POST', '/api/class/member-remove', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, uid: UID_TEACHER },
    });
    assert.equal(rm.status, 200, JSON.stringify(rm.json));
    assert.equal(rm.json.removed, true);
    const r = await subscribe(viewer, CLASS_MAIN);
    assert.ok(isDenied(r), `期望被拒，实际 ${JSON.stringify(r)}`);
  });

  await ok('未授权的匿名调用被 401 拒（成员管理不是公开接口）', async () => {
    const r = await call('POST', '/api/class/member-add', {
      body: { classCode: CLASS_MAIN, uid: 'x', role: 'owner' },
    });
    assert.equal(r.status, 401, JSON.stringify(r.json));
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  for (const a of agents) { try { a.ws.close(); } catch { /* 忽略 */ } }
  if (viewer) { try { viewer.ws.close(); } catch { /* 忽略 */ } }
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-6).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);
