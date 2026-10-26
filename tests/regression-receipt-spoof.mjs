// 回执归属伪造 · 端到端复现/回归 —— 2026-10-27（WS 帧级审计）
//
// 守的是：receipt（设备执行回执）必须只能由「指令下发目标设备」本人提交。
// 当前（修复前）recordResult(id, uid, ...) 不校验该 id 的下发目标 uid，且 settle 会
// 清掉目标的补发队列 —— 一台已认证设备 B 可拿 A 的指令 id 发 receipt：
//   ① 伪造 A 的执行状态（管理端看到假回执）；② settle 掉 A 的补发队列（A 真没收到
//   该指令时 = 指令静默丢失，关机/重启类指令可被针对掉）。
//
// 流程：A、B 两台设备注册（静态令牌，v1）→ REST 给 A 下发一条指令 →
//       从 A 的 WS 消息流捕获指令 id → B 用该 id 发 receipt{result:'done'}。
// 修复前：B 无任何响应（伪造成功）；修复后：B 收到 error=BAD_RECEIPT（拒绝）。
// 正向控制：A 自己回执同一 id → 修复后仍正常（不拦截本机回执）。

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'rcp-spoof-'));

const CLOUD_PORT = 8798;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';

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
      CLOUD_EVENTS_FILE: join(WAR_DIR, 'events.log'),
      CLOUD_QUEUE_DB_FILE: join(WAR_DIR, 'queue.db'),
      CLOUD_INTERACT_DB_FILE: join(WAR_DIR, 'interact.db'),
      CLOUD_CLASS_DIR_DB_FILE: join(WAR_DIR, 'class-dir.db'),
      CLOUD_CLASS_ROOM_DB_FILE: join(WAR_DIR, 'class-room.db'),
      CLOUD_TEST_WHITELIST_FILE: join(WAR_DIR, 'test-whitelist.json'),
      CLOUD_REPAIRS_FILE: join(WAR_DIR, 'repairs.json'),
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

/** 设备注册（v1）。返回 { ws, got: [] }：got 收所有文本帧供解析。 */
async function connectAgent(uid) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}/ws/agent`);
  const got = [];
  ws.on('message', (d) => {
    try { got.push(JSON.parse(d.toString('utf8'))); } catch { got.push({ raw: d.toString('utf8').slice(0, 60) }); }
  });
  await new Promise((res, rej) => { ws.on('open', res); ws.on('error', rej); });
  ws.send(JSON.stringify({
    v: 1, type: 'register',
    payload: { uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9', name: `机-${uid}` },
  }));
  // 等 registered
  const t0 = Date.now();
  while (!got.some((f) => f.type === 'registered') && Date.now() - t0 < 6000) await sleep(100);
  return { ws, got };
}

function findErr(got, code) {
  // v1 信封：{v,type:'error',payload:{ecode,code,message}} —— code 在 payload 里
  return got.find((f) => {
    if (f.type !== 'error') return false;
    const c = f.code ?? f.payload?.code ?? '';
    return c === code || String(c).includes(code);
  });
}

/* ─────────────── 开始 ─────────────── */

const cloud = startCloud();
const conns = [];

try {
  await waitUp();

  const A = await connectAgent('rc-A');
  const B = await connectAgent('rc-B');
  conns.push(A.ws, B.ws);

  await ok('脚手架：向 A 下发一条指令（REST，200 sent）', async () => {
    const r = await call('POST', '/api/instructions', {
      auth: VIEWER_TOKEN, body: { uid: 'rc-A', action: 'notify', params: { title: '回执审计' } },
    });
    assert.equal(r.status, 200, `期望 200，实际 ${r.status} ${JSON.stringify(r.json)}`);
    assert.equal(r.json.state, 'sent');
  });

  // 从 A 的消息流捕获指令 id（注册 v1 后云端按 v1 信封下发，id 在信封上）
  await ok('脚手架：从 A 的 WS 消息流捕获指令 id', async () => {
    const t0 = Date.now();
    while (!A.got.some((f) => f.type === 'instruction') && Date.now() - t0 < 6000) await sleep(100);
    const cmd = A.got.find((f) => f.type === 'instruction');
    assert.ok(cmd && cmd.id !== undefined && cmd.id !== '', `未捕获到指令帧：${JSON.stringify(A.got.slice(-4))}`);
    const n = Number(cmd.id);
    assert.ok(Number.isFinite(n) && n > 0, `指令 id 异常：${cmd.id}`);
    console.log(`  · 指令 id = ${n}（下发目标 rc-A，往返经 A 的 WS）`);
    A.insId = n;
  });

  await ok('越权回执：B 拿 A 的指令 id 回执 done → 必须被拒（修复前无响应=伪造成功）', async () => {
    const before = B.got.length;
    B.ws.send(JSON.stringify({ v: 1, type: 'receipt', payload: { id: A.insId, action: 'notify', result: 'done' } }));
    await sleep(500);
    const err = findErr(B.got.slice(before), 'BAD_RECEIPT');
    assert.ok(err, `B 未收到 BAD_RECEIPT 拒绝帧；B 收到：${JSON.stringify(B.got.slice(before))}`);
  });

  await ok('正向控制：A 自己回执同一指令 id → 正常路径（不拦截）', async () => {
    const before = B.got.length;
    A.ws.send(JSON.stringify({ v: 1, type: 'receipt', payload: { id: A.insId, action: 'notify', result: 'done' } }));
    await sleep(500);
    // A 自己回执合法：不该有 BAD_RECEIPT（B 更不该有）
    assert.ok(!findErr(A.got, 'BAD_RECEIPT'), 'A 本机回执被误拒');
    assert.ok(!findErr(B.got.slice(before), 'BAD_RECEIPT'), 'B 收到无关错误帧');
  });

  await ok('兼容性：悬垂 id（无下发记录的指令）回执 → 不拦截、按旧行为处理', async () => {
    const before = B.got.length;
    B.ws.send(JSON.stringify({ v: 1, type: 'receipt', payload: { id: 999999, result: 'done' } }));
    await sleep(500);
    assert.ok(!findErr(B.got.slice(before), 'BAD_RECEIPT'), '悬垂 id 不应被判归属不符（保持旧行为）');
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  for (const w of conns) { try { w.close(); } catch { /* 忽略 */ } }
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-6).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);