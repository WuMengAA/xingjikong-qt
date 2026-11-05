// 绑班统一 · 端到端回归（2026-11-05 · 用户点单「绑定逻辑多路接口统一成一个核心 API」）
//
// 守的契约（bind-api.js 是唯一实现，五条 HTTP 路只是薄壳）：
//   ① 五条路绑出来的结果**同一个班码**：/api/device/bind-class、/api/class/bind、
//      设备输码 /api/device/activate、手机选班 /api/bind/confirm、班级码扫码选设备
//      /api/bind/confirm(kind=class) → 都落到同一个 canonical 班；
//   ② 无权限 → 403（旧路曾整段没有 canManageMembers，任何持票者能把设备塞进任意班）；
//   ③ 重认证门：同一张票在窗口过期后不许绑班（写操作口径与 /api/class/* 一致）；
//   ④ 设备重名：nameConflict 标记 → 改名 → 标记消失；撞名 409 + suggested；
//      dedupe:true 自动加后缀；改名后重连**不被自报主机名顶回**（applyNamePin）；
//   ⑤ 班级接入码回 matrix，且矩阵合法（size=4*version+17、三个定位图案正确）。
//
// 跑法：node tests/regression-bind-unify.mjs   （自带临时目录 + 隔离端口）

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync, cpSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SRC_DIR = join(__dirname, '..', 'src');
// 临时目录放在**工程内**（不是系统 tmp）：--reverse 模式会复制一份 src 到这里跑，
// 从复制品向上找 node_modules 时才能命中 Stelarith-cloud/node_modules（ws/sqlite 都靠它）。
const WAR_DIR = mkdtempSync(join(__dirname, '..', '.tmp-bind-unify-'));
const REVERSE = process.argv.includes('--reverse');
let CLOUD_ENTRY = join(SRC_DIR, 'index.js');

// 端口错开：--reverse 要另起一个「坏云端」，不能撞 8846 上正在用的那个。
const CLOUD_PORT = REVERSE ? 8847 : 8846;   // 8788 生产 / 8795-8841 已分配给既有 harness
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';
const REAUTH_MS = 4000;                // 压到 4s：让"过期"在测试里可等，又够跑完前面的用例

const CLASS_A = 'cls-bu01a';           // 9 位，过 isClassCodeValid（6-16）
const UID_ADMIN = 'TEST_BU_ADMIN';     // 后台直接绑
const UID_CLS = 'TEST_BU_CLS';         // /api/class/bind
const UID_CODE = 'TEST_BU_CODE';       // 设备输码
const UID_CONFIRM = 'TEST_BU_CONFIRM'; // 手机扫码确认
const UID_DUP1 = 'TEST_BU_DUP1';       // 重名样本 1
const UID_DUP2 = 'TEST_BU_DUP2';       // 重名样本 2
const UID_OTHER = 'TEST_BU_OTHER';     // 拿已用过的码去撞（票一次性）
const UID_FORBID = 'TEST_BU_FORBID';   // 越权绑定样本
const UID_SCAN = 'TEST_BU_SCAN';       // 班级码扫码选中的未分班设备
const UID_SCAN2 = 'TEST_BU_SCAN2';     // 换设备复用已绑班级码（必须被拒）
let scanCode = '';                     // ⑮ 签发、⑯⑰ 复用的班级码

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
const J = (m) => JSON.stringify(m);

/** 极简 HTTP 客户端：返回 { status, json }。 */
function call(method, path, { auth, body } = {}) {
  return new Promise((resolve, reject) => {
    const raw = body === undefined ? null : JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: `Bearer ${auth}` } : {}),
        ...(raw ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(raw) } : {}),
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
  // --reverse：把 src 复制进临时目录，把「班级码扫码选设备」这条薄壳退回修复前
  // 的 400（NEEDS_DEVICE_INPUT），再启动这个坏云端。干净版跑全绿、坏版本必须
  // 让新增的 ⑮ 变红 —— 否则这条端到端断言就是假阴性（探针断言铁律）。
  // 临时目录在工程内，复制品向上找 node_modules 能命中 Stelarith-cloud/node_modules。
  if (REVERSE) {
    const badSrc = join(WAR_DIR, 'src');
    cpSync(SRC_DIR, badSrc, { recursive: true });
    const badIndex = join(badSrc, 'index.js');
    let src = readFileSync(badIndex, 'utf8');
    const good = `      if (t.kind === 'class') {
        // 班级码扫码选设备：候选清单只来自云端 listDevices()，判权全在 classScanBind 里。
        const r = bindReply(classScanBind(t, b.uid, listDevices()));
        if (!r.body.ok) return send(r.status, { ok: false, error: r.body.error, code: r.body.code });
        return send(200, { ok: true, uid: r.body.uid, classCode: r.body.classCode, className: r.body.className });
      }`;
    assert.ok(src.includes(good), '反向验证锚点漂移：/api/bind/confirm 的 kind=class 分支已变形');
    src = src.replace(good, `      if (t.kind === 'class') {
        return send(400, { ok: false, code: 'NEEDS_DEVICE_INPUT', error: '这个绑定码要在教室电脑上输入' });
      }`);
    writeFileSync(badIndex, src);
    CLOUD_ENTRY = badIndex;
  }
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
      CLOUD_TENANTS_FILE: join(WAR_DIR, 'tenants.json'),   // 绝不写生产租户表
      STE_REAUTH_INTERVAL_MS: String(REAUTH_MS),
      STE_QML_PATCH_DISABLE: '1',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const log = { out: '', err: '' };
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
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

/* ---------- WS ---------- */

function wsClient(path) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 忽略非 JSON */ } });
  return { ws, inbox };
}
function opened(c) {
  return new Promise((resolve, reject) => {
    if (c.ws.readyState === 1) return resolve();
    c.ws.on('open', resolve);
    c.ws.on('error', reject);
  });
}
function waitFor(inbox, pred, ms = 3000, from = 0) {
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

/** 连一台被控端并注册（静态令牌路）。name 可指定，用于重名样本。 */
async function connectAgent(uid, name) {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: {
      uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9',
      name: name || `机器-${uid}`, actions: [],
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 注册没收到 registered 回执`);
  return c;
}

/** 连一个管理端 WS（票据路）—— 重认证门只认这条路径登记的"已验证"。 */
async function connectViewer(ticket) {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: { ticket } }));
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

/** 取 /api/devices 里某个 uid（找不到回 null）。 */
async function getDeviceOf(uid) {
  const r = await call('GET', '/api/devices', { auth: VIEWER_TOKEN });
  assert.equal(r.status, 200);
  return (r.json.devices || []).find((d) => d.uid === uid) || null;
}

/**
 * 矩阵合法性：尺寸公式 + 每行只含 0/1 + 三个 7×7 定位图案（含中心 3×3 实心）。
 * 只测"有 matrix 字段"是不够的 —— 编码器出错时图看着正常、手机扫不出来。
 */
function assertQrMatrix(obj) {
  assert.ok(obj, '缺回执');
  const rows = obj.matrix;
  assert.ok(Array.isArray(rows) && rows.length, `缺 matrix 字段：${J(obj)}`);
  assert.ok(Number.isInteger(obj.version) && obj.version >= 1, `version 不合法：${obj.version}`);
  assert.equal(obj.size, 4 * obj.version + 17, `尺寸公式不成立：size=${obj.size} version=${obj.version}`);
  assert.equal(rows.length, obj.size, '矩阵行数与 size 不符');
  for (const row of rows) assert.match(row, /^[01]+$/, '每行只能是 0/1 字符串');
  const at = (r, c) => rows[r][c];
  const finder = (r0, c0, tag) => {
    for (let r = 0; r < 7; r++) {
      for (let c = 0; c < 7; c++) {
        const edge = r === 0 || r === 6 || c === 0 || c === 6;
        const core = r >= 2 && r <= 4 && c >= 2 && c <= 4;
        const want = (edge || core) ? '1' : '0';
        assert.equal(at(r0 + r, c0 + c), want, `${tag} 定位图案 (${r0 + r},${c0 + c})`);
      }
    }
  };
  finder(0, 0, '左上');
  finder(0, obj.size - 7, '右上');
  finder(obj.size - 7, 0, '左下');
}

/* ---------- 开跑 ---------- */

console.log('绑班统一 · 端到端回归（bind-api 唯一核心 + 设备改名去重 + 班级码带 matrix）');

let cloud = startCloud();
const agents = [];
const viewers = [];

try {
  await waitUp();

  // ① 先建班（静态令牌 = 全权身份），四条绑班路都往这个班里绑
  const cr = await call('POST', '/api/class/create', {
    auth: VIEWER_TOKEN, body: { classCode: CLASS_A, className: '统一绑班测试班' },
  });
  assert.equal(cr.status, 200, `建班失败：${J(cr.json)}`);
  assert.equal(cr.json.classCode, CLASS_A);

  // ② 四条路绑四台设备
  await ok('① 后台直接绑 /api/device/bind-class → 200 且回 className', async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: UID_ADMIN, classCode: CLASS_A },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.ok, true);
    assert.equal(r.json.classCode, CLASS_A);
    assert.equal(r.json.className, '统一绑班测试班', `className 缺失：${J(r.json)}`);
    assert.equal(r.json.channel, 'admin');
  });

  await ok('② /api/class/bind → 同一个班码（后台另一条前缀）', async () => {
    const r = await call('POST', '/api/class/bind', {
      auth: VIEWER_TOKEN, body: { uid: UID_CLS, classCode: CLASS_A },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.classCode, CLASS_A);
    assert.equal(r.json.name, '统一绑班测试班');
  });

  await ok('③ 班级接入码 → 设备输码 /api/device/activate → 同一个班码', async () => {
    const iss = await call('POST', '/api/class/bindcode', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A },
    });
    assert.equal(iss.status, 200, `发码失败：${J(iss.json)}`);
    assert.ok(/^[A-Z0-9]{6}$/.test(iss.json.code), `接入码格式不对：${iss.json.code}`);
    const r = await call('POST', `/api/device/activate?code=${iss.json.code}&uid=${UID_CODE}`);
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.classCode, CLASS_A);
    assert.equal(r.json.className, '统一绑班测试班');
    // 幂等：同一台设备再输一次，不报错、结果一样（被控端重试路径）
    const again = await call('POST', `/api/device/activate?code=${iss.json.code}&uid=${UID_CODE}`);
    assert.equal(again.status, 200, `重输应对同设备幂等：${J(again.json)}`);
    assert.equal(again.json.classCode, CLASS_A);
    // 换一台设备用同一张已绑的码 → 必须拒（票的一次性）
    const other = await call('POST', `/api/device/activate?code=${iss.json.code}&uid=${UID_OTHER}`);
    assert.ok(other.status >= 400, `已用过的码被另一台设备复用：${J(other.json)}`);
  });

  await ok('④ 手机扫码选班 /api/bind/confirm → 同一个班码', async () => {
    const qrIssue = await call('GET', `/api/device/bind-qr?uid=${UID_CONFIRM}`);
    assert.equal(qrIssue.status, 200, `设备码签发失败：${J(qrIssue.json)}`);
    const r = await call('POST', '/api/bind/confirm', {
      body: { code: qrIssue.json.code, classCode: CLASS_A },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.classCode, CLASS_A);
  });

  await ok('⑤ 四条路的结果最后都落在**同一个班**里（不是四个班）', async () => {
    const cls = await getClassOf(CLASS_A);
    assert.ok(cls, '班不见了');
    for (const u of [UID_ADMIN, UID_CLS, UID_CODE, UID_CONFIRM]) {
      assert.ok(cls.devices.includes(u), `${u} 没进 ${CLASS_A}（实际 ${J(cls.devices)}）`);
    }
    const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
    const sameName = (r.json.classes || []).filter((c) => c.name === '统一绑班测试班');
    assert.equal(sameName.length, 1, `同名班长出了 ${sameName.length} 个：${J(sameName.map((c) => c.code))}`);
  });

  // ③ 权限与重认证
  const restricted = mintViewerTicket('teacher-bu', 120000, {
    tenantId: 'tn_default',
    schoolId: null,
    roles: ['teacher'],
    // 显式 scopes 只含一个**不相干**的班 ⇒ 同租户但没这个班的权限
    scopes: [{ type: 'class', id: 'cls-not-mine' }],
  });
  assert.ok(restricted.ok, `签票失败：${restricted.reason}`);
  const vRestricted = await connectViewer(restricted.ticket);
  viewers.push(vRestricted);

  await ok('⑥ 无班级权限的票据 → 403 FORBIDDEN（旧路这里会 200，把设备塞进别人的班）', async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: restricted.ticket, body: { uid: UID_FORBID, classCode: CLASS_A },
    });
    assert.equal(r.status, 403, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.code, 'FORBIDDEN', J(r.json));
    const cls = await getClassOf(CLASS_A);
    assert.ok(!cls.devices.includes(UID_FORBID), '越权绑定居然真的生效了');
  });

  await ok('⑦ 同一张票超期后绑班 → 403 REAUTH_REQUIRED（写操作重认证门）', async () => {
    await sleep(REAUTH_MS + 400);
    const r = await call('POST', '/api/device/bind-class', {
      auth: restricted.ticket, body: { uid: UID_FORBID, classCode: CLASS_A },
    });
    assert.equal(r.status, 403, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.code, 'REAUTH_REQUIRED', J(r.json));
  });

  // ④ 设备重名 + 改名
  const a1 = await connectAgent(UID_DUP1, '一号讲台');
  const a2 = await connectAgent(UID_DUP2, '一号讲台');
  agents.push(a1, a2);

  await ok('⑧ 两台设备同名 → GET /api/devices 两台都标 nameConflict', async () => {
    const d1 = await getDeviceOf(UID_DUP1);
    const d2 = await getDeviceOf(UID_DUP2);
    assert.ok(d1 && d2, '设备没登记上');
    assert.equal(d1.nameConflict, true, `DUP1 未标冲突：${J(d1)}`);
    assert.equal(d2.nameConflict, true, `DUP2 未标冲突：${J(d2)}`);
    // 不重名的设备不该被误标
    const other = await getDeviceOf(UID_ADMIN);
    if (other) assert.equal(other.nameConflict, false);
  });

  await ok('⑨ 改名 → 标记消失；改名到别人的名字 → 409 且带 suggested', async () => {
    const r = await call('POST', '/api/device/rename', {
      auth: VIEWER_TOKEN, body: { uid: UID_DUP1, name: '一号讲台-A' },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.name, '一号讲台-A');
    const d2 = await getDeviceOf(UID_DUP2);
    assert.equal(d2.nameConflict, false, `另一台仍被标冲突：${J(d2)}`);

    const taken = await call('POST', '/api/device/rename', {
      auth: VIEWER_TOKEN, body: { uid: UID_DUP2, name: '一号讲台-A' },
    });
    assert.equal(taken.status, 409, `status=${taken.status} ${J(taken.json)}`);
    assert.equal(taken.json.code, 'NAME_TAKEN');
    assert.equal(taken.json.suggested, '一号讲台-A-2', `建议名不对：${J(taken.json)}`);
  });

  await ok('⑩ dedupe:true → 自动加后缀，回执写明 deduped', async () => {
    const r = await call('POST', '/api/device/rename', {
      auth: VIEWER_TOKEN, body: { uid: UID_DUP2, name: '一号讲台-A', dedupe: true },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.name, '一号讲台-A-2');
    assert.equal(r.json.deduped, true);
  });

  await ok('⑪ 改名被钉住：设备重连自报旧主机名，不会把管理端的名字顶回去', async () => {
    // 模拟被控端重连：同一 uid 再 register，name 仍上报它自己的主机名
    try { a1.ws.close(); } catch { /* ignore */ }
    const again = await connectAgent(UID_DUP1, '被控端自报的名字');
    agents.push(again);
    const d1 = await getDeviceOf(UID_DUP1);
    assert.equal(d1.name, '一号讲台-A', `改名被重连顶回：${d1.name}`);
  });

  await ok('⑫ 改名无权限 → 403；空名 → 400', async () => {
    const r = await call('POST', '/api/device/rename', {
      auth: restricted.ticket, body: { uid: UID_ADMIN, name: '越权改名' },
    });
    // 这张票已过期 ⇒ 先撞重认证门；它同样是 403，这里只断言"没改成"
    assert.equal(r.status, 403, `status=${r.status} ${J(r.json)}`);
    const d = await getDeviceOf(UID_ADMIN);
    assert.notEqual(d.name, '越权改名');
    const empty = await call('POST', '/api/device/rename', {
      auth: VIEWER_TOKEN, body: { uid: UID_ADMIN, name: '   ' },
    });
    assert.equal(empty.status, 400, `status=${empty.status} ${J(empty.json)}`);
    assert.equal(empty.json.code, 'EMPTY_NAME');
  });

  // ⑤ 班级码的二维码矩阵
  await ok('⑬ 班级接入码回 matrix：size=4*version+17、三定位图案、scanUrl 指向 /bind', async () => {
    const r = await call('POST', '/api/class/bindcode', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    // 旧字段一个不少
    assert.ok(r.json.classCode && r.json.className && r.json.code && r.json.expiresAt);
    assert.ok(Number.isInteger(r.json.ttlSec) && r.json.ttlSec > 0, `ttlSec=${r.json.ttlSec}`);
    assertQrMatrix({ matrix: r.json.matrix, size: r.json.size, version: r.json.version });
    assert.match(r.json.scanUrl, /\/bind\?c=[A-Z0-9]{6}$/, `scanUrl 不对：${r.json.scanUrl}`);
    assert.ok(r.json.scanUrl.includes(r.json.code), 'scanUrl 里的码与 code 不一致');
  });

  await ok('⑭ 设备码回 matrix 同口径（同一条生成路径）', async () => {
    const r = await call('GET', `/api/device/bind-qr?uid=${UID_DUP1}`);
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assertQrMatrix({ matrix: r.json.matrix, size: r.json.size, version: r.json.version });
  });

  // ⑥ 班级码扫码选设备（2026-11-05 · 用户 m01202「扫码选设备直接绑」）
  //    纯逻辑断言证明不了 /api/bind/confirm 薄壳真把 body.uid 透传下去，必须真跑 HTTP。
  //    --reverse 模式把这条薄壳退回 400 ⇒ ⑮ 必须变红，否则新增断言是假阴性。
  // ⑮ 之前先让两台候选设备「上过线」：listDevices() 只枚举 knownDevices，
  //    凭空造的 uid 在服务端查不到，会被 DEVICE_UNKNOWN 拒（这正是设计要的：不能凭请求体绑幽灵设备）。
  const aScan = await connectAgent(UID_SCAN, '扫码候选机-A');
  const aScan2 = await connectAgent(UID_SCAN2, '扫码候选机-B');
  agents.push(aScan, aScan2);

  await ok('⑮ 班级码扫码选设备 → /api/bind/confirm 落同一个班（HTTP 薄壳真透传 uid）', async () => {
    const iss = await call('POST', '/api/class/bindcode', {
      auth: VIEWER_TOKEN, body: { classCode: CLASS_A },
    });
    assert.equal(iss.status, 200, `发班级码失败：${J(iss.json)}`);
    scanCode = iss.json.code;
    assert.ok(/^[A-Z0-9]{6}$/.test(scanCode), `码格式不对：${scanCode}`);

    // 扫码落地页先问 /api/bind/info：必须回候选设备清单、且脱敏（只有 uid/name/online）
    const info = await call('GET', `/api/bind/info?c=${scanCode}`);
    assert.equal(info.status, 200, `status=${info.status} ${J(info.json)}`);
    assert.equal(info.json.kind, 'class');
    assert.equal(info.json.classCode, CLASS_A);
    const cand = (info.json.devices || []).find((d) => d.uid === UID_SCAN);
    assert.ok(cand, `候选清单里没有 ${UID_SCAN}：${J(info.json.devices)}`);
    assert.deepEqual(Object.keys(cand).sort(), ['name', 'online', 'uid'], `候选没脱敏：${J(cand)}`);
    // 已经绑进班的四台不许出现在候选里
    for (const u of [UID_ADMIN, UID_CLS, UID_CODE, UID_CONFIRM]) {
      assert.ok(!(info.json.devices || []).some((d) => d.uid === u), `${u} 已分班却仍被列为候选`);
    }

    // 手机点确认：只带 code + uid，不带任何身份 ⇒ 凭票凭据，不能 FORBIDDEN
    const r = await call('POST', '/api/bind/confirm', {
      body: { code: scanCode, uid: UID_SCAN },
    });
    assert.equal(r.status, 200, `status=${r.status} ${J(r.json)}`);
    assert.equal(r.json.classCode, CLASS_A);
    assert.equal(r.json.uid, UID_SCAN);
    // 真落班（回执 ok 不等于落盘）
    const cls = await getClassOf(CLASS_A);
    assert.ok(cls.devices.includes(UID_SCAN), `${UID_SCAN} 没进 ${CLASS_A}：${J(cls.devices)}`);
    const dev = await getDeviceOf(UID_SCAN);
    assert.equal(dev.classCode, CLASS_A, `设备归属没写回 knownDevices：${J(dev)}`);
  });

  await ok('⑯ 同一张班级码换设备复用 → 必须拒（票一次性，不是假成功）', async () => {
    const r = await call('POST', '/api/bind/confirm', {
      body: { code: scanCode, uid: UID_SCAN2 },
    });
    assert.ok(r.status >= 400, `已用过的班级码被换设备复用：${J(r.json)}`);
    const cls = await getClassOf(CLASS_A);
    assert.ok(!cls.devices.includes(UID_SCAN2), '换设备复用居然真的绑进去了');
    // /api/bind/info 对已绑票回 alreadyBound，而不是再吐一份候选清单
    const info = await call('GET', `/api/bind/info?c=${scanCode}`);
    assert.equal(info.status, 200);
    assert.equal(info.json.alreadyBound, true, `已绑票没标 alreadyBound：${J(info.json)}`);
    assert.ok(!info.json.devices || info.json.devices.length === 0, '已绑票不该再回候选设备');
  });
} finally {
  for (const v of viewers) { try { v.ws.close(); } catch { /* ignore */ } }
  for (const a of agents) { try { a.ws.close(); } catch { /* ignore */ } }
  await stopCloud(cloud);
  rmSync(WAR_DIR, { recursive: true, force: true, maxRetries: 3 });
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exitCode = 1;
