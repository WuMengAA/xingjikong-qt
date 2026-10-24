// 班级管理 API · 端到端回归（2026-10-24 · 管理端「班级管理」面板云端侧）
//
// 守的契约（class-admin.js + class-dir.js rename/delete + registry G3/G4）：
//   ① create 只建身份不绑设备（/api/classes 里 devices 为空）
//   ② create 幂等：同名再建 → 同码；只给合法 code → 用该 code（外部码优先）
//   ③ create 空名空码 → 拒
//   ④ rename 只动显示名：名变、码不变（canonical 铁律）
//   ⑤ rename 撞别班显示名 → 拒（同名归一会把设备送到别人的班）
//   ⑥ rename 不存在的班 → 拒
//   ⑦ G3 换班残留：设备绑 A 再绑 B → A 班索引不含该 uid（只加不清的回归）
//   ⑧ unbind：设备移出，该班 devices 空
//   ⑨ delete refuse：有设备时拒，并带回 deviceCount
//   ⑩ delete release：设备全部移出 + 班消失 + 订阅者收 class-unsubscribed{class-deleted}
//   ⑪ delete 不存在的班 → 拒
//   ⑫ G4 在线下发：在线设备 bind-class → WS 收到 class-bound{code,name}
//   ⑬ 鉴权：无 token → 401（isViewerApi 前缀生效）
//
// 跑法：node tests/regression-class-admin.mjs   （自带临时目录 + 自动挑端口）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-admin-'));

const CLOUD_PORT = 8800;               // 8798/8799 已被既有 harness 占用（class-events / voicehub standin）
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';
const CLASS_A = 'cls-ca01a';           // 9 位，过 isClassCodeValid（6-16）
const CLASS_B = 'cls-ca02b';
const UID_A = 'TEST_CA_A';
const UID_C = 'TEST_CA_C';             // 在线绑班用（G4）

process.env.CLOUD_VIEWER_SECRET = VIEWER_SECRET;

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** 极简 HTTP 客户端：POST JSON / GET，返回 { status, json } */
function call(method, path, { auth, body } = {}) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? null : JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: `Bearer ${auth}` } : {}),
        ...(data ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(data) } : {}),
      },
      timeout: 8000,
    }, (res) => {
      let raw = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { raw += c; });
      res.on('end', () => {
        let json = null;
        try { json = JSON.parse(raw); } catch { json = { raw }; }
        resolve({ status: res.statusCode, json });
      });
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', reject);
    if (data) req.write(data);
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

/* ---------- WS 客户端 ---------- */

function wsClient(path) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  const state = { closed: false };
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 忽略非 JSON */ } });
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

/** 在 inbox 里等一条匹配的消息（from 起扫——findIndex 无 fromIndex 参数，必须自己比下标）。 */
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

/** 连一台被控端并注册（静态令牌路）。classCode 传空 = 不绑班。 */
async function connectAgent(uid, classCode) {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: {
      uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9',
      name: `机器-${uid}`, actions: [],
      ...(classCode ? { classCode, className: '注册班' } : {}),
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 注册没收到 registered 回执`);
  return c;
}

/** 连一个管理端 WS（静态令牌路）。 */
async function connectViewer() {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: { token: VIEWER_TOKEN } }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'viewer-ready');
  assert.ok(ack, '管理端鉴权后没收到 viewer-ready');
  return c;
}

/** 取 /api/classes 里某个班（找不到回 null）。 */
async function getClassOf(code) {
  const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
  assert.equal(r.status, 200);
  return (r.json.classes || []).find((c) => c.code === code) || null;
}

const J = (m) => JSON.stringify(m);

// ── 开跑 ─────────────────────────────────────────────────────────────
console.log('班级管理 API · 端到端回归');

let cloud = startCloud();
let agentA = null, agentC = null, viewer1 = null;

try {
  await waitUp();

  // 设备 A 带 CLASS_A 注册（G3/unbind/delete 的样本）；设备 C 不绑班（G4 在线绑班）
  agentA = await connectAgent(UID_A, CLASS_A);
  agentC = await connectAgent(UID_C, null);
  viewer1 = await connectViewer();

  // ① create 只建身份不绑设备
  await ok('create 只给班名 → 建班成功且不绑任何设备', async () => {
    const r = await call('POST', '/api/class/create', {
      auth: VIEWER_TOKEN, body: { className: '管理测试班' },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.ok(r.json.ok, J(r.json));
    assert.ok(/^cls-[0-9a-f]{10}$/.test(r.json.classCode), `派生码不合规：${r.json.classCode}`);
    assert.equal(r.json.name, '管理测试班');
    const cls = await getClassOf(r.json.classCode);
    assert.ok(cls, '新建的班没出现在 /api/classes');
    assert.equal(cls.devices.length, 0, `create 居然绑了设备：${J(cls.devices)}`);
  });

  // ② create 幂等：同名再建 → 同码
  await ok('create 幂等：同名再建 → 返回同一个码', async () => {
    const r1 = await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { className: '管理测试班' } });
    const r2 = await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { className: '管理测试班' } });
    assert.ok(r1.json.ok && r2.json.ok);
    assert.equal(r1.json.classCode, r2.json.classCode, '同名两次创建拿到了不同的码');
  });

  // ②b create 只给合法 code → 外部码优先（站点 class_id 必须原样保留）
  await ok('create 只给合法 code → 用该 code（外部码优先于派生）', async () => {
    const r = await call('POST', '/api/class/create', {
      auth: VIEWER_TOKEN, body: { classCode: 'class_01', className: '站点对账班' },
    });
    assert.equal(r.status, 200, J(r.json));
    assert.equal(r.json.classCode, 'class_01', `外部码被改写了：${r.json.classCode}`);
    const cls = await getClassOf('class_01');
    assert.ok(cls, 'class_01 没进班级目录');
    assert.equal(cls.name, '站点对账班');
  });

  // ③ create 空名空码 → 拒
  await ok('create 空名空码 → 400 拒', async () => {
    const r = await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: {} });
    assert.equal(r.status, 400, `status=${r.status}`);
    assert.equal(r.json.ok, false);
  });

  // ④ rename 只动显示名（canonical 铁律）
  await ok('rename 只动显示名：名变、码不变', async () => {
    const r = await call('POST', '/api/class/rename', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A, className: '高一(7)班' },
    });
    assert.equal(r.status, 200, J(r.json));
    assert.equal(r.json.classCode, CLASS_A, 'rename 把码也改了（违反 canonical 铁律）');
    const cls = await getClassOf(CLASS_A);
    assert.equal(cls.name, '高一(7)班', `显示名没变：${cls.name}`);
  });

  // ⑥ rename 不存在 → 拒
  await ok('rename 不存在的班 → 400 拒', async () => {
    const r = await call('POST', '/api/class/rename', {
      auth: VIEWER_TOKEN, body: { classCode: 'cls-nosuch1', className: '幽灵班' },
    });
    assert.equal(r.status, 400);
    assert.equal(r.json.ok, false);
  });

  // ⑦ G3：设备绑 A 再绑 B → A 班索引必须清掉该 uid（只加不清的回归）
  await ok('G3 换班残留：设备 A→B 改绑后，A 班索引不含该 uid', async () => {
    const before = await getClassOf(CLASS_A);
    assert.ok(before.devices.includes(UID_A), '前置条件不成立：设备一开始不在 A 班');
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: UID_A, classCode: CLASS_B },
    });
    assert.equal(r.status, 200, `绑班失败：${J(r.json)}`);
    const a = await getClassOf(CLASS_A);
    const b = await getClassOf(CLASS_B);
    assert.ok(!a.devices.includes(UID_A), `A 班索引残留：${J(a.devices)}`);
    assert.ok(b.devices.includes(UID_A), `B 班索引没有该设备：${J(b.devices)}`);
    assert.equal(b.onlineCount, 1, `B 班在线数应为 1，实为 ${b.onlineCount}`);
    assert.equal(a.onlineCount, 0, `A 班在线数应为 0，实为 ${a.onlineCount}（虚高=G3 复发）`);
  });

  // ⑤（顺序依赖：必须放在 G3 之后——CLASS_B 是被改绑操作创建的，此前不存在）
  await ok('rename 撞别班显示名 → 400 拒', async () => {
    const r = await call('POST', '/api/class/rename', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_B, className: '高一(7)班' },
    });
    assert.equal(r.status, 400, `status=${r.status}`);
    assert.ok(/已被班级/.test(r.json.error || ''), `错误信息不明确：${J(r.json)}`);
  });

  // ⑧ unbind：移出设备
  await ok('unbind：设备移出班级，该班 devices 空', async () => {
    const r = await call('POST', '/api/class/unbind', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_B, uid: UID_A },
    });
    assert.equal(r.status, 200, J(r.json));
    const b = await getClassOf(CLASS_B);
    assert.equal(b.devices.length, 0, `B 班还有设备：${J(b.devices)}`);
    const r2 = await call('POST', '/api/class/unbind', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_B, uid: UID_A },
    });
    assert.equal(r2.status, 400, '重复移出应拒（设备已不在此班）');
  });

  // ⑨ delete refuse：有设备时拒，带回 deviceCount
  await ok('delete refuse：班内有设备 → 拒并带回设备数', async () => {
    await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: UID_A, classCode: CLASS_A },
    });
    const r = await call('POST', '/api/class/delete', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A },
    });
    assert.equal(r.status, 400, `status=${r.status}`);
    assert.equal(r.json.ok, false);
    assert.equal(r.json.deviceCount, 1, `deviceCount=${r.json.deviceCount}`);
    const cls = await getClassOf(CLASS_A);
    assert.ok(cls, 'refuse 删除居然把班删了');
  });

  // ⑩ delete release：先订后删 → 设备移出 + 班消失 + 订阅者收 class-unsubscribed
  await ok('delete release：设备全移出 + 班消失 + 订阅者收到 class-unsubscribed', async () => {
    // viewer1 订 CLASS_A
    viewer1.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 'sub-del', payload: { code: CLASS_A } }));
    const sub = await waitFor(viewer1.inbox, (m) => m.type === 'class-subscribed' && m.payload?.classCode === CLASS_A);
    assert.ok(sub, '删班前订阅没成功');
    const before = viewer1.inbox.length;
    const r = await call('POST', '/api/class/delete', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A, strategy: 'release' },
    });
    assert.equal(r.status, 200, J(r.json));
    assert.equal(r.json.releasedDevices, 1, `releasedDevices=${r.json.releasedDevices}`);
    assert.equal(r.json.subsDropped, 1, `subsDropped=${r.json.subsDropped}`);
    const cls = await getClassOf(CLASS_A);
    assert.equal(cls, null, '班没从 /api/classes 消失');
    const un = await waitFor(viewer1.inbox,
      (m) => m.type === 'class-unsubscribed' && m.payload?.classCode === CLASS_A, 2500, before);
    assert.ok(un, '订阅者没收到 class-unsubscribed');
    assert.equal(un.payload.reason, 'class-deleted', `reason=${un.payload?.reason}`);
    // 设备归属已清：重订同码应被拒（班不存在）
    viewer1.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 'sub-del2', payload: { code: CLASS_A } }));
    const rej = await waitFor(viewer1.inbox, (m) => m.type === 'error', 2500, viewer1.inbox.indexOf(un) + 1);
    assert.ok(rej, '删班后重订没收到拒绝');
  });

  // ⑪ delete 不存在 → 拒
  await ok('delete 不存在的班 → 400 拒', async () => {
    const r = await call('POST', '/api/class/delete', { auth: VIEWER_TOKEN, body: { classCode: 'cls-nosuch1' } });
    assert.equal(r.status, 400);
    assert.equal(r.json.ok, false);
  });

  // ⑫ G4：在线设备 bind-class → WS 收到 class-bound
  await ok('G4 在线下发：在线设备绑班 → WS 收到 class-bound{code,name}', async () => {
    const before = agentC.inbox.length;
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: UID_C, className: '在线绑班' },
    });
    assert.equal(r.status, 200, `绑班失败：${J(r.json)}`);
    const m = await waitFor(agentC.inbox, (x) => x.type === 'class-bound', 2500, before);
    assert.ok(m, `在线设备没收到 class-bound，收到的是：${agentC.inbox.slice(before).map(J).join(' | ')}`);
    assert.ok(m.payload.code, 'class-bound 没带 code');
    assert.equal(m.payload.name, '在线绑班');
  });

  // ⑬ 鉴权：无 token → 401（isViewerApi 前缀生效）
  await ok('鉴权：无 token 调 /api/class/create → 401', async () => {
    const r = await call('POST', '/api/class/create', { body: { className: '无证建班' } });
    assert.equal(r.status, 401, `status=${r.status}`);
    const cls = await getClassOf('cls-nosuch1');
    assert.equal(cls, null);
  });

} finally {
  for (const c of [agentA, agentC, viewer1]) {
    try { c?.ws.close(); } catch { /* 已关 */ }
  }
  await stopCloud(cloud);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail ? 1 : 0);
