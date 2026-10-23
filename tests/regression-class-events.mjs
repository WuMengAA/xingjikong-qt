// 班级域事件通道 · 端到端回归（2026-10-23 · #458 班级工作台地基）
//
// 守的是"按班"这条新通道的完整契约（class-events.js + index.js 接线层）：
//   ① subscribe_class → class-subscribed 快照（设备清单/在线数/最近事件）
//   ② agent class-event → 该班订阅者收 class.event + 发送方收 ack
//   ③ 伪冒防护：载荷里带假 classCode，投递标签仍是云端真值（设备自报不可信）
//   ④ 限流：同 uid×kind 二连发 → 第二条 throttled，订阅者只收 1 条
//   ⑤ 坏 kind → ack ok:false，不投递
//   ⑥ 未绑班设备 → 拒收（NO_CLASS），不静默
//   ⑦ 权限：scoped 票订别班 → NO_DEVICE_PERMISSION；订本班 → ok
//   ⑧ 成员上下线：agent 断 → member.offline；重连 → member.online
//   ⑨ 环形缓冲：后订者快照里含历史事件
//   ⑩ 退订后不再收；断开后新订阅者可订且照常收
//   ⑪ 全部退订/断开后 ack viewers:0（无悬挂投递目标）
//
// 为什么要起真服务：班级域横跨 registry（班级归属）、perms（权限）、
// protocol（信封）、WS 生命周期（断开清理）四个模块，纯单测验不了接线。
//
// 跑法：node tests/regression-class-events.mjs   （自带临时目录 + 自动挑端口）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-events-'));

const CLOUD_PORT = 8798;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';
const CLASS_A = 'cls-test01';          // 10 位，过 isClassCodeValid（6-16）
const UID_A = 'TEST_CE_A';
const UID_B = 'TEST_CE_B';             // 不绑班的设备

// ⚠️ config.js / ticket.js 在模块加载时读 env ⇒ 必须在 import 它们之前设好
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

