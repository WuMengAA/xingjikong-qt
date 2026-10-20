// 班级真源统一 · 端到端回归（2026-10-20）
//
// 守的是"设备端只有一个入口、云端只有一个班级身份"这条线：
//   ① 管理台按班名开码 → 设备敲码       → 班级码 A
//   ② 同一台/另一台设备按同名班绑        → 必须还是 A（不能长出新码）
//   ③ 站点式接入码 XJK-… → 云端代兑换    → 站点的 class_id 成为该班身份（或别名）
//   ④ 云端重启后                        → 班级与别名原样还在，同名解析还给 A
//
// 为什么要起真服务：班级目录是**落盘 + 进程级缓存**，纯单测（unit-class-dir）验不了
// "HTTP 入口有没有真的过解析器"、"重启后 registry 索引会不会空掉"。
//
// 跑法：node tests/regression-class-unify.mjs      （自动挑空闲端口，自带临时目录）

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-unify-'));

const CLOUD_PORT = 8797;
const SITE_PORT = 8796;
const VIEWER_TOKEN = 'test-viewer-token';
const SITE_CODE = 'XJK-TEST-001';
const SITE_CLASS_ID = 'class_01';
const SITE_CLASS_NAME = '高一(1)班';

let pass = 0; let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

/** 极简 HTTP 客户端：返回 { status, json } */
function call(method, path, { auth, body } = {}) {
  return new Promise((resolve, reject) => {
    const payload = body === undefined ? null : Buffer.from(JSON.stringify(body));
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: `Bearer ${auth}` } : {}),
        ...(payload ? { 'content-type': 'application/json', 'content-length': payload.length } : {}),
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
    req.end(payload || undefined);
  });
}

function startCloud() {
  const child = spawn(process.execPath, [CLOUD_ENTRY], {
    cwd: WAR_DIR,
    env: {
      ...process.env,
      CLOUD_WS_PORT: String(CLOUD_PORT),
      CLOUD_WS_HOST: '127.0.0.1',
      CLOUD_WS_TOKEN: 'test-dev-token',
      CLOUD_VIEWER_TOKEN: VIEWER_TOKEN,
      CLOUD_EVENTS_FILE: join(WAR_DIR, 'events.log'),
      CLOUD_QUEUE_DB_FILE: join(WAR_DIR, 'queue.db'),
      CLOUD_INTERACT_DB_FILE: join(WAR_DIR, 'interact.db'),
      CLOUD_CLASS_DIR_DB_FILE: join(WAR_DIR, 'class-dir.db'),
      STE_SITE_URL: `http://127.0.0.1:${SITE_PORT}`,
      STE_SITE_ACTIVATE_TIMEOUT_MS: '3000',
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
    await new Promise((r) => setTimeout(r, 250));
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

/** 假站点：只实现 /api/device/activate，回站点自己的 class_id / className（模拟 stelarith 站点） */
function startFakeSite() {
  const srv = http.createServer((req, res) => {
    const u = new URL(req.url, `http://127.0.0.1:${SITE_PORT}`);
    if (u.pathname !== '/api/device/activate') {
      res.writeHead(404, { 'content-type': 'application/json' });
      return res.end(JSON.stringify({ ok: false, error: 'not found' }));
    }
    const code = u.searchParams.get('code') || '';
    if (code !== SITE_CODE) {
      res.writeHead(404, { 'content-type': 'application/json' });
      return res.end(JSON.stringify({ ok: false, error: '接入码无效' }));
    }
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({
      ok: true, classId: SITE_CLASS_ID, classCode: SITE_CLASS_ID,
      className: SITE_CLASS_NAME, uid: u.searchParams.get('uid'),
    }));
  });
  return new Promise((r) => srv.listen(SITE_PORT, '127.0.0.1', () => r(srv)));
}

// ── 开跑 ─────────────────────────────────────────────────────────────
console.log('班级真源统一 · 端到端回归');

const site = await startFakeSite();
let cloud = startCloud();
let codeA = '';
let codeFromSite = '';

try {
  await waitUp();

  // ① 管理台按班名开码（不给班级码，只有名字 —— 走 resolveClassCode 派生）
  let issued = null;
  await (async () => {
    const r = await call('POST', '/api/admin/bindcode', {
      auth: VIEWER_TOKEN, body: { className: '高二(3)班' },
    });
    ok('管理台按班名开码成功，回班级码', () => {
      assert.equal(r.status, 200, `HTTP ${r.status}`);
      assert.equal(r.json.ok, true);
      assert.match(r.json.classCode, /^cls-[0-9a-f]{10}$/);
    });
    issued = r.json;
  })();
  codeA = issued.classCode;

  // ② 设备敲这枚码
  await (async () => {
    const r = await call('POST', `/api/device/activate?code=${issued.code}&uid=dev-A`, { body: {} });
    ok('设备敲码绑定 → 得到同一个班级码', () => {
      assert.equal(r.status, 200, `HTTP ${r.status} ${JSON.stringify(r.json)}`);
      assert.equal(r.json.classCode, codeA);
      assert.equal(r.json.className, '高二(3)班');
    });
  })();

  // ③ 另一台设备按**同名班**绑定（管理端老路）→ 必须复用它，不能新长一个
  await (async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: 'dev-B', className: '高二(3)班' },
    });
    ok('按同名班绑定另一台设备 → 复用同一个班级码（不新造）', () => {
      assert.equal(r.status, 200, `HTTP ${r.status}`);
      assert.equal(r.json.classCode, codeA, '同一个班长出了第二个码');
    });
  })();

  // ④ 站点接入码走云端代兑换
  await (async () => {
    const r = await call('POST', `/api/device/activate?code=${SITE_CODE}&uid=dev-C`, { body: {} });
    ok('站点接入码 XJK-… 由云端兑换 → 班级码=站点 class_id、名字对齐', () => {
      assert.equal(r.status, 200, `HTTP ${r.status} ${JSON.stringify(r.json)}`);
      assert.equal(r.json.classCode, SITE_CLASS_ID);
      assert.equal(r.json.className, SITE_CLASS_NAME);
      assert.equal(r.json.source, 'site');
    });
    codeFromSite = SITE_CLASS_ID;
  })();

  // ⑤ 站点班与云端班必须互不串（不同名字 → 两个班）
  await (async () => {
    const r = await call('GET', '/api/classes/dir', { auth: VIEWER_TOKEN });
    ok('班级目录可查且只有两个班（云端派生的 + 站点登记的）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.ok, true);
      const codes = r.json.classes.map((c) => c.code).sort();
      assert.deepEqual(codes, [codeA, codeFromSite].sort());
    });
  })();

  // ⑥ 站点码的班按名字再解析 → 命中 class_01，不新造
  await (async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: 'dev-D', className: SITE_CLASS_NAME },
    });
    ok('站点班的同名绑定 → 归一到 class_01（别名体系生效）', () => {
      assert.equal(r.status, 200, `HTTP ${r.status}`);
      assert.equal(r.json.classCode, SITE_CLASS_ID);
    });
  })();

  // ⑦ 重启：班级目录与别名必须原样还在
  await stopCloud(cloud);
  cloud = startCloud();
  await waitUp();

  await (async () => {
    const r = await call('GET', '/api/classes/dir', { auth: VIEWER_TOKEN });
    ok('重启后班级目录原样恢复（码/名/别名都在）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.classes.length, 2);
      const site1 = r.json.classes.find((c) => c.code === SITE_CLASS_ID);
      assert.ok(site1, '重启后站点班丢了');
      assert.equal(site1.name, SITE_CLASS_NAME);
    });
  })();

  await (async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: 'dev-E', className: '高二(3)班' },
    });
    ok('重启后同名解析仍还给原来的码（不派生第三套）', () => {
      assert.equal(r.json.classCode, codeA, '重启后又长出了新码');
    });
  })();

  await (async () => {
    const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
    ok('重启后 /api/classes 仍列出班级（不是空列表）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.classes.length, 2, `列了 ${r.json.classes.length} 个班`);
      assert.ok(r.json.classes.every((c) => typeof c.code === 'string' && c.code));
    });
  })();

  // ⑧ 错误路径不能假装成功
  await (async () => {
    const r = await call('POST', '/api/device/activate?code=XJK-NOPE-999&uid=dev-F', { body: {} });
    ok('站点拒绝的接入码 → 明确失败（不静默成功）', () => {
      assert.ok(r.status >= 400, `竟然回了 ${r.status}`);
      assert.equal(r.json.ok, false);
      assert.ok(String(r.json.error || '').length > 0);
    });
  })();
} catch (e) {
  fail++;
  console.error(`  FAIL 运行期异常：${e.message}`);
  console.error('--- cloud stdout ---\n' + (cloud.log.out || ''));
  console.error('--- cloud stderr ---\n' + (cloud.log.err || ''));
} finally {
  await stopCloud(cloud);
  await new Promise((r) => site.close(r));
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 忽略 */ }
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
process.exit(fail === 0 ? 0 : 1);
