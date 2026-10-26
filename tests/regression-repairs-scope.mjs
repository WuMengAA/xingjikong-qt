// 报修按班授权 · 端到端复现/回归 —— 2026-10-26（大范围找活 · 审计面三副产物）
//
// 守的是：/api/repairs 系列（列表 / 单条 / PATCH / DELETE）必须按调用者身份过滤——
// 受限身份（scope 仅某班）只能看/改/删**本班设备**的报修单；无班级归属的设备
// 受限身份一律不可见（与 recordings 同口径）。
//
// 修复前：GET 列表全校可见、单条/PATCH/DELETE 任意 id 均可（200）。
// 修复后：越权路径必须 403 / 列表过滤；静态令牌（全权）不受影响。

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync, existsSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const WAR_DIR = mkdtempSync(join(tmpdir(), 'rep-scope-'));

const CLOUD_PORT = 8797;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

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
        let json = null; try { json = JSON.parse(s); } catch { json = { raw: s }; }
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
      // ⚠️ 必设：repairs 落盘路径必须指到 Temp，否则会直写工程根 repairs.json
      //    （2026-10-26 真事故：测试曾把假报修单写进生产同文件，见当日归档）。
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
    } catch { /* not up */ }
    await sleep(250);
  }
  throw new Error('云端未能在超时内起来');
}

function stopCloud(c) {
  return new Promise((resolve) => {
    if (!c || c.child.exitCode !== null) return resolve();
    c.child.once('exit', () => resolve());
    try { c.child.kill(); } catch { resolve(); }
    setTimeout(resolve, 4000);
  });
}

async function connectAgent(uid, classCode) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}/ws/agent`);
  await new Promise((res, rej) => { ws.on('open', res); ws.on('error', rej); });
  ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: { uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9', name: `机-${uid}`, actions: [], classCode: classCode || '', className: classCode ? `机-${uid}` : '' },
  }));
  await new Promise((res) => setTimeout(res, 400));
  return ws;
}

async function createRepair(uid, symptom) {
  const r = await call('POST', '/api/repairs', { auth: VIEWER_TOKEN, body: { uid, symptom, severity: 'low' } });
  assert.equal(r.status, 201, `建单失败：${JSON.stringify(r.json)}`);
  return r.json.ticket.id;
}

async function classCodeByName(name) {
  const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
  assert.equal(r.status, 200, JSON.stringify(r.json));
  const hit = (r.json.classes || []).find((c) => c.name === name);
  assert.ok(hit, `班「${name}」未建成：${JSON.stringify(r.json.classes)}`);
  return hit.code;
}

// 宿主污染护栏：测试全程必须不触碰工程根 repairs.json（隔离事故回归护栏，2026-10-26）
const HOST_REPAIRS = join(__dirname, '..', 'repairs.json');
let hostRepairsBefore = '';
try { hostRepairsBefore = existsSync(HOST_REPAIRS) ? readFileSync(HOST_REPAIRS, 'utf8') : '<absent>'; } catch { hostRepairsBefore = '<absent>'; }

const cloud = startCloud();
const agents = [];
const ids = {};

try {
  await waitUp();

  // 建班：注意云端按 className 派生 canonical code（传入的 seed code 不保留），
  // 必须用建班后的真实 code 做 scopes 与设备归属，否则全部失配。
  let codeA = '', codeB = '';
  await ok('脚手架·建「甲班」', async () => {
    const r = await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: 'seed-a', className: '甲班' } });
    assert.equal(r.status, 200, `建班失败：${r.status} ${JSON.stringify(r.json)}`);
    codeA = (await classCodeByName('甲班'));
  });
  await ok('脚手架·建「乙班」', async () => {
    const r = await call('POST', '/api/class/create', { auth: VIEWER_TOKEN, body: { classCode: 'seed-b', className: '乙班' } });
    assert.equal(r.status, 200, `建班失败：${r.status} ${JSON.stringify(r.json)}`);
    codeB = (await classCodeByName('乙班'));
  });
  const agNo = await connectAgent('dev-no', null);
  const agB = await connectAgent('dev-B', codeB);
  const agA = await connectAgent('dev-A', codeA);
  agents.push(agNo, agB, agA);

  // 三张报修单：无班 / B 班 / A 班（都用全权静态令牌建）
  ids.no = await createRepair('dev-no', '无班机异响');
  ids.B = await createRepair('dev-B', 'B 班屏幕闪烁');
  ids.A = await createRepair('dev-A', 'A 班投影无信号');

  const attacker = mintViewerTicket('atk-teacher', 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: codeA }],
  }).ticket;

  // ── 越权面：受限教师（只属 A 班）──
  await ok('越权·列表过滤：attacker 只见 A 班单，不见 B 班/无班单', async () => {
    const r = await call('GET', '/api/repairs', { auth: attacker });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    const got = r.json.repairs.map((x) => x.id);
    assert.ok(got.includes(ids.A), `应含本班单 ${ids.A}，实际 ${JSON.stringify(got)}`);
    assert.ok(!got.includes(ids.B), `不应含 B 班单 ${ids.B}`);
    assert.ok(!got.includes(ids.no), `不应含无班单 ${ids.no}`);
  });

  await ok('越权·单条读 B 班单 → 必须 403（修复前会 200）', async () => {
    const r = await call('GET', `/api/repairs/${ids.B}`, { auth: attacker });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·PATCH B 班单 → 必须 403（修复前会 200）', async () => {
    const r = await call('PATCH', `/api/repairs/${ids.B}`, { auth: attacker, body: { status: 'open', note: '偷改他人' } });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·DELETE B 班单 → 必须 403（修复前会 200）', async () => {
    const r = await call('DELETE', `/api/repairs/${ids.B}`, { auth: attacker });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  // ── 正向面 ──
  await ok('正向·PATCH 自己 A 班单 → 200', async () => {
    const r = await call('PATCH', `/api/repairs/${ids.A}`, { auth: attacker, body: { status: 'open', note: '我班设备' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });

  await ok('正向·全权静态令牌列表见全部 3 单', async () => {
    const r = await call('GET', '/api/repairs', { auth: VIEWER_TOKEN });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.repairs.length, 3, `应 3 单，实际 ${r.json.repairs.length}`);
  });

  await ok('正向·全权静态令牌 DELETE B 班单 → 200', async () => {
    const r = await call('DELETE', `/api/repairs/${ids.B}`, { auth: VIEWER_TOKEN });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  for (const a of agents) { try { a.close(); } catch { /* ignore */ } }
  await stopCloud(cloud);
  // 隔离护栏断言（放 finally：任何环节污染都抓住）
  try {
    const now = existsSync(HOST_REPAIRS) ? readFileSync(HOST_REPAIRS, 'utf8') : '<absent>';
    if (now !== hostRepairsBefore) {
      fail++;
      console.error(`  FAIL 宿主文件污染：工程根 repairs.json 被测试改写！\n    before=${hostRepairsBefore.slice(0, 80)}\n    after =${now.slice(0, 80)}`);
    }
  } catch (e) { fail++; console.error(`  FAIL 护栏自身异常：${e.message}`); }
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-6).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* temp */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);