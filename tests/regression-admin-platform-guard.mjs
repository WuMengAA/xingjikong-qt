// 管理面平台身份越权 · 端到端复现/回归 —— 2026-10-26（大范围找活 · 审计面三）
//
// 守的是：/api/admin/{config,ota,cims,provision,songkeys,user-claims,broadcast-club}
// 这些平台级变更端点必须校验「调用者是否为平台超管 / 全权身份」，
// 不能让「只属于某个班的受限教师票据」也能改写服务端令牌、发全校 OTA、拉装机包。
//
// 当前（修复前）状态：isManageApi 只认「有效令牌或有效票据」，不查 isPlatformIdent。
// 而 admin/config · admin/ota · admin/cims · admin/provision 的 handler 内部
// 也都不查 isPlatformIdent / scopes===null —— 于是受限教师票据能直达这些变更操作。
//
// 本测试用一张 scope 仅限 cls-A 的受限教师票据去碰平台端点：
//   修复前会 200（越权成功，其中 provision 还会回吐含设备令牌的装机 bat）= 漏洞实证；
//   修复后必须 403（需平台身份）。
// 正向控制：静态令牌（平台全权）碰同样的端点 → 200。

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'admin-pguard-'));

const CLOUD_PORT = 8796;
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
        resolve({ status: res.statusCode, json, raw: s });
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

const cloud = startCloud();

try {
  await waitUp();

  // 受限教师票据：scope 仅 cls-A（攻击者只管一个班）
  const attackerTicket = mintViewerTicket('attacker-teacher', 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: 'cls-A' }],
  }).ticket;

  // ── 越权：受限票据碰平台级变更端点 ──
  await ok('越权·拉装机包：受限教师票据 GET /api/admin/provision → 必须 403（修复前会 200）', async () => {
    const r = await call('GET', '/api/admin/provision?type=agent&uid=class_test_2028_08', { auth: attackerTicket });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·发 OTA：受限教师票据 POST /api/admin/ota → 必须 403（修复前会 200）', async () => {
    const r = await call('POST', '/api/admin/ota', {
      auth: attackerTicket,
      body: { product: 'agent', version: '0.0.1', url: 'https://example.com/x', sha256: 'a'.repeat(64), size: 1 },
    });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·CIMS 状态：受限教师票据 GET /api/admin/cims/status → 必须 403（修复前会 200）', async () => {
    const r = await call('GET', '/api/admin/cims/status', { auth: attackerTicket });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·CIMS 下发：受限教师票据 POST /api/admin/cims/apply → 必须 403（修复前会 200）', async () => {
    const r = await call('POST', '/api/admin/cims/apply', { auth: attackerTicket, body: {} });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  await ok('越权·改配置：受限教师票据 POST /api/admin/config → 必须 403（修复前会 200 并重启云端）', async () => {
    const r = await call('POST', '/api/admin/config', {
      auth: attackerTicket, body: { wsToken: '0123456789abcdef0123' },
    });
    assert.equal(r.status, 403, `期望 403，实际 ${r.status} ${JSON.stringify(r.json)}`);
  });

  // ── 正向控制：静态令牌（平台全权）碰同样端点 → 200 ──
  await ok('正向·静态令牌 GET /api/admin/cims/status → 200', async () => {
    const r = await call('GET', '/api/admin/cims/status', { auth: VIEWER_TOKEN });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });

  await ok('正向·静态令牌 POST /api/admin/ota → 200', async () => {
    const r = await call('POST', '/api/admin/ota', {
      auth: VIEWER_TOKEN,
      body: { product: 'agent', version: '0.0.1', url: 'https://example.com/x', sha256: 'b'.repeat(64), size: 1 },
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
  });
} catch (e) {
  fail++;
  console.error(`  FAIL 脚手架异常：${e.message}`);
} finally {
  await stopCloud(cloud);
  console.log(`\n云端日志尾部：\n${(cloud.log.out + cloud.log.err).split('\n').slice(-8).join('\n')}`);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* temp */ }
}

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);
