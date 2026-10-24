// 发布中心（P1，2026-10-24）· 端到端回归
//
// 守的是「管理端发布一张卡片 → 云端推全班在线被控端 + 落 Timeline」这条链
// （class-admin.js adminPublish + registry.js sendToClassDevices + 既有权限/重认证门）：
//   ① 权限拒绝：非本班成员且无 scopes 的票据者 publish → 400 无权限
//   ② 类型非法 → 400 不支持的发布类型
//   ③ 空内容 → 400 发布内容不能为空
//   ④ 正常发布：班内在线设备 = 1 → 返回 delivered:1 / online:1
//   ⑤ 设备（被控端 WS）实时收到 blackboard-card（payload.type 与发布一致）
//   ⑥ Timeline 留痕：管理端订阅者收到 class.event kind=publish
//
// 跑法：node tests/regression-class-publish.mjs（自带临时目录 + 自动端口）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-publish-'));

const CLOUD_PORT = 8821;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

const CLASS_MAIN = 'cls-main02';
const CLASS_OTHER = 'cls-other2';
const UID_DEV = 'TEST_PUB_DEV';
const UID_TEACHER = 'teacher-pub';

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
      // 刻意**不**关 CLASS_EVENT_MIN_MS（用默认 1s 真实值）：本测试要覆盖的正是
      // 「限流开启时，连发两条不同内容的发布会不会被吞」这条真实路径 ——
      // 修好前这里必然只剩 1 条。用例⑦⑧把它钉死（改回置 0 会让⑦⑧失去意义）。
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

async function subscribe(c, code) {
  const from = c.inbox.length;
  c.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: `sub-${code}`, payload: { code } }));
  const m = await waitFor(c.inbox, (x) => x.type === 'class-subscribed'
    || (x.type === 'error' && x.payload?.code === 'NO_DEVICE_PERMISSION'), 3000, from);
  return m;
}

const cloud = startCloud();
const agents = [];
let viewer = null;

try {
  await waitUp();

  const agent = await connectAgent(UID_DEV, CLASS_MAIN);
  agents.push(agent);

  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, className: '高二3班' } });
  await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: CLASS_OTHER, className: '高二9班' } });
  const bind = await call('POST', '/api/class/bind', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, uid: UID_DEV } });
  assert.equal(bind.status, 200, `绑设备失败：${JSON.stringify(bind.json)}`);

  // 越权票据：scopes 只含"别的班"，且不是本班成员
  const foreignTicket = mintViewerTicket(UID_TEACHER, 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_OTHER }],
  }).ticket;

  // 先连一次 viewer（foreignTicket）触发 onViewerAuthed → noteVerified(teacher-pub)，
  // 让它"验证新鲜"，这样后面的权限拒绝测试能越过 reauth 门、露出真正的权限判定。
  // （reauth 门在所有 /api/class/* POST 之前，非 fresh 的票据会被 403 挡在权限门之前。）
  const probeViewer = await connectViewer(foreignTicket);
  try { probeViewer.ws.close(); } catch { /* 忽略 */ }

  await ok('权限拒绝：越权票据向本班发布 → 400 无权限', async () => {
    const r = await call('POST', '/api/class/publish', {
      auth: foreignTicket, body: { classCode: CLASS_MAIN, card: { type: 'class-notice', title: 't', body: 'b' } },
    });
    assert.equal(r.status, 400, JSON.stringify(r.json));
    assert.ok(/无权限/.test(r.json.error || ''), JSON.stringify(r.json));
  });

  await ok('类型非法 → 400 不支持的发布类型', async () => {
    const r = await call('POST', '/api/class/publish', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'bogus', title: 't', body: 'b' } },
    });
    assert.equal(r.status, 400, JSON.stringify(r.json));
    assert.ok(/发布类型/.test(r.json.error || ''), JSON.stringify(r.json));
  });

  await ok('空内容 → 400 发布内容不能为空', async () => {
    const r = await call('POST', '/api/class/publish', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'class-notice', title: '', body: '   ' } },
    });
    assert.equal(r.status, 400, JSON.stringify(r.json));
    assert.ok(/不能为空/.test(r.json.error || ''), JSON.stringify(r.json));
  });

  await ok('正常发布：班内 1 台在线 → delivered:1 / online:1', async () => {
    const r = await call('POST', '/api/class/publish', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'fullscreen-notice', title: '放学通知', body: '今天提前放学' } },
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.ok, true);
    assert.equal(r.json.delivered, 1, `期望推到 1 台，实际 ${JSON.stringify(r.json)}`);
    assert.equal(r.json.online, 1, `期望班内 1 台，实际 ${JSON.stringify(r.json)}`);
  });

  await ok('被控端实时收到 blackboard-card（payload.type 一致）', async () => {
    const from = agent.inbox.length;
    await call('POST', '/api/class/publish', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'homework', title: '数学作业', body: 'P1-P10' } },
    });
    const m = await waitFor(agent.inbox, (x) => x.type === 'blackboard-card' && x.payload?.type === 'homework', 2500, from);
    assert.ok(m, `被控端没收到 homework 的 blackboard-card：${JSON.stringify(agent.inbox.slice(-5))}`);
    assert.equal(m.payload?.title, '数学作业');
    assert.equal(m.payload?.body, 'P1-P10');
  });

  await ok('Timeline 留痕：订阅者收到 class.event kind=publish', async () => {
    // 把 foreignTicket 持有者加进本班，使其可订阅并收到 publish 事件
    await call('POST', '/api/class/member-add', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, uid: UID_TEACHER, role: 'teacher' } });
    viewer = await connectViewer(foreignTicket);
    const sub = await subscribe(viewer, CLASS_MAIN);
    assert.ok(sub && sub.type === 'class-subscribed', `订阅失败：${JSON.stringify(sub)}`);
    const from = viewer.inbox.length;
    await call('POST', '/api/class/publish', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'class-notice', title: '班会', body: '下午班会' } },
    });
    const m = await waitFor(viewer.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'publish', 2500, from);
    assert.ok(m, `订阅者没收到 publish 事件：${JSON.stringify(viewer.inbox.slice(-5))}`);
    assert.equal(m.payload?.data?.type, 'class-notice');
  });

  // ⑦ 修复验证（2026-10-25）：uid×kind 粗粒度限流会静默吞掉 1s 内的第二条发布
  // ⇒ 卡片照样推到设备、Timeline 却没痕迹。现在限流键并入内容指纹，不同内容放行。
  await ok('连发两条不同内容：两条都进 Timeline（1s 限流不再误吞）', async () => {
    const from = viewer.inbox.length;
    await Promise.all([
      call('POST', '/api/class/publish', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'class-notice', title: '通知A', body: '内容A' } } }),
      call('POST', '/api/class/publish', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: { type: 'class-notice', title: '通知B', body: '内容B' } } }),
    ]);
    await sleep(800);
    const got = viewer.inbox.slice(from)
      .filter((x) => x.type === 'class.event' && x.payload?.kind === 'publish');
    assert.equal(got.length, 2,
      `期望两条不同内容都留痕，实际 ${got.length} 条：${JSON.stringify(got.map((g) => g.payload?.data?.title))}`);
  });

  // ⑧ 反向守卫：防重复刷屏的能力必须保留 —— 老师手抖连点两下不该刷两条一样的时间线。
  await ok('同一内容连点两下：只留一条（防重复刷屏保留）', async () => {
    const from = viewer.inbox.length;
    const same = { type: 'class-notice', title: '重复通知', body: '同样内容' };
    await call('POST', '/api/class/publish', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: same } });
    await call('POST', '/api/class/publish', { auth: VIEWER_TOKEN, body: { classCode: CLASS_MAIN, card: same } });
    await sleep(800);
    const got = viewer.inbox.slice(from)
      .filter((x) => x.type === 'class.event' && x.payload?.kind === 'publish');
    assert.equal(got.length, 1,
      `期望相同内容只留一条，实际 ${got.length} 条：${JSON.stringify(got.map((g) => g.payload?.data?.title))}`);
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  for (const a of agents) { try { a.ws.close(); } catch { /* 忽略 */ } }
  if (viewer) { try { viewer.ws.close(); } catch { /* 忽略 */ } }
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-8).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);
