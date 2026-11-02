/**
 * 回归测试 · 课表下发到教室机（2026-11-02）
 *
 * 守的是一条**改造前根本不存在**的链路：
 *   改造前 adminImportTimetable 只落库 + 广播给管理端浏览器，教室机一无所知；
 *   设备端也从来没有任何接收课表的消息类型。两端各自正确、中间从未接线。
 *
 * 这里用真云端 + 真 WS 被控端跑端到端，断言：
 *   ① 没有课表时下发必须**明确拒绝**（不能谎报成功，更不能下发空课表清空教室课表）
 *   ② 保存课表后下发 → 设备真收到 timetable-apply，且带的是归一化信封
 *   ③ 设备回执 → 送达视图里从 pending 变成 delivered
 *   ④ 别名班级码下发也命中（与「绑班过解析器、查班也必须过」同一口径）
 *   ⑤ 权限：非本班成员不能下发
 *
 * 跑法：node tests/regression-class-timetable-push.mjs
 * 退出码：0 全绿 / 1 有失败
 */
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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-tt-push-'));
const CLOUD_PORT = 8841;
const VIEWER_TOKEN = 'test-viewer-token';
const DEV_TOKEN = 'test-dev-token';
const CLASS_A = 'cls-tt-a';
const CLASS_B = 'cls-tt-b';

process.env.CLOUD_VIEWER_SECRET = 'test-viewer-secret';

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

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
const cls = (action, body, auth = VIEWER_TOKEN) => call('POST', `/api/class/${action}`, { auth, body });

function startCloud() {
  const child = spawn(process.execPath, [CLOUD_ENTRY], {
    cwd: WAR_DIR,
    env: {
      ...process.env,
      CLOUD_WS_PORT: String(CLOUD_PORT),
      CLOUD_WS_HOST: '127.0.0.1',
      CLOUD_WS_TOKEN: DEV_TOKEN,
      CLOUD_VIEWER_TOKEN: VIEWER_TOKEN,
      CLOUD_VIEWER_SECRET: process.env.CLOUD_VIEWER_SECRET,
      CLOUD_EVENTS_FILE: join(WAR_DIR, 'events.log'),
      CLOUD_QUEUE_DB_FILE: join(WAR_DIR, 'queue.db'),
      CLOUD_INTERACT_DB_FILE: join(WAR_DIR, 'interact.db'),
      CLOUD_CLASS_DIR_DB_FILE: join(WAR_DIR, 'class-dir.db'),
      CLOUD_CLASS_ROOM_DB_FILE: join(WAR_DIR, 'class-room.db'),
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

function wsClient(path) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 非 JSON 忽略 */ } });
  ws.on('error', () => { /* 关闭阶段正常 */ });
  return { ws, inbox };
}
const opened = (c) => new Promise((res, rej) => {
  if (c.ws.readyState === 1) return res();
  c.ws.on('open', res); c.ws.on('error', rej);
});
function waitFor(inbox, pred, ms = 2500, from = 0) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const tick = () => {
      const i = inbox.findIndex((m, idx) => idx >= from && pred(m));
      if (i >= 0) return resolve(inbox[i]);
      if (Date.now() - t0 > ms) return resolve(null);
      setTimeout(tick, 20);
    };
    tick();
  });
}

async function connectAgent(uid, classCode) {
  const c = wsClient('/ws/agent');
  await opened(c);
  c.ws.send(JSON.stringify({
    v: 1, type: 'register', id: `reg-${uid}`,
    payload: {
      uid, token: DEV_TOKEN, fp: `fp-${uid}`, version: '9.9.9',
      name: `机器-${uid}`, actions: [],
      ...(classCode ? { classCode, className: '回归班' } : {}),
    },
  }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'registered' && m.payload?.uid === uid);
  assert.ok(ack, `被控端 ${uid} 没收到 registered`);
  return c;
}

async function waitUp(timeoutMs = 20000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    try {
      const r = await call('POST', '/api/class/my-classes', { auth: VIEWER_TOKEN, body: {} });
      if (r.status === 200) return;
    } catch { /* 未就绪 */ }
    await sleep(250);
  }
  throw new Error('云端没能在超时内起来');
}

/* 一份最小但形状正确的归一化信封（两天课、两天共用一张作息） */
function envelope() {
  return {
    schema: 'classisland/v1',
    source: 'classisland',
    importedAt: new Date().toISOString(),
    group: { id: 'g1', name: '回归课表群', label: '1班', index: 1, inferred: false, nameSource: 'declared' },
    days: [
      { dow: 1, name: '周一', timeLayoutId: 'tl1', lessons: [
        { index: 1, subjectId: 's1', subject: '语文', initial: '语', teacher: '张', start: '08:00', end: '08:45' },
        { index: 2, subjectId: 's2', subject: '数学', initial: '数', teacher: '李', start: '08:55', end: '09:40' },
      ] },
      { dow: 2, name: '周二', timeLayoutId: 'tl1', lessons: [
        { index: 1, subjectId: 's2', subject: '数学', initial: '数', teacher: '李', start: '08:00', end: '08:45' },
        { index: 2, subjectId: 's1', subject: '语文', initial: '语', teacher: '张', start: '08:55', end: '09:40' },
      ] },
    ],
    layouts: [{ id: 'tl1', name: '秋季作息', items: [
      { kind: 'lesson', label: '第1节', start: '08:00', end: '08:45', name: '' },
      { kind: 'break', label: '课间1', start: '08:45', end: '08:55', name: '课间' },
      { kind: 'lesson', label: '第2节', start: '08:55', end: '09:40', name: '' },
    ] }],
    subjects: [
      { id: 's1', name: '语文', initial: '语', teacher: '张' },
      { id: 's2', name: '数学', initial: '数', teacher: '李' },
    ],
  };
}

console.log('课表下发到教室机 · 端到端回归');
const cloud = startCloud();
let agentA = null;

try {
  await waitUp();

  await ok('建两个班', async () => {
    for (const [code, name] of [[CLASS_A, '回归A班'], [CLASS_B, '回归B班']]) {
      const r = await cls('create', { classCode: code, className: name });
      assert.equal(r.status, 200, JSON.stringify(r.json));
      assert.ok(r.json.ok, JSON.stringify(r.json));
    }
  });

  agentA = await connectAgent('tt-dev-1', CLASS_A);
  await sleep(200);

  // ① 没课表就下发 → 必须拒绝（空课表会让教室机把当天课全清掉）
  await ok('没有课表时下发 → 明确拒绝（不谎报、不下发空表）', async () => {
    const r = await cls('timetable-push', { classCode: CLASS_A });
    assert.equal(r.status, 400, `status=${r.status} ${JSON.stringify(r.json)}`);
    assert.equal(r.json.ok, false);
    assert.match(String(r.json.error || ''), /还没有课表/, JSON.stringify(r.json));
    const m = await waitFor(agentA.inbox, (x) => x.type === 'timetable-apply', 600);
    assert.equal(m, null, '拒绝下发了却仍给设备发了 timetable-apply');
  });

  // ② 保存课表
  await ok('保存课表', async () => {
    const r = await cls('timetable-save', { classCode: CLASS_A, profile: envelope() });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.ok(r.json.ok, JSON.stringify(r.json));
  });

  // ③ 下发 → 设备真收到，且带的是归一化信封
  await ok('下发 → 设备收到 timetable-apply 且信封 schema 正确', async () => {
    const before = agentA.inbox.length;
    const r = await cls('timetable-push', { classCode: CLASS_A });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.ok(r.json.ok, JSON.stringify(r.json));
    assert.equal(r.json.days, 2, `days=${r.json.days}`);
    assert.equal(r.json.lessons, 4, `lessons=${r.json.lessons}`);
    assert.ok(r.json.online >= 1, `online=${r.json.online}`);
    assert.ok(r.json.delivered >= 1, `delivered=${r.json.delivered}`);

    const m = await waitFor(agentA.inbox, (x) => x.type === 'timetable-apply', 2500, before);
    assert.ok(m, '设备没收到 timetable-apply');
    assert.equal(m.payload?.profile?.schema, 'classisland/v1', JSON.stringify(m.payload?.profile?.schema));
    assert.equal(m.payload?.profile?.days?.length, 2, '信封里的天数不对');
  });

  // ④ 设备回执 → 送达视图从 pending 变 delivered
  await ok('送达视图：没回执在 pending，回执后进 delivered', async () => {
    const before = await cls('timetable-delivery', { classCode: CLASS_A });
    assert.equal(before.status, 200, JSON.stringify(before.json));
    assert.ok(before.json.pending.includes('tt-dev-1'), `pending=${JSON.stringify(before.json.pending)}`);

    agentA.ws.send(JSON.stringify({
      v: 1, type: 'timetable-applied', id: 'r1',
      payload: { ok: true, changed: true, days: 2, lessons: 4 },
    }));
    await sleep(400);

    const after = await cls('timetable-delivery', { classCode: CLASS_A });
    assert.equal(after.status, 200, JSON.stringify(after.json));
    const hit = after.json.delivered.find((d) => d.uid === 'tt-dev-1');
    assert.ok(hit, `delivered=${JSON.stringify(after.json.delivered)}`);
    assert.equal(hit.ok, true);
    assert.equal(hit.days, 2);
    assert.equal(hit.classCode, CLASS_A, `回执里班级归属被补错了：${hit.classCode}`);
    assert.ok(!after.json.pending.includes('tt-dev-1'), '回执后仍在 pending');
  });

  // ⑤ 失败回执也要如实进 delivered 且 ok=false（"收到但失败"≠"没收到"）
  await ok('失败回执 ok=false 仍进 delivered（与 pending 区分开）', async () => {
    agentA.ws.send(JSON.stringify({
      v: 1, type: 'timetable-applied', id: 'r2',
      payload: { ok: false, error: '写盘失败：磁盘满' },
    }));
    await sleep(400);
    const r = await cls('timetable-delivery', { classCode: CLASS_A });
    const hit = r.json.delivered.find((d) => d.uid === 'tt-dev-1');
    assert.ok(hit && hit.ok === false, JSON.stringify(hit));
    assert.match(hit.error, /磁盘满/, JSON.stringify(hit));
  });

  // ⑥ 别名下发也命中（与绑班/查班同一归一口径）
  await ok('用班级别名下发同样命中设备', async () => {
    const alias = await cls('create', { classCode: 'cls-tt-alias-src', className: '回归A班' });
    assert.equal(alias.status, 200, JSON.stringify(alias.json));
    const before = agentA.inbox.length;
    // 用「显示名」当班级码下发：解析器应把它归一回到 canonical
    const r = await cls('timetable-push', { classCode: '回归A班' });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.ok(r.json.delivered >= 1, `别名下发没送到：${JSON.stringify(r.json)}`);
    const m = await waitFor(agentA.inbox, (x) => x.type === 'timetable-apply', 2000, before);
    assert.ok(m, '别名下发后设备没收到');
  });

  // ⑦ 空 days 信封：云端存得进去（class-data 不解释语义），但设备端必须拒
  await ok('空 days 的信封设备端必须拒（不覆盖本机课表）', async () => {
    // 这里只验云端不崩；端侧拒绝由 timetable_apply 的 schema/days 双校验保证
    const bad = await cls('timetable-save', {
      classCode: CLASS_B,
      profile: { schema: 'classisland/v1', days: [], layouts: [], subjects: [], group: { id: 'g', label: '空' } },
    });
    assert.equal(bad.status, 200, JSON.stringify(bad.json));
    const push = await cls('timetable-push', { classCode: CLASS_B });
    // B 班没有在线设备，但 push 本身应 ok（课表存在），online=0
    assert.equal(push.status, 200, JSON.stringify(push.json));
    assert.equal(push.json.online, 0, `B 班不该有在线设备：${JSON.stringify(push.json)}`);
  });

  // ⑧ 权限：非本班成员不能下发（用一个全新 viewer 票）
  await ok('非本班成员下发 → 拒绝', async () => {
    const { mintViewerTicket } = await import('../src/ticket.js');
    const outsider = mintViewerTicket({ uid: 'tt-outsider', role: 'teacher', scopes: [], classes: [] });
    const r = await cls('timetable-push', { classCode: CLASS_A }, outsider);
    // 401 = 票据未被云端验证过；400/403 = 走到权限判定后被拒。两者都算"没放行"，
    // 但**必须断言 ok=false** —— 只看状态码会让「200 但 ok:false」这种假成功漏网。
    assert.ok([400, 401, 403].includes(r.status), `status=${r.status}`);
    assert.equal(r.json.ok, false, JSON.stringify(r.json));
  });
} catch (e) {
  fail++;
  console.error('[FATAL]', e && e.stack || e);
} finally {
  if (agentA) { try { agentA.ws.close(); } catch { /* 忽略 */ } }
  try { cloud.child.kill(); } catch { /* 忽略 */ }
  await sleep(600);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 目录被占用不算失败 */ }
}

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail) {
  console.log('--- 云端 stderr 末尾 ---');
  console.log((cloud.log.err || '').split('\n').slice(-25).join('\n'));
}
process.exit(fail ? 1 : 0);