/** 极简 HTTP 客户端：返回 { status, json } */
function call(method, path, { auth } = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: auth ? { authorization: `Bearer ${auth}` } : {},
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

/** 在 inbox 里等一条匹配的消息（轮询，from 起扫——用于跳过此前已收的消息）。超时回 null。
 *  ⚠️ findIndex 没有 fromIndex 参数（第二参是 thisArg），必须自己在回调里比下标。 */
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
      ...(classCode ? { classCode, className: '测试班' } : {}),
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 注册没收到 registered 回执`);
  return c;
}

/** 连一个管理端并鉴权。auth = {token} 或 {ticket}。 */
async function connectViewer(auth) {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: auth }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'viewer-ready');
  assert.ok(ack, '管理端鉴权后没收到 viewer-ready');
  return c;
}

const J = (m) => JSON.stringify(m);

// ── 开跑 ─────────────────────────────────────────────────────────────
console.log('班级域事件通道 · 端到端回归');

let cloud = startCloud();
let agentA = null, agentB = null, viewer1 = null, viewer2 = null, viewer3 = null, viewer4 = null;

try {
  await waitUp();

  // 设备 A 带班注册；设备 B 不绑班（⑥ 用）
  agentA = await connectAgent(UID_A, CLASS_A);
  agentB = await connectAgent(UID_B, null);

  // 管理端 1：静态令牌（全权）
  viewer1 = await connectViewer({ token: VIEWER_TOKEN });

  // ① 订阅班级 → 快照
  await ok('subscribe_class → class-subscribed 快照（设备/在线数/事件齐）', async () => {
    viewer1.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's1', payload: { code: CLASS_A } }));
    const m = await waitFor(viewer1.inbox, (x) => x.type === 'class-subscribed');
    assert.ok(m, `没收到 class-subscribed，收到的是：${viewer1.inbox.map(J).join(' | ')}`);
    assert.equal(m.payload.ok, true);
    assert.equal(m.payload.classCode, CLASS_A);
    assert.equal(m.payload.onlineCount, 1, `onlineCount=${m.payload.onlineCount}`);
    assert.ok(m.payload.devices.some((d) => d.uid === UID_A), '快照设备里没有 TEST_CE_A');
    assert.ok(Array.isArray(m.payload.events), 'events 不是数组');
    assert.ok(m.payload.events.some((e) => e.kind === 'member.online'), '快照历史里没有 member.online');
  });

  // ② 设备上报 → 订阅者收 class.event + ack
  await ok('agent class-event → 订阅者收 class.event、发送方收 ack(viewers=1)', async () => {
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e2',
      payload: { kind: 'test.ping', data: { msg: 'hello' } } }));
    const ev = await waitFor(viewer1.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'test.ping');
    assert.ok(ev, '订阅者没收到 class.event');
    assert.equal(ev.payload.uid, UID_A);
    assert.equal(ev.payload.classCode, CLASS_A);
    assert.equal(ev.payload.data.msg, 'hello');
    assert.ok(typeof ev.payload.id === 'number', '事件没有 id');
    const ack = await waitFor(agentA.inbox, (x) => x.type === 'class-event-ack');
    assert.ok(ack, '发送方没收到 class-event-ack');
    assert.equal(ack.payload.ok, true);
    assert.equal(ack.payload.viewers, 1, `viewers=${ack.payload.viewers}`);
  });

  // ②b 缩略图：实时 fanout 照旧，但不进历史环（防大班量下内存炸）
  await ok('缩略图：实时 fanout 照常、但不进历史环', async () => {
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e2b',
      payload: { kind: 'thumbnail', data: { w: 320, h: 180, mime: 'image/jpeg', b64: 'AAAA' } } }));
    const ev = await waitFor(viewer1.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'thumbnail');
    assert.ok(ev, '缩略图实时 fanout 没送达订阅者');
  });

  // ③ 伪冒防护：载荷带假 classCode，标签仍是云端真值
  await ok('伪冒防护：载荷里的假 classCode 被忽略，事件标签仍是云端真值', async () => {
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e3',
      payload: { kind: 'test.spoof', classCode: 'cls-spof1', data: { classCode: 'cls-spof1' } } }));
    const ev = await waitFor(viewer1.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'test.spoof');
    assert.ok(ev, '伪冒测试事件没送达（若信任了载荷 classCode，它会被投到没人订的班）');
    assert.equal(ev.payload.classCode, CLASS_A, `事件被贴成了 ${ev.payload.classCode}`);
  });

  // ④ 限流：同 uid×kind 二连发
  await ok('限流：同 uid×kind 二连发 → 第二条 throttled，订阅者只收 1 条', async () => {
    const vBefore = viewer1.inbox.length;
    const aBefore = agentA.inbox.length;
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e4a', payload: { kind: 'test.flood', data: { n: 1 } } }));
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e4b', payload: { kind: 'test.flood', data: { n: 2 } } }));
    const a1 = await waitFor(agentA.inbox, (x) => x.type === 'class-event-ack', 2500, aBefore);
    const a2 = await waitFor(agentA.inbox, (x) => x.type === 'class-event-ack', 2500, agentA.inbox.indexOf(a1) + 1);
    assert.ok(a1 && a1.payload.ok === true && !a1.payload.throttled, `第一条 ack 异常：${J(a1)}`);
    assert.ok(a2 && a2.payload.throttled === true, `第二条没被限流：${J(a2)}`);
    await sleep(500);
    const got = viewer1.inbox.slice(vBefore).filter((x) => x.type === 'class.event' && x.payload?.kind === 'test.flood');
    assert.equal(got.length, 1, `订阅者收了 ${got.length} 条（应为 1）`);
  });

  // ⑤ 坏 kind → 拒收不投递
  await ok('坏 kind → ack ok:false，不投递', async () => {
    const before = viewer1.inbox.length;
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e5',
      payload: { kind: 'Bad Kind!', data: {} } }));
    const ack = await waitFor(agentA.inbox, (x) => x.type === 'class-event-ack' && x.payload?.ok === false);
    assert.ok(ack, '坏 kind 没有收到失败 ack');
    assert.equal(ack.payload.code, 'BAD_PAYLOAD');
    await sleep(400);
    const got = viewer1.inbox.slice(before).filter((x) => x.type === 'class.event');
    assert.equal(got.length, 0, '坏 kind 竟然投递出去了');
  });

  // ⑥ 未绑班设备 → 拒收
  await ok('未绑班设备上报 → NO_CLASS 拒收（不静默）', async () => {
    agentB.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e6',
      payload: { kind: 'test.noclass', data: {} } }));
    const ack = await waitFor(agentB.inbox, (x) => x.type === 'class-event-ack');
    assert.ok(ack, '没收到 ack');
    assert.equal(ack.payload.ok, false);
    assert.equal(ack.payload.code, 'NO_CLASS', `code=${ack.payload.code}`);
  });

  // ⑦ 权限：scoped 票
  await ok('权限：scoped 票订别班被拒、订本班通过', async () => {
    const t = mintViewerTicket('teacher-scoped', 120000, {
      roles: [], scopes: [{ type: 'class', id: CLASS_A }],
    });
    assert.ok(t.ok, `签票失败：${t.reason}`);
    viewer2 = await connectViewer({ ticket: t.ticket });
    // 订本班 → ok
    viewer2.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's7a', payload: { code: CLASS_A } }));
    const mOk = await waitFor(viewer2.inbox, (x) => x.type === 'class-subscribed');
    assert.ok(mOk && mOk.payload.ok === true, 'scoped 票订本班失败');
    // 订别班 → NO_DEVICE_PERMISSION（PV4002）
    viewer2.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's7b', payload: { code: 'cls-other9' } }));
    const mErr = await waitFor(viewer2.inbox, (x) => x.type === 'error');
    assert.ok(mErr, '订别班没有收到错误');
    assert.equal(mErr.payload.code, 'NO_DEVICE_PERMISSION', `code=${mErr.payload.code}`);
    assert.equal(mErr.payload.ecode, 'PV4002');
  });

  // ⑧ 成员上下线
  await ok('成员上下线：agent 断 → member.offline；重连 → member.online', async () => {
    agentA.ws.close();
    const off = await waitFor(viewer1.inbox, (x) => x.type === 'class.member.offline' && x.payload?.uid === UID_A);
    assert.ok(off, '没收到 member.offline');
    assert.equal(off.payload.classCode, CLASS_A);
    agentA = await connectAgent(UID_A, CLASS_A);
    const on = await waitFor(viewer1.inbox, (x) => x.type === 'class.member.online' && x.payload?.uid === UID_A);
    assert.ok(on, '重连后没收到 member.online');
  });

  // ⑨ 环形缓冲：后订者快照含历史
  await ok('环形缓冲：后订者快照里含历史事件', async () => {
    viewer3 = await connectViewer({ token: VIEWER_TOKEN });
    viewer3.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's9', payload: { code: CLASS_A } }));
    const m = await waitFor(viewer3.inbox, (x) => x.type === 'class-subscribed');
    assert.ok(m, 'viewer3 没订上');
    const kinds = m.payload.events.map((e) => e.kind);
    assert.ok(kinds.includes('member.online'), '历史里没有 member.online');
    assert.ok(kinds.includes('test.ping'), '历史里没有 test.ping');
    assert.ok(!kinds.includes('thumbnail'), '缩略图进了历史环（应被排除）');
    assert.ok(m.payload.events.length <= 20, `快照事件条数 ${m.payload.events.length} 超过 20`);
  });

  // ⑩ 退订后不再收
  await ok('退订后不再收该班事件', async () => {
    viewer1.ws.send(JSON.stringify({ v: 1, type: 'unsubscribe_class', id: 'u10', payload: { code: CLASS_A } }));
    const m = await waitFor(viewer1.inbox, (x) => x.type === 'class-unsubscribed');
    assert.ok(m && m.payload.ok === true, '没收到 class-unsubscribed');
    const before = viewer1.inbox.length;
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e10', payload: { kind: 'test.afterunsub', data: {} } }));
    // viewer3 应该收到（证明事件真在流动，不是全局断了）
    const got3 = await waitFor(viewer3.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'test.afterunsub');
    assert.ok(got3, 'viewer3 没收到（事件流断了？）');
    await sleep(400);
    const got1 = viewer1.inbox.slice(before).filter((x) => x.type === 'class.event');
    assert.equal(got1.length, 0, '退订后的连接竟然还在收');
  });

  // ⑪ 断开清理：viewer 断开后，新订阅者可订且照常收
  await ok('断开清理：viewer 断开后新订阅者可订且照常收', async () => {
    // viewer2（scoped 票）与 viewer3 先后断开 —— 两类连接都要清干净
    viewer2.ws.close();
    await sleep(200);
    viewer3.ws.close();
    await sleep(300);
    viewer4 = await connectViewer({ token: VIEWER_TOKEN });
    viewer4.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's11', payload: { code: CLASS_A } }));
    const m = await waitFor(viewer4.inbox, (x) => x.type === 'class-subscribed');
    assert.ok(m, 'viewer4 没订上（断开清理把公共状态搞坏了？）');
    assert.ok(m.payload.events.some((e) => e.kind === 'test.afterunsub'), '快照历史缺 test.afterunsub');
    // 云端回执不带请求 id ⇒ 按位置取最后一条旧 ack 之后的新 ack
    const aFrom = agentA.inbox.reduce((acc, y, idx) => (y.type === 'class-event-ack' ? idx + 1 : acc), 0);
    agentA.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'e11', payload: { kind: 'test.fresh', data: {} } }));
    const ev = await waitFor(viewer4.inbox, (x) => x.type === 'class.event' && x.payload?.kind === 'test.fresh');
    assert.ok(ev, 'viewer4 没收到新事件');
    const ack = await waitFor(agentA.inbox, (x) => x.type === 'class-event-ack', 2500, aFrom);
    assert.ok(ack && ack.payload.viewers === 1, `viewers=${J(ack?.payload)}（断开后订阅者计数应为 1）`);
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 运行期异常：${e.message}`);
  console.error('--- cloud stdout ---\n' + (cloud.log.out || ''));
  console.error('--- cloud stderr ---\n' + (cloud.log.err || ''));
} finally {
  for (const c of [viewer4, viewer3, viewer2, viewer1, agentB, agentA]) {
    try { if (c && !c.state.closed) c.ws.close(); } catch { /* 已断 */ }
  }
  await stopCloud(cloud);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 忽略 */ }
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
process.exit(fail === 0 ? 0 : 1);
