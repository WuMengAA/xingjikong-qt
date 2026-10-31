// 班级考勤（2026-10-31）· 端到端回归
//
// 守的是「云端数据与 API 先行」这条落地（实盘确认此前云端连考勤 API 都没有）：
//   ① 记录：按 studentId 记 / 按 studentNo 记 / 非法状态拒 / 不在名单拒 / 坏日期拒
//   ② upsert：同一学生同一天同一节次重复记 = 改判（不长第二行）
//   ③ 按班按日列出 + 按区间列出
//   ④ 统计：四状态计数 + 名单总人数 + 未记录人数
//   ⑤ 批量：全班一次性提交，单行失败不阻断其余
//   ⑥ 权限：非本班成员读不到 / 写不了（读宽写窄）
//   ⑦ 删班：考勤随班销毁，不留孤儿行
//   ⑧ 落盘：重启后考勤仍在（不是内存态）
//
// 跑法：node tests/regression-class-attendance.mjs（自带临时目录 + 自动端口 + 关停）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-att-'));

const CLOUD_PORT = 8831;
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

const CLASS_A = 'cls-att-a';
const CLASS_B = 'cls-att-b';
const UID_OUTSIDER = 'outsider-teacher';

process.env.CLOUD_VIEWER_SECRET = VIEWER_SECRET;
const { mintViewerTicket } = await import('../src/ticket.js');

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

function warmTicket(ticket, timeoutMs = 6000) {
  return new Promise((resolve) => {
    let ws = null;
    const done = () => { try { ws && ws.close(); } catch { /* 忽略 */ } resolve(true); };
    const timer = setTimeout(done, timeoutMs);
    try {
      ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}/ws/viewer`);
      ws.on('open', () => {
        ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-warm', payload: { ticket } }));
      });
      ws.on('message', (raw) => {
        let m = null;
        try { m = JSON.parse(raw.toString()); } catch { return; }
        if (m && m.type === 'viewer-ready') { clearTimeout(timer); done(); }
      });
      ws.on('error', () => { clearTimeout(timer); done(); });
    } catch { clearTimeout(timer); done(); }
  });
}

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

function cls(action, body, auth = VIEWER_TOKEN) {
  return call('POST', `/api/class/${action}`, { auth, body });
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
      CLOUD_VIEWER_SECRET: VIEWER_SECRET,
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

let cloud = startCloud();
try {
  await waitUp();
  await cls('create', { classCode: CLASS_A, className: '考勤测试A班' });
  await cls('create', { classCode: CLASS_B, className: '考勤测试B班' });

  // 越权票据：scopes 只覆盖 B 班，且不是 A 班成员
  const outsider = mintViewerTicket(UID_OUTSIDER, 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_B }],
  }).ticket;
  await warmTicket(outsider);

  // 名单（A 班 3 人）
  const s1 = (await cls('student-add', { classCode: CLASS_A, student: { name: '李明', studentNo: '20280101', seat: '01' } })).json.student;
  const s2 = (await cls('student-add', { classCode: CLASS_A, student: { name: '王芳', studentNo: '20280102', seat: '02' } })).json.student;
  const s3 = (await cls('student-add', { classCode: CLASS_A, student: { name: '张伟', studentNo: '20280103', seat: '03' } })).json.student;

  const TODAY = '2026-10-31';

  await ok('① 按 studentId 记录出勤', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s1.id, date: TODAY, status: 'present' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.record.status, 'present');
    assert.equal(r.json.record.studentName, '李明');
    assert.equal(r.json.record.recorder, 'system');
  });

  await ok('② 按 studentNo 记录迟到', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentNo: '20280102', date: TODAY, status: 'late' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.record.status, 'late');
    assert.equal(r.json.record.studentName, '王芳');
  });

  await ok('③ 非法状态被拒', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s1.id, date: TODAY, status: '逃课' } });
    assert.equal(r.status, 400);
    assert.match(r.json.error, /状态/);
  });

  await ok('④ 不在名单的学生被拒', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentNo: '99999999', date: TODAY, status: 'present' } });
    assert.equal(r.status, 400);
    assert.match(r.json.error, /名单/);
  });

  await ok('⑤ 坏日期被拒', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s1.id, date: '2026/10/31', status: 'present' } });
    assert.equal(r.status, 400);
    assert.match(r.json.error, /YYYY-MM-DD/);
  });

  await ok('⑤b 记录张伟（20280103）出勤', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s3.id, date: TODAY, status: 'present' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.record.studentName, '张伟');
  });

  await ok('⑥ upsert：同一学生同天改判（不长第二行）', async () => {
    const a = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s1.id, date: TODAY, status: 'absent' } });
    assert.equal(a.status, 200, JSON.stringify(a.json));
    assert.equal(a.json.replaced, true, '第二次应是改判');
    const list = await cls('attendance-list', { classCode: CLASS_A, date: TODAY });
    assert.equal(list.json.count, 3, 'A 班当天只有 3 名学生被记过，不应因改判而变 4');
    const hit = list.json.records.find((x) => x.studentId === s1.id);
    assert.equal(hit.status, 'absent', '改判后应迟到→缺勤');
  });

  await ok('⑦ 按班按日列出（按学号序）', async () => {
    const list = await cls('attendance-list', { classCode: CLASS_A, date: TODAY });
    assert.equal(list.status, 200, JSON.stringify(list.json));
    assert.deepEqual(list.json.records.map((x) => x.studentNo), ['20280101', '20280102', '20280103']);
  });

  await ok('⑧ 按 session 过滤（节次维度）', async () => {
    await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s2.id, date: TODAY, session: '1', status: 'present' } });
    const all = await cls('attendance-list', { classCode: CLASS_A, date: TODAY });
    assert.equal(all.json.count, 4, '王芳现在有两个节次记录');
    const s1only = await cls('attendance-list', { classCode: CLASS_A, date: TODAY, session: '1' });
    assert.equal(s1only.json.count, 1, '只王芳有第1节记录');
  });

  await ok('⑨ 统计：四状态计数 + 未记录人数', async () => {
    // A 班 3 人。当天：李明(absent) / 王芳(late + 第1节 present) / 张伟(默认没记第1节)...
    // 注意：张伟只有 ③ 里没记（被拒），实际张伟未记录。重新明确：张伟记一次。
    await cls('attendance-record', { classCode: CLASS_A, record: { studentNo: '20280103', date: TODAY, status: 'leave' } });
    const st = await cls('attendance-stats', { classCode: CLASS_A, date: TODAY });
    assert.equal(st.status, 200, JSON.stringify(st.json));
    // 已记录学生去重：李明(1) + 王芳(2节次但1人) + 张伟(1) = 3
    assert.equal(st.json.recorded, 3, JSON.stringify(st.json));
    assert.equal(st.json.rosterSize, 3);
    assert.equal(st.json.unrecorded, 0);
    assert.equal(st.json.counts.absent, 1);
    assert.equal(st.json.counts.late, 1);
    assert.equal(st.json.counts.leave, 1);
  });

  await ok('⑩ 批量记录：全班一次性提交', async () => {
    const rows = [
      { studentId: s1.id, date: TODAY, session: '2', status: 'present' },
      { studentNo: '20280102', date: TODAY, session: '2', status: 'present' },
      { studentNo: '00000000', date: TODAY, session: '2', status: 'present' }, // 不存在 → 单行失败
    ];
    const r = await cls('attendance-batch', { classCode: CLASS_A, records: rows });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.total, 3);
    assert.equal(r.json.succeeded, 2, JSON.stringify(r.json));
    assert.equal(r.json.failed, 1, '不存在学号那行应失败且不阻断其余');
    assert.equal(r.json.results[2].ok, false);
  });

  await ok('⑪ 按区间列出', async () => {
    const r = await cls('attendance-range', { classCode: CLASS_A, fromDate: TODAY, toDate: TODAY });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.ok(r.json.count >= 5, '当天累计多节次记录');
  });

  await ok('⑫ 非本班成员：读不到考勤', async () => {
    const r = await cls('attendance-list', { classCode: CLASS_A, date: TODAY }, outsider);
    assert.equal(r.status, 400, JSON.stringify(r.json));
    assert.match(r.json.error, /无权限/);
  });

  await ok('⑬ 非本班成员：写不了考勤', async () => {
    const r = await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s1.id, date: TODAY, status: 'present' } }, outsider);
    assert.equal(r.status, 400, JSON.stringify(r.json));
    assert.match(r.json.error, /无权限/);
  });

  await ok('⑭ 删除一条（幂等）', async () => {
    const before = (await cls('attendance-list', { classCode: CLASS_A, date: TODAY })).json.count;
    const r = await cls('attendance-remove', { classCode: CLASS_A, studentId: s1.id, date: TODAY, session: '' });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.ok(r.json.removed >= 1);
    const after = (await cls('attendance-list', { classCode: CLASS_A, date: TODAY })).json.count;
    assert.equal(after, before - 1, "删一条应少一条（session='' 那条）");
  });

  await ok('⑮ 删班：考勤随班销毁', async () => {
    const del = await cls('delete', { classCode: CLASS_A, strategy: 'release' });
    assert.equal(del.status, 200, JSON.stringify(del.json));
    assert.ok(del.json.attendanceDropped >= 1, JSON.stringify(del.json));
    const after = await cls('attendance-list', { classCode: CLASS_A, date: TODAY });
    assert.equal(after.status, 400, '班级已不存在');
  });

  await ok('⑯ 重启后考勤仍在（落盘不是内存）', async () => {
    await cls('create', { classCode: CLASS_A, className: '考勤测试A班' });
    const s = (await cls('student-add', { classCode: CLASS_A, student: { name: '持久化', studentNo: '20280999', seat: '01' } })).json.student;
    await cls('attendance-record', { classCode: CLASS_A, record: { studentId: s.id, date: TODAY, status: 'present' } });
    await stopCloud(cloud);
    cloud = startCloud();
    await waitUp();
    const list = await cls('attendance-list', { classCode: CLASS_A, date: TODAY });
    assert.equal(list.json.count, 1, '重启后考勤应仍在');
    assert.equal(list.json.records[0].status, 'present');
  });
} catch (e) {
  fail++;
  console.error('  FATAL', e && e.stack || e);
  console.error('--- cloud stdout ---\n' + cloud.log.out.slice(-2000));
  console.error('--- cloud stderr ---\n' + cloud.log.err.slice(-2000));
} finally {
  await stopCloud(cloud);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* 忽略 */ }
}

console.log(`\n════ 考勤回归 ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail === 0 ? 0 : 1);
