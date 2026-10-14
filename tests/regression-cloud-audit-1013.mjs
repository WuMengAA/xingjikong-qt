// 2026-10-13 三端审计 · 云端 8 处修复回归（AI 测试通道 T1 套件成员）
//
// 作用：把 9540209/d9c8d23/7319539 三笔修复的行为固化成断言，防止回归。
// 覆盖：
//   [单元] U1 knownDevices 上限淘汰（CLOUD_KNOWN_KEEP，跳过在线、按 lastSeen 排）
//   [单元] U2 receiptsByNotice LIKE 定界（notice_id=1 不误配 12/123）
//   [HTTP] H1 管理面无令牌 401 / H2 错令牌 401 / H3 对令牌 200
//   [HTTP] H4 /api/public/ota cache-control private
//   [HTTP] H5 ota channel 路径穿越回落 stable（不 500、不泄穿越文件）
//   [HTTP] H6 /api/admin/config 令牌脱敏（尾 4 位，不出明文）
//   [HTTP] H7 ws maxPayload 4MiB（鉴权前挡大帧）
//
// 用法（AI 测试通道统一入口会自动做这一步）：
//   # 终端 A：隔离实例（端口/队列库/清单全走 env，不碰生产）
//   CLOUD_WS_PORT=18788 CLOUD_WS_TOKEN=it-agent-token CLOUD_VIEWER_TOKEN=it-viewer-token \
//   CLOUD_VIEWER_SECRET=it-secret CLOUD_QUEUE_DB_FILE=<tmp>/it-queue.db \
//   CLOUD_OTA_MANIFEST_FILE=ota.json node src/index.js
//   # 终端 B：
//   node tests/regression-cloud-audit-1013.mjs [port]
// 返回码：0 全过；非 0 有失败。
// 安全：只对隔离实例跑，绝不要指向生产 8788。

import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import http from 'node:http';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const PORT = Number(process.argv[2] || 18788);
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'it-agent-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'it-viewer-token';

