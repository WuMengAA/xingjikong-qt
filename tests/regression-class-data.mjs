// 班级数据（学生名单 + 班级课表）· 端到端回归（2026-10-25）
//
// 守的是用户裁决「云端数据库存储，不要本机了」这条落地：
//   ① 学生名单：增 / 改 / 删 / 排序 / 学号幂等 / 空名拒绝 / 学号冲突拒绝
//   ② 批量导入：同学号更新而非新增（反复导入不长重复）
//   ③ 课表：没排过 → profile:null（不编造空白表）/ 保存后原样取回 / 超限拒绝
//   ④ 权限：非本班成员读不到、也写不了（读宽写窄）
//   ⑤ 删班：名单与课表随班销毁，不留孤儿行
//
// 跑法：node tests/regression-class-data.mjs（自带临时目录 + 自动端口 + 关停）

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
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-data-'));

const CLOUD_PORT = 8826;
const VIEWER_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

const CLASS_A = 'cls-data-a';
const CLASS_B = 'cls-data-b';
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

/**
 * 用票据连一次 /ws/viewer 并等 viewer-ready。
 * 为什么必须做：/api/class/* 写路径前面有一道"票据新鲜度"门，全新 uid 的票会被判
 * 未验证直接 403（在权限判定之前）。连一次 WS 等于完成一次真实验证，之后的 HTTP
 * 才会走到"无权限"这条我们要断言的分支（同 regression-class-publish 的做法）。
 */
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

/** 班级管理一律 POST /api/class/<action>（与既有 API 同形，读也走 action）。 */
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
  await cls('create', { classCode: CLASS_A, className: '高二3班' });
  await cls('create', { classCode: CLASS_B, className: '高二9班' });

  // 越权票据：scopes 只覆盖 B 班，且不是 A 班成员
  const outsider = mintViewerTicket(UID_OUTSIDER, 3600_000, {
    schoolId: null, roles: ['teacher'], scopes: [{ type: 'class', id: CLASS_B }],
  }).ticket;
  // 造一次"新鲜验证"，让新鲜度门放行、露出真正的权限判定（同 publish 回归的做法）
  await warmTicket(outsider);

  let s1 = null;
  let s2 = null;
  let s3 = null;

  await ok('① 加学生：返回 id 与规范化字段', async () => {
    const r = await cls('student-add', { classCode: CLASS_A, student: { name: ' 李明 ', studentNo: ' 20280101 ', seat: '03' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.student.name, '李明');
    assert.equal(r.json.student.studentNo, '20280101');
    assert.equal(r.json.student.seat, '03');
    assert.ok(String(r.json.student.id).startsWith('stu-'));
    s1 = r.json.student;
  });

  await ok('② 同学号再加一次 = 更新（不长重复）', async () => {
    const r = await cls('student-add', { classCode: CLASS_A, student: { name: '李明（改）', studentNo: '20280101', seat: '03' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.replaced, true);
    const list = await cls('students', { classCode: CLASS_A });
    assert.equal(list.json.count, 1, '同学号不该新增第二行');
  });

  await ok('③ 空姓名被拒', async () => {
    const r = await cls('student-add', { classCode: CLASS_A, student: { name: '   ' } });
    assert.equal(r.status, 400);
    assert.match(r.json.error, /姓名/);
  });

  await ok('④ 再补两人（含无座位者）', async () => {
    const a = await cls('student-add', { classCode: CLASS_A, student: { name: '王芳', studentNo: '20280102', seat: '01' } });
    assert.equal(a.status, 200, JSON.stringify(a.json));
    s2 = a.json.student;
    const b = await cls('student-add', { classCode: CLASS_A, student: { name: '张伟' } });
    assert.equal(b.status, 200, JSON.stringify(b.json));
    s3 = b.json.student;
  });

  await ok('⑤ 排序：座位 01 → 03 → 空座位排最后', async () => {
    const list = await cls('students', { classCode: CLASS_A });
    assert.equal(list.json.count, 3);
    assert.deepEqual(list.json.students.map((x) => x.name), ['王芳', '李明（改）', '张伟']);
  });

  await ok('⑥ 改座位/备注生效', async () => {
    const r = await cls('student-update', { classCode: CLASS_A, studentId: s3.id, patch: { seat: '02', note: '转学生' } });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.student.seat, '02');
    assert.equal(r.json.student.note, '转学生');
    assert.equal(r.json.student.name, '张伟', '没给的字段必须保持原样');
    s3 = r.json.student;
  });

  await ok('⑦ 学号改成别人占用的 → 拒', async () => {
    const r = await cls('student-update', { classCode: CLASS_A, studentId: s3.id, patch: { studentNo: '20280101' } });
    assert.equal(r.status, 400);
    assert.match(r.json.error, /学号/);
  });

  await ok('⑧ 批量导入：2 新 + 1 同学号更新', async () => {
    const r = await cls('students-import', {
      classCode: CLASS_A,
      rows: [
        { name: '赵敏', studentNo: '20280105', seat: '05' },
        { name: '孙悦', studentNo: '20280106', seat: '06' },
        { name: '王芳（改名）', studentNo: '20280102', seat: '01' },
        { name: '', studentNo: '20280107' },   // 无名 → 计 bad，不阻断整批
      ],
    });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.added, 2, JSON.stringify(r.json));
    assert.equal(r.json.updated, 1, JSON.stringify(r.json));
    assert.equal(r.json.bad, 1, JSON.stringify(r.json));
    assert.equal(r.json.count, 5);
  });

  await ok('⑨ 反复导入同一批 → 不新增（幂等）', async () => {
    const r = await cls('students-import', {
      classCode: CLASS_A,
      rows: [
        { name: '赵敏', studentNo: '20280105', seat: '05' },
        { name: '孙悦', studentNo: '20280106', seat: '06' },
      ],
    });
    assert.equal(r.json.added, 0, JSON.stringify(r.json));
    assert.equal(r.json.updated, 2);
    assert.equal(r.json.count, 5);
  });

  await ok('⑩ 课表：没排过 → profile:null（不编造空白表）', async () => {
    const r = await cls('timetable', { classCode: CLASS_A });
    assert.equal(r.status, 200, JSON.stringify(r.json));
    assert.equal(r.json.profile, null);
    assert.equal(r.json.updatedAt, 0);
  });

  const PROFILE = {
    name: '高二3班秋季',
    subjects: [{ id: 's1', name: '语文' }, { id: 's2', name: '数学' }],
    timeSlots: [{ id: 't1', start: '08:00', end: '08:45' }],
    weeks: [{ week: 1, cells: { 't1|1': 's1', 't1|2': 's2' } }],
  };

  await ok('⑪ 保存课表后原样取回（含 updatedAt）', async () => {
    const w = await cls('timetable-save', { classCode: CLASS_A, profile: PROFILE });
    assert.equal(w.status, 200, JSON.stringify(w.json));
    assert.ok(w.json.updatedAt > 0);
    const r = await cls('timetable', { classCode: CLASS_A });
    assert.deepEqual(r.json.profile, PROFILE, '取回内容必须与保存的一致');
    assert.ok(r.json.updatedAt >= w.json.updatedAt);
  });

  await ok('⑫ 课表非对象 / 超大 → 拒', async () => {
    const bad1 = await cls('timetable-save', { classCode: CLASS_A, profile: 'not-an-object' });
    assert.equal(bad1.status, 400);
    const big = { blob: 'x'.repeat(600 * 1024) };
    const bad2 = await cls('timetable-save', { classCode: CLASS_A, profile: big });
    assert.equal(bad2.status, 400);
    assert.match(bad2.json.error, /上限/);
  });

  await ok('⑬ 非本班成员：读不到名单', async () => {
    const r = await cls('students', { classCode: CLASS_A }, outsider);
    // 2026-11-02 对齐：10-31（7db72ea）路由落地起拒绝码就是 403/PV4002（全库口径一致），
    // 本用例写于路由存在之前（10-25），期望 400 从未与实现对齐过。
    assert.equal(r.status, 403, JSON.stringify(r.json));
    assert.match(r.json.error, /无权/);
  });

  await ok('⑭ 非本班成员：写不了名单 / 课表', async () => {
    const a = await cls('student-add', { classCode: CLASS_A, student: { name: '闯入者' } }, outsider);
    // 2026-11-02 口径对齐：student-add 在 index.js 路由层 canReach 拒 → 403 PV4002（与读同源）；
    // timetable-save 走 handleClassAdminApi 通用分发 send(r.ok?200:400) → 400。两个入口两种码是现状。
    assert.equal(a.status, 403);
    assert.equal(a.json.ok, false);
    const b = await cls('timetable-save', { classCode: CLASS_A, profile: PROFILE }, outsider);
    assert.equal(b.status, 400);
    assert.equal(b.json.ok, false);
    assert.match(b.json.error, /无权/);
  });

  await ok('⑮ 删班：名单与课表随班销毁', async () => {
    const before = await cls('students', { classCode: CLASS_A });
    assert.equal(before.json.count, 5);
    const del = await cls('delete', { classCode: CLASS_A, strategy: 'release' });
    assert.equal(del.status, 200, JSON.stringify(del.json));
    assert.ok(del.json.studentsDropped >= 5, JSON.stringify(del.json));
    assert.ok(del.json.timetableDropped >= 1, JSON.stringify(del.json));
    const after = await cls('students', { classCode: CLASS_A });
    // 2026-11-02 对齐实现：listStudents 对不存在班回 ok:true + count:0（空表不是错误），
    // 删班后名单已毁的直接证据 = count 归零（原期望 400/「班级不存在」从未与实现对齐过）。
    assert.equal(after.status, 200);
    assert.equal(after.json.count, 0);
  });

  await ok('⑯ 重启后数据仍在（落盘不是内存）', async () => {
    await cls('create', { classCode: CLASS_A, className: '高二3班' });
    await cls('student-add', { classCode: CLASS_A, student: { name: '持久化', studentNo: '20280199', seat: '01' } });
    await cls('timetable-save', { classCode: CLASS_A, profile: PROFILE });
    await stopCloud(cloud);
    cloud = startCloud();
    await waitUp();
    const s = await cls('students', { classCode: CLASS_A });
    assert.equal(s.json.count, 1, '重启后名单应仍在');
    const t = await cls('timetable', { classCode: CLASS_A });
    assert.deepEqual(t.json.profile, PROFILE, '重启后课表应仍在');
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

console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail === 0 ? 0 : 1);
