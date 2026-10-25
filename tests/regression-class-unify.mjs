// 班级真源统一 · 端到端回归（2026-10-20）
//
// 守的是"设备端只有一个入口、云端只有一个班级身份"这条线：
//   ① 管理台按班名开码 → 设备敲码       → 班级码 A
//   ② 同一台/另一台设备按同名班绑        → 必须还是 A（不能长出新码）
//   ③ 站点式接入码 XJK-… → **已废弃**（2026-10-25 收归我方）→ 必须明确拒绝，不代兑换
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
const VIEWER_TOKEN = 'test-viewer-token';
// 站点签发的旧码形：只留一个样本，用于验证「旧路确实被拒绝」（云端不再跨服务兑换）。
const SITE_CODE = 'XJK-TEST-001';

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
      // 2026-10-25：不再给云端喂 STE_SITE_URL / STE_SITE_ACTIVATE_TIMEOUT_MS ——
      // 那两项原本只服务「XJK 码代兑换站点」，接入码收归我方后该路已删（config.js 同步移除）。
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


// ── 开跑 ─────────────────────────────────────────────────────────────
console.log('班级真源统一 · 端到端回归');

let cloud = startCloud();
let codeA = '';
let bindCode = '';

try {
  await waitUp();

  // ① 建班（班级真源在云端；面板发码前班必须已存在 —— 面板里是从列表里选班）
  await (async () => {
    const r = await call('POST', '/api/class/create', {
      auth: VIEWER_TOKEN, body: { className: '高二(3)班' },
    });
    ok('班级面板建班成功，回 canonical 班级码', () => {
      assert.equal(r.status, 200, `HTTP ${r.status} ${JSON.stringify(r.json)}`);
      assert.equal(r.json.ok, true);
      assert.match(r.json.classCode, /^cls-[0-9a-f]{10}$/);
    });
    codeA = r.json.classCode;
  })();

  // ② 班级面板发接入码（2026-10-25 收归我方：POST /api/class/bindcode）
  await (async () => {
    const r = await call('POST', '/api/class/bindcode', {
      auth: VIEWER_TOKEN, body: { classCode: codeA },
    });
    ok('班级面板发接入码 → 6 位无歧义、绑到该班、带 24h 有效期', () => {
      assert.equal(r.status, 200, `HTTP ${r.status} ${JSON.stringify(r.json)}`);
      assert.equal(r.json.ok, true);
      assert.equal(r.json.classCode, codeA, '接入码没绑到目标班');
      // 字符集去掉 I/O/0/1（老师照着念不会错）—— 断言到这个精度才算守住约定
      assert.match(r.json.code, /^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}$/, '接入码不是 6 位无歧义字符');
      assert.ok(r.json.expiresAt > Date.now(), '有效期不是未来时间');
      assert.ok(r.json.ttlSec > 23 * 3600, `TTL 不足 24h（${r.json.ttlSec}s）`);
    });
    bindCode = r.json.code;
  })();

  // ③ 设备敲这枚码
  await (async () => {
    const r = await call('POST', `/api/device/activate?code=${bindCode}&uid=dev-A`, { body: {} });
    ok('设备敲接入码 → 得到同一个班级码', () => {
      assert.equal(r.status, 200, `HTTP ${r.status} ${JSON.stringify(r.json)}`);
      assert.equal(r.json.classCode, codeA);
      assert.equal(r.json.className, '高二(3)班');
    });
  })();

  // ④ 接入码必须**一次性**：同一枚码用在第二台设备上要被拒（否则整个班能被路人领走）
  await (async () => {
    const r = await call('POST', `/api/device/activate?code=${bindCode}&uid=dev-OTHER`, { body: {} });
    ok('同一枚接入码绑第二台设备 → 明确拒绝（一次性）', () => {
      assert.ok(r.status >= 400, `竟然回了 ${r.status}`);
      assert.equal(r.json.ok, false);
    });
  })();

  // ⑤ 另一台设备按**同名班**绑定（管理端老路）→ 必须复用它，不能新长一个
  await (async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: 'dev-B', className: '高二(3)班' },
    });
    ok('按同名班绑定另一台设备 → 复用同一个班级码（不新造）', () => {
      assert.equal(r.status, 200, `HTTP ${r.status}`);
      assert.equal(r.json.classCode, codeA, '同一个班长出了第二个码');
    });
  })();

  // ⑥ 站点 XJK 接入码已废弃（2026-10-25 收归我方）：必须明确拒绝，不许再代兑换
  await (async () => {
    const r = await call('POST', `/api/device/activate?code=${SITE_CODE}&uid=dev-C`, { body: {} });
    ok('站点接入码 XJK-… 已废弃 → 明确拒绝（不静默、不代兑换）', () => {
      assert.ok(r.status >= 400, `竟然回了 ${r.status}（旧路又长回来了？）`);
      assert.equal(r.json.ok, false);
      assert.ok(String(r.json.error || '').length > 0);
    });
  })();

  // ⑦ 班级目录只能有**一个**班：单真源，站点来源已废，不可能再长出第二套码
  await (async () => {
    const r = await call('GET', '/api/classes/dir', { auth: VIEWER_TOKEN });
    ok('班级目录只有一个班（单真源，无站点班）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.ok, true);
      const codes = r.json.classes.map((c) => c.code).sort();
      assert.deepEqual(codes, [codeA]);
    });
  })();

  // ⑧ 重启：班级目录必须原样还在
  await stopCloud(cloud);
  cloud = startCloud();
  await waitUp();

  await (async () => {
    const r = await call('GET', '/api/classes/dir', { auth: VIEWER_TOKEN });
    ok('重启后班级目录原样恢复（码/名都在）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.classes.length, 1);
      const c1 = r.json.classes.find((c) => c.code === codeA);
      assert.ok(c1, '重启后班级丢了');
      assert.equal(c1.name, '高二(3)班');
    });
  })();

  await (async () => {
    const r = await call('POST', '/api/device/bind-class', {
      auth: VIEWER_TOKEN, body: { uid: 'dev-E', className: '高二(3)班' },
    });
    ok('重启后同名解析仍还给原来的码（不派生第二套）', () => {
      assert.equal(r.json.classCode, codeA, '重启后又长出了新码');
    });
  })();

  await (async () => {
    const r = await call('GET', '/api/classes', { auth: VIEWER_TOKEN });
    ok('重启后 /api/classes 仍列出班级（不是空列表）', () => {
      assert.equal(r.status, 200);
      assert.equal(r.json.classes.length, 1, `列了 ${r.json.classes.length} 个班`);
      assert.ok(r.json.classes.every((c) => typeof c.code === 'string' && c.code));
    });
  })();

} catch (e) {
  fail++;
  console.error(`  FAIL 运行期异常：${e.message}`);
  console.error('--- cloud stdout ---\n' + (cloud.log.out || ''));
  console.error('--- cloud stderr ---\n' + (cloud.log.err || ''));
} finally {
  await stopCloud(cloud);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 忽略 */ }
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
process.exit(fail === 0 ? 0 : 1);
