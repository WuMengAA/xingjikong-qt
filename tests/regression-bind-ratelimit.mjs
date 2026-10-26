// OOBE 绑定码洪水限流 · 端到端回归 —— 2026-10-27（WS 审计候选面 · 纯加固）
//
// 守的是：/api/device/bind-qr（匿名签发绑定码）60s/IP/6 次、
//         /api/bind/info（凭码查班级清单）60s/IP/60 次。
// 限流不改变任何正常流程语义：正常 OOBE 换码重试 1-3 次/分钟、手机选班页 <5 次/分钟。
// 断言：正常流（限额内）全 200 → 超限 429 + retry-after → 窗口滑过（sleep 61s）恢复。
// ⚠️ 全部请求来自 127.0.0.1 共享一个 IP 桶：测试要精确控制次数（bind-qr 只打 6 次正常
//    +1 次超限；bind/info 打 60 次正常 +1 次超限）。恢复断言单独 sleep 61s 复验。

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const WAR_DIR = mkdtempSync(join(tmpdir(), 'bind-rl-'));

const CLOUD_PORT = 8801;
const DEV_TOKEN = 'test-dev-token';
const VIEWER_TOKEN = 'test-viewer-token';

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function call(method, path, { auth } = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: auth ? { authorization: `Bearer ${auth}` } : {},
      timeout: 8000,
    }, (res) => {
      let s = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { s += c; });
      res.on('end', () => {
        let json = null;
        try { json = JSON.parse(s); } catch { json = { raw: s }; }
        resolve({ status: res.statusCode, headers: res.headers, json });
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

/* ─────────────── 开始 ─────────────── */

const cloud = startCloud();

try {
  await waitUp();

  // ── bind-qr：正常流 6 次全 200 → 第 7 次 429 ──
  await ok('bind-qr 正常流：连续 6 次签发 → 全 200', async () => {
    for (let i = 0; i < 6; i++) {
      const r = await call('GET', '/api/device/bind-qr?uid=rl-dev-' + i);
      assert.equal(r.status, 200, `第 ${i + 1} 次期望 200，实际 ${r.status} ${JSON.stringify(r.json)}`);
      assert.ok(r.json.code && r.json.code.length === 6, `应返回 6 位码：${JSON.stringify(r.json)}`);
    }
  });

  await ok('bind-qr 超限：第 7 次 → 429 + retry-after=60 + 人话', async () => {
    const r = await call('GET', '/api/device/bind-qr?uid=rl-dev-over');
    assert.equal(r.status, 429, `期望 429，实际 ${r.status} ${JSON.stringify(r.json)}`);
    assert.equal(r.headers['retry-after'], '60', '应带 retry-after 头');
    assert.ok(String(r.json.error).includes('频繁'), `应有人话：${JSON.stringify(r.json)}`);
  });

  await ok('bind-qr 超限后仍 429（窗口未过）', async () => {
    const r = await call('GET', '/api/device/bind-qr?uid=rl-dev-over2');
    assert.equal(r.status, 429, `期望 429，实际 ${r.status}`);
  });

  // ── bind/info：正常流 60 次全 200 → 第 61 次 429 ──
  // 先签一个有效码（bind-qr 窗口此时已超限，但换 uid 不换 IP 仍会被限——不行！）
  // 所以：bind/info 的正常流用「无效码」打（getTicket 查不到 → 410，但**请求本身已过限流**）。
  // 断言 410（过限流到达业务）而非 200，限流语义与码有效性解耦，测试更稳。
  await ok('bind/info 正常流：连续 60 次（无效码也到业务层=410 而非 429）', async () => {
    for (let i = 0; i < 60; i++) {
      const r = await call('GET', '/api/bind/info?c=RLINFO' + (100000 + i));
      assert.equal(r.status, 410, `第 ${i + 1} 次期望 410（过限流），实际 ${r.status} ${JSON.stringify(r.json)}`);
    }
  });

  await ok('bind/info 超限：第 61 次 → 429 + retry-after=60', async () => {
    const r = await call('GET', '/api/bind/info?c=RLINFOOVER');
    assert.equal(r.status, 429, `期望 429，实际 ${r.status} ${JSON.stringify(r.json)}`);
    assert.equal(r.headers['retry-after'], '60', '应带 retry-after 头');
  });

  // ── 窗口滑过恢复（等 61s 两个窗口一起重开）──
  await ok('窗口恢复：sleep 61s 后 bind-qr 重新 200、bind/info 重新 410', async () => {
    await sleep(61_000);
    const a = await call('GET', '/api/device/bind-qr?uid=rl-dev-recover');
    assert.equal(a.status, 200, `bind-qr 期望 200，实际 ${a.status} ${JSON.stringify(a.json)}`);
    const b = await call('GET', '/api/bind/info?c=RLINFORECOVER');
    assert.equal(b.status, 410, `bind/info 期望 410，实际 ${b.status} ${JSON.stringify(b.json)}`);
  });

  // ── 其它匿名端点不受影响（防误伤：同 IP 的其它路径无限流）──
  await ok('误伤防护：同 IP 的 /api/bind/confirm 与 /api/device/bind-status 不受限流影响', async () => {
    const r1 = await call('POST', '/api/bind/confirm');
    const r2 = await call('GET', '/api/device/bind-status?code=XXXXXX&uid=rl-x');
    // confirm 无 body → 业务 400 或 410（不是 429）；bind-status 无效码 → 410（不是 429）
    assert.notEqual(r1.status, 429, `confirm 不应 429，实际 ${r1.status}`);
    assert.notEqual(r2.status, 429, `bind-status 不应 429，实际 ${r2.status}`);
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-5).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 临时目录 */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);