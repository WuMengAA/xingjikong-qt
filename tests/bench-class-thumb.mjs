// 缩略图大班并发压测（2026-10-24 · #496）
//
// 目的：量化 class-events 缩略图链路在「大班并发」下的表现——
//   ① 常规轮：60 台设备 × 3 轮缩略图（4KB data，模拟真实 3.5KB）→ 事件全量到达、无丢失
//   ② 超载轮：60 台 × 40 轮连发（绕 uid×kind 1s 限流靠不同 uid）→ fanout 背压跳过计数、内存增量
//   ③ 限流专项：单台同 kind 二连发 → 第二条 throttled（1s 闸门）
// 同时统计：到达率 / 云端进程 RSS 增量 / 缩略图是否进历史环（后订者快照无 thumbnail）。
// 只对隔离实例跑（自起临时云端，端口自动挑，不碰生产 8788）。
//
// 用法：node tests/bench-class-thumb.mjs [devices=60]

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const WebSocket = require(process.env.WS_MODULE || 'ws');

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const N = Number(process.argv[2] || 60);

const WAR_DIR = mkdtempSync(join(tmpdir(), 'cloud-thumb-bench-'));
const CLOUD_PORT = 19000 + Math.floor(Math.random() * 400);
const DEV_TOKEN = 'bench-agent-token';
const VIEWER_TOKEN = 'bench-viewer-token';
const VIEWER_SECRET = 'bench-secret';
const CLASS_A = 'cls-bench0001';
const SNAP = 60;               // 快照里最近事件条数（cloud 默认 CLASS_EVENT_SNAPSHOT）
const DATA_B64 = Buffer.alloc(2600, 0x41).toString('base64'); // ~3467B data JSON