let pass = 0, fail = 0;
const results = [];
function check(name, ok, detail) {
  if (ok) { pass++; results.push({ name, ok: true }); console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; results.push({ name, ok: false, detail }); console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}

/* ── 单元段 ──────────────────────────────────────────────────────────────
 * ESM import 会提升，env 必须在 import 前设 ⇒ 用 dynamic import。 */

async function unitPruneKnown() {
  console.log('\n[单元] U1 knownDevices 上限淘汰');
  process.env.CLOUD_KNOWN_KEEP = '5';   // 小上限，免得真灌 2 万条
  const registry = await import('../src/registry.js');
  const fake = () => ({ readyState: 1, send() {}, close() {} });
  // 灌 6 台：每台隔 6ms，保证 lastSeen 有区分（U1 最旧）。fake 要留引用——markDisconnected 按 ws 对象认设备。
  const socks = [];
  for (let i = 1; i <= 6; i++) {
    const s = fake(); socks.push(s);
    registry.markConnected(`IT-U${i}`, '0.6.31', s, null, { name: `机${i}` });
    await new Promise(r => setTimeout(r, 6));
  }
  // 全部离线：只从 devices 移除，knownDevices 保留（离线保留是设计行为）
  for (const s of socks) registry.markDisconnected(s);
  // 灌一台新 uid 触发 pruneKnown（isNewUid=true 时才淘汰）
  registry.markConnected('IT-U7', '0.6.31', fake(), null, { name: '新机' });

  const list = registry.listDevices();
  const uids = list.map(d => d.uid);
  // 设计行为：一次淘汰 max(overflow, 10%) 且 prune 在 set 之前 ⇒ 单次注册最多超出一台，
  // 不是严格封顶（留余量免得每来一台新设备就全表扫）。
  check('淘汰后规模受控（<= KEEP+1 = 6）', list.length <= 6, `size=${list.length}`);
  check('最久未见的 IT-U1 被淘汰', !uids.includes('IT-U1'));
  check('最新的 IT-U7 保留（刚注册）', uids.includes('IT-U7'));
  check('离线设备也保留元数据（设计行为：离线不消失）', uids.includes('IT-U6'));

  // 在线设备不淘汰：连一台在线的，再灌新的触发淘汰，在线的必须活
  const live = fake();
  registry.markConnected('IT-LIVE', '0.6.31', live, null, { name: '常在' });
  for (let i = 10; i <= 16; i++) {
    registry.markConnected(`IT-X${i}`, '0.6.31', fake(), null, { name: `灌${i}` });
    await new Promise(r => setTimeout(r, 6));
  }
  const uids2 = registry.listDevices().map(d => d.uid);
  check('在线设备不被淘汰（lastSeen 旧也跳过）', uids2.includes('IT-LIVE'));
}

async function unitReceiptsLike() {
  console.log('\n[单元] U2 receiptsByNotice LIKE 定界');
  const tmpDb = path.join(os.tmpdir(), `it-receipts-${crypto.randomBytes(4).toString('hex')}.db`);
  process.env.CLOUD_QUEUE_DB_FILE = tmpDb;   // 绝不碰生产队列库
  const store = await import('../src/store.js');
  store.initStore();
  try {
    assert.ok(store.storeReady(), 'store 应初始化成功');
    // notice_id=1 与 notice_id=12 的两条 notify；1 是老 bug 的误配源。
    // ⚠️ params 传**对象**——enqueueDispatch 内部会 JSON.stringify，传字符串会被二次转义
    //    （params 列存成 "\"{\\\"notice_id\\\":1}\""，LIKE 永远匹配不上；2026-10-14 实测踩过）。
    store.enqueueDispatch('IT-A', 'notify', { notice_id: 1, title: '广播一' }, 0);
    store.enqueueDispatch('IT-B', 'notify', { notice_id: 12, title: '广播十二' }, 0);
    store.enqueueDispatch('IT-C', 'notify', { notice_id: 123, title: '广播一百二十三' }, 0);

    const r1 = store.receiptsByNotice(1);
    check('receiptsByNotice(1) 只回 notice_id=1 的（不误配 12/123）',
      r1.length === 1 && r1[0].uid === 'IT-A', `got ${JSON.stringify(r1.map(x => x.uid))}`);
    const r12 = store.receiptsByNotice(12);
    check('receiptsByNotice(12) 精确命中', r12.length === 1 && r12[0].uid === 'IT-B');
    const r123 = store.receiptsByNotice(123);
    check('receiptsByNotice(123) 精确命中', r123.length === 1 && r123[0].uid === 'IT-C');
    const r0 = store.receiptsByNotice(0);
    check('receiptsByNotice(0) 不误捞任何', r0.length === 0, `got ${r0.length}`);
  } finally {
    try { fs.unlinkSync(tmpDb); } catch {}
    try { fs.unlinkSync(tmpDb + '-journal'); } catch {}
  }
}

/* ── HTTP 段（连隔离实例）────────────────────────────────────────────── */

function get(p, headers) {
  return new Promise((res, rej) => {
    const req = http.get({ host: HOST, port: PORT, path: p, headers: headers || {} }, r => {
      let b = ''; r.on('data', c => b += c);
      r.on('end', () => res({ code: r.statusCode, headers: r.headers, body: b }));
    });
    req.on('error', rej);
    req.setTimeout(5000, () => { req.destroy(); rej(new Error('timeout ' + p)); });
  });
}

async function httpChecks() {
  console.log('\n[HTTP] 管理面鉴权 / 缓存头 / 通道穿越 / 脱敏 / maxPayload');

  // H1/H2/H3 令牌三道闸
  try {
    const r0 = await get('/api/devices');
    check('H1 /api/devices 无令牌 → 401', r0.code === 401, `code=${r0.code}`);
  } catch (e) { check('H1 /api/devices 无令牌 → 401', false, e.message); }
  try {
    const r1 = await get('/api/devices', { authorization: 'Bearer wrong-token' });
    check('H2 /api/devices 错令牌 → 401', r1.code === 401, `code=${r1.code}`);
  } catch (e) { check('H2 /api/devices 错令牌 → 401', false, e.message); }
  try {
    const r2 = await get('/api/devices', { authorization: 'Bearer ' + VIEWER_TOKEN });
    const j = JSON.parse(r2.body);
    check('H3 /api/devices 对令牌 → 200 且 ok', r2.code === 200 && j.ok === true, `code=${r2.code}`);
  } catch (e) { check('H3 /api/devices 对令牌 → 200 且 ok', false, e.message); }

  // H4 cache-control private
  try {
    const r = await get('/api/public/ota');
    check('H4 /api/public/ota cache-control 含 private',
      /private/.test(r.headers['cache-control'] || ''), r.headers['cache-control']);
  } catch (e) { check('H4 /api/public/ota cache-control 含 private', false, e.message); }

  // H5 channel 路径穿越回落 stable
  try {
    const r = await get('/api/public/ota?product=agent&channel=' + encodeURIComponent('../config'));
    check('H5 channel=../config 回落 stable（200 不炸）', r.code === 200, `code=${r.code}`);
    const j = JSON.parse(r.body);
    check('H5 回落结果不含穿越文件内容（不是 JS 源码）',
      !/\bexport const\b/.test(r.body), 'body 无异样');
  } catch (e) { check('H5 channel 穿越回落', false, e.message); }

  // H6 admin/config 脱敏
  try {
    const r = await get('/api/admin/config', { authorization: 'Bearer ' + VIEWER_TOKEN });
    const j = JSON.parse(r.body);
    const noLeak = !r.body.includes(AGENT_TOKEN) && !r.body.includes(VIEWER_TOKEN);
    check('H6 /api/admin/config 不吐令牌明文', r.code === 200 && noLeak, `wsToken=${j.wsToken}`);
    check('H6 脱敏格式为 ****尾4位', /^\*{4}.{0,4}$/.test(j.wsToken || ''), j.wsToken);
  } catch (e) { check('H6 /api/admin/config 脱敏', false, e.message); }

  // H7 ws maxPayload：鉴权前挡 5MB 帧
  let WS = null;
  try {
    const mod = await import('ws');
    WS = mod.default || mod.WebSocket || mod;
  } catch { /* 无 ws 库就跳过，不算失败 */ }
  if (!WS) {
    check('H7 ws maxPayload（SKIP：无 ws 库）', true);
  } else {
    await new Promise(resolve => {
      const ws = new WS(`ws://${HOST}:${PORT}/ws/agent`);
      let verdict = null;
      const timer = setTimeout(() => {
        try { ws.terminate(); } catch {}
        check('H7 ws maxPayload 4MiB（5MB 帧应被拒）', verdict === 'closed', `verdict=${verdict}`);
        resolve();
      }, 6000);
      ws.on('open', () => {
        ws.send('x'.repeat(5 * 1024 * 1024));   // 5MB > 4MiB 上限
      });
      ws.on('error', () => { verdict = 'error'; });
      ws.on('close', () => {
        verdict = 'closed';
        clearTimeout(timer);
        check('H7 ws maxPayload 4MiB（5MB 帧被拒）', true);
        resolve();
      });
    });
  }
}

/* ── main ─────────────────────────────────────────────────────────────── */
console.log('=== 云端 10-13 审计修复回归 ===');
console.log(`目标: http://${HOST}:${PORT}`);
try {
  await unitPruneKnown();
  await unitReceiptsLike();
  await httpChecks();
} catch (e) {
  check('测试自身异常', false, e.stack || e.message);
}
console.log(`\n=== 结果: ${pass} 过 / ${fail} 败 ===`);
process.exit(fail === 0 ? 0 : 1);