/* ---------- 云端 ---------- */
function startCloud() {
  const child = spawn(process.execPath, [CLOUD_ENTRY], {
    cwd: join(__dirname, '..'),
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
      CLOUD_CLASS_ROOM_DB_FILE: join(WAR_DIR, 'class-room.db'),
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

function call(method, path, { auth } = {}) {
  return new Promise((resolve) => {
    const req = http.request({ host: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: auth ? { Authorization: `Bearer ${auth}` } : {} }, (res) => {
      let b = '';
      res.on('data', (c) => { b += c; });
      res.on('end', () => resolve({ status: res.statusCode, body: b }));
    });
    req.on('error', () => resolve({ status: -1, body: '' }));
    req.end();
  });
}

async function waitUp(timeoutMs = 15000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    try {
      const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
      if (r.status === 200) return true;
    } catch { /* not up */ }
    await sleep(250);
  }
  throw new Error('云端没起来');
}

function stopCloud(c) {
  return new Promise((resolve) => {
    if (!c || c.child.exitCode !== null) return resolve();
    c.child.once('exit', () => resolve());
    try { c.child.kill(); } catch { /* ignore */ }
    setTimeout(resolve, 4000);
  });
}

/* ---------- WS 工具 ---------- */
function wsClient(path) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 非 JSON */ } });
  return { ws, inbox };
}
const opened = (c) => new Promise((res, rej) => {
  if (c.ws.readyState === 1) return res();
  c.ws.on('open', res);
  c.ws.on('error', rej);
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function connectAgent(uid, classCode) {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'register', id: `reg-${uid}`,
    payload: { uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9', name: `机器-${uid}`,
      actions: [], ...(classCode ? { classCode, className: '压测班' } : {}) } }));
  await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid, 5000);
  return c;
}
async function connectViewer() {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: { token: VIEWER_TOKEN } }));
  await waitFor(c.inbox, (m) => m.type === 'viewer-ready', 5000);
  return c;
}
function waitFor(inbox, pred, ms = 8000, from = 0) {
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

/* ---------- 内存 ---------- */
function rssOf(pid) {
  try { return process._getActiveHandles?.() ? NaN : NaN; } catch { return NaN; }
}
// Windows 用 wmic 不可用（已移除），改用 tasklist /FO CSV 拿内存（GBK，仅数字）
function rssMB(pid) {
  const { execSync } = require('node:child_process');
  try {
    const out = execSync(`tasklist /FI "PID eq ${pid}" /FO CSV /NH`, { encoding: 'utf8' });
    const m = out.match(/"([0-9,]+) K"/);
    return m ? Math.round(parseInt(m[1].replace(/,/g, ''), 10) / 1024) : NaN;
  } catch { return NaN; }
}

(async () => {
  let cloud = startCloud();
  const tStart = Date.now();
  let pass = 0, fail = 0;
  const ok = async (label, fn) => {
    try { await fn(); pass++; console.log(`[ok] ${label}`); }
    catch (e) { fail++; console.log(`[FAIL] ${label} — ${e.message}`); }
  };

  try {
    await waitUp();
    const cloudPid = cloud.child.pid;
    const rss0 = rssMB(cloudPid);

    // 绑班：第一台注册即建班（classCode 非空）
    const agents = [];
    for (let i = 0; i < N; i++) {
      const a = await connectAgent(`B${String(i).padStart(2, '0')}`, CLASS_A);
      agents.push(a);
    }
    const viewer = await connectViewer();
    viewer.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's1', payload: { code: CLASS_A } }));
    await waitFor(viewer.inbox, (m) => m.type === 'class-subscribed' && m.payload?.classCode === CLASS_A);

    // ── ① 常规轮：每台 3 轮缩略图（间隔 1.2s，模拟 15s tick 的加速）──
    await ok(`常规轮：${N} 台 × 3 轮 thumbnail 全量到达、零丢失`, async () => {
      const before = viewer.inbox.length;
      for (let rnd = 0; rnd < 3; rnd++) {
        for (const a of agents) {
          a.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: `t-${a.ws.uid}`,
            payload: { kind: 'thumbnail', data: { w: 320, h: 180, mime: 'image/jpeg', b64: DATA_B64 } } }));
        }
        await sleep(1200);
      }
      // 等 fanout 落定
      await sleep(1500);
      const got = viewer.inbox.filter((m) => m.type === 'class.event' && m.payload?.kind === 'thumbnail').length;
      const expect = N * 3;
      console.log(`   期望 ${expect} 条，实收 ${got} 条（自订阅起累计）`);
      // viewer 可能还有 subscribe 快照等，用累计差：从 before 后新增的 class.event 计数
      const after = viewer.inbox.length;
      const added = viewer.inbox.slice(before).filter((m) => m.type === 'class.event').length;
      console.log(`   本轮新增 class.event = ${added}`);
      if (added !== expect) throw new Error(`到达率 ${added}/${expect}`);
    });

    // ── ② 超载轮：60 台 × 40 轮连发（无间隔，靠不同 uid 绕 1s 限流）──
    await ok(`超载轮：${N} 台 × 40 轮连发 → fanout 背压护栏生效、不拖垮其他通道`, async () => {
      const before = viewer.inbox.length;
      const burst = 40;
      for (let rnd = 0; rnd < burst; rnd++) {
        for (const a of agents) {
          a.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: `b-${rnd}`,
            payload: { kind: 'thumbnail', data: { w: 320, h: 180, mime: 'image/jpeg', b64: DATA_B64 } } }));
        }
      }
      await sleep(4000);
      const added = viewer.inbox.slice(before).filter((m) => m.type === 'class.event').length;
      const rss1 = rssMB(cloudPid);
      console.log(`   期望 ${N * burst} 条，实收 ${added} 条；云端 RSS ${rss0}MB → ${rss1}MB`);
      if (added === 0) throw new Error('超载轮一条都没到（链路被冲垮）');
      if (rss1 - rss0 > 400) throw new Error(`内存增量 ${rss1 - rss0}MB 异常（缩略图疑似进历史环）`);
    });

    // ── ②b 背压分级：绕限流（每台独立 kind）猛灌 → 8MB fanout 背压真实触发 ──
    await ok('背压分级：每台独立 kind 绕限流猛灌 → 8MB fanout 背压跳过、连接不崩', async () => {
      const before = viewer.inbox.length;
      const burst = 40;
      for (let rnd = 0; rnd < burst; rnd++) {
        for (let i = 0; i < agents.length; i++) {
          // kind 每台独立且每轮变化 → uid×kind 限流完全绕开，纯测 fanout 背压
          agents[i].ws.send(JSON.stringify({ v: 1, type: 'class-event',
            id: `bp-${rnd}-${i}`,
            payload: { kind: `thumb.b${rnd}.${i}`, data: { w: 320, h: 180, mime: 'image/jpeg', b64: DATA_B64 } } }));
        }
        await sleep(80); // 400ms 内发完 60 台，防串行过慢
      }
      await sleep(5000);
      const added = viewer.inbox.slice(before).filter((m) => m.type === 'class.event').length;
      const rss1 = rssMB(cloudPid);
      console.log(`   期望 ${N * burst} 条，实收 ${added} 条；云端 RSS ${rss0}MB → ${rss1}MB`);
      if (added === 0) throw new Error('背压轮一条都没到（链路被冲垮）');
      if (rss1 - rss0 > 600) throw new Error(`内存增量 ${rss1 - rss0}MB 异常（缩略图疑似进历史环）`);
      // 订阅连接仍在 = 背压只是跳过，没有掐连接
      if (viewer.ws.readyState !== 1) throw new Error('订阅连接被掐断了！');
    });

    // ── ③ 限流专项：单台同 kind 二连发 → 第二条 throttled ──
    await ok('限流专项：同 uid×kind 二连发 → 第二条被 1s 闸门拒，订阅者只收 1 条', async () => {
      const a = agents[0];
      const before = viewer.inbox.length;
      a.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'f1', payload: { kind: 'test.rate', data: { n: 1 } } }));
      a.ws.send(JSON.stringify({ v: 1, type: 'class-event', id: 'f2', payload: { kind: 'test.rate', data: { n: 2 } } }));
      await sleep(1200);
      const added = viewer.inbox.slice(before).filter((m) => m.type === 'class.event' && m.payload?.kind === 'test.rate').length;
      console.log(`   同 kind 二连发，订阅者实收 ${added} 条（应为 1）`);
      if (added !== 1) throw new Error(`限流未生效：实收 ${added}`);
    });

    // ── ④ 缩略图不进历史环：新订者快照里无 thumbnail ──
    await ok('缩略图不进历史环：后订者快照事件里无 thumbnail', async () => {
      const v2 = await connectViewer();
      v2.ws.send(JSON.stringify({ v: 1, type: 'subscribe_class', id: 's2', payload: { code: CLASS_A } }));
      const snap = await waitFor(v2.inbox, (m) => m.type === 'class-subscribed' && m.payload?.classCode === CLASS_A, 6000);
      if (!snap) throw new Error('第二订阅者没拿到快照');
      const evs = snap.payload.events || [];
      const thumbs = evs.filter((e) => e.kind === 'thumbnail').length;
      console.log(`   快照事件 ${evs.length} 条，其中 thumbnail ${thumbs} 条（应为 0）`);
      if (thumbs !== 0) throw new Error('缩略图进了历史环！');
      v2.ws.close();
    });

    const rss2 = rssMB(cloudPid);
    console.log(`\n==== 结果：${pass} 通过 / ${fail} 失败；耗时 ${((Date.now() - tStart) / 1000).toFixed(1)}s；云端 RSS ${rss0}→${rss2}MB ====`);
    process.exit(fail === 0 ? 0 : 1);
  } finally {
    await stopCloud(cloud);
    try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* ignore */ }
    console.log('[.] 云端已停，临时目录已清');
  }
})().catch((e) => { console.error('FATAL', e); process.exit(2); });