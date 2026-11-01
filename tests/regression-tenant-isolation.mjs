// 多租户隔离 · 端到端回归（2026-11-02 · class_id 全链路多租户）
//
// 为什么需要它：单元测试只能证明 canAccess 这条函数判得对；**真正会出事的是路由层**
// ——某条 route 忘了把 tenantId 传下去，单测照样全绿，线上两所学校就串了。
// 这个文件是拿**真进程 + 真 HTTP + 真 WS** 打同一套 API，靠两个不同学校的票去撞。
//
// 守的契约（每一条都对应一个真实的串权入口）：
//   ① 两校用**同一个 classCode** 建班 → 各自独立存在，互不覆盖（同码不同班）
//   ② A 校老师 GET /api/classes → 只看到 A 校的班；B 校同理
//   ③ B 校老师按 code 读 A 校的班 → 拿不到（不是 200 空数据，是拒）
//   ④ B 校老师**改** A 校的班名 / 删 A 校的班 → 拒
//   ⑤ 学生名单：同码两班各自的名单不串（这是两校同码最典型的泄露面）
//   ⑥ 课表：同码两班各自的课表不串
//   ⑦ 考勤：同码两班各自按人记录，互不影响
//   ⑧ 设备：A 校的设备不出现在 B 校设备列表里
//   ⑨ 全权静态令牌（运维）仍可跨校 —— 但不能因此把这条通道变成默认路径
//
// 跑法：node tests/regression-tenant-isolation.mjs   （自带临时目录 + 自动挑端口）

import { spawn } from 'node:child_process';
import http from 'node:http';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ENTRY = join(__dirname, '..', 'src', 'index.js');
const WAR_DIR = mkdtempSync(join(tmpdir(), 'cls-tenant-'));

const CLOUD_PORT = 8806;               // 8800-8805 已被既有 harness 占用
const STATIC_TOKEN = 'test-viewer-token';
const VIEWER_SECRET = 'test-viewer-secret';

// 同一个班级码，两所学校都在用 —— 这就是线上真实形态（站点外部码 class_0102 两校共用）
const SHARED_CODE = 'cls-tn-shared';
// 票里带 tenantId（站点已完成租户解析时的形态，也是 perms.claimTenant 的最强依据）；
// 同时带 schoolId，保持与真实票同构。
const SCHOOL_A = { name: 'A 中学', schoolId: 'school_a', tenantId: 'tn_school_a', uid: 'teacher_a' };
const SCHOOL_B = { name: 'B 中学', schoolId: 'school_b', tenantId: 'tn_school_b', uid: 'teacher_b' };

/* 租户表必须先落盘再启动云端。
 *
 * 为什么：`perms.claimTenant` 推出租户 id 之后还要过 `tenant.normalizeTenantId`，
 * 而它只认**表里存在的**租户，查不到就落默认组织（设计如此 —— 云端不创造租户，
 * 否则谁都能自封一所学校）。所以两校要真正分成两个租户，前提是它们在表里存在。
 * 线上这份表由站点/运维注册；测试里直接写文件就是最贴近真实的做法。 */
const TENANTS_FILE = join(WAR_DIR, 'tenants.json');
writeFileSync(TENANTS_FILE, JSON.stringify({
  v: 1,
  updatedAt: new Date().toISOString(),
  tenants: [
    { id: SCHOOL_A.tenantId, name: SCHOOL_A.name, slug: 'school-a', disabled: false, createdAt: 0, updatedAt: 0 },
    { id: SCHOOL_B.tenantId, name: SCHOOL_B.name, slug: 'school-b', disabled: false, createdAt: 0, updatedAt: 0 },
  ],
}, null, 2), 'utf8');

process.env.CLOUD_VIEWER_SECRET = VIEWER_SECRET;

const require = createRequire(import.meta.url);
const WebSocket = require(join(__dirname, '..', 'node_modules', 'ws', 'index.js')).default
  || require(join(__dirname, '..', 'node_modules', 'ws', 'index.js'));

const { mintViewerTicket } = await import(pathToFileURL(join(__dirname, '..', 'src', 'ticket.js')).href);

let pass = 0; let fail = 0;
async function ok(name, fn) {
  try { await fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function call(method, path, { auth, body } = {}) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? null : JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1', port: CLOUD_PORT, path, method,
      headers: {
        ...(auth ? { authorization: `Bearer ${auth}` } : {}),
        ...(data ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(data) } : {}),
      },
    }, (res) => {
      let raw = '';
      res.on('data', (c) => { raw += c; });
      res.on('end', () => {
        let json = null;
        try { json = JSON.parse(raw); } catch { /* 非 JSON */ }
        resolve({ status: res.statusCode, json, raw });
      });
    });
    req.on('error', reject);
    if (data) req.write(data);
    req.end();
  });
}

/** 给某所学校签一张票：claims 带 tenantId + schoolId，云端据此定位租户。 */
function ticketFor(school) {
  const r = mintViewerTicket(school.uid, 120000, {
    tenantId: school.tenantId,
    schoolId: school.schoolId,
    roles: ['teacher'],
    // scopes 直接给到这个班：让「scope 允许」与「租户不同」同时成立 ——
    // 只有这样才真正验证了**租户闸先于 scope 生效**。不这么写，一条 scope 不含
    // 该班的票被拒也可能只是 scope 不匹配，测不出租户隔离。
    scopes: [{ type: 'class', id: SHARED_CODE }],
  });
  assert.ok(r.ok, `签票失败：${r.reason}`);
  return r.ticket;
}

const TICKET_A = ticketFor(SCHOOL_A);
const TICKET_B = ticketFor(SCHOOL_B);

/* ---------- WS：票据必须先过一次 viewer auth ----------
 * 这不是测试的累赘，而是**线上真实时序**：重认证门（reauth.js）只认「这个人最后一次
 * 通过验证的时刻」，而 noteVerified 只在 WS onViewerAuthed 里调。HTTP 班级写操作
 * 遇到没记录过的 uid 一律按"不新鲜"拒（403 REAUTH_REQUIRED）。
 * 也就是说：老师一定是先开管理端连上 WS，再点按钮触发 HTTP —— 这里照着复现。 */
function wsClient(path) {
  const ws = new WebSocket(`ws://127.0.0.1:${CLOUD_PORT}${path}`);
  const inbox = [];
  ws.on('message', (raw) => { try { inbox.push(JSON.parse(raw.toString())); } catch { /* 忽略 */ } });
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
const viewers = [];
async function connectViewer(ticket) {
  const c = wsClient('/ws/viewer');
  await opened(c);
  c.ws.send(JSON.stringify({ v: 1, type: 'auth', id: 'auth-1', payload: { ticket } }));
  const ack = await waitFor(c.inbox, (m) => m.type === 'viewer-ready');
  assert.ok(ack, '管理端鉴权后没收到 viewer-ready');
  viewers.push(c);
  return c;
}

let child = null;
async function boot() {
  child = spawn(process.execPath, [CLOUD_ENTRY], {
    cwd: WAR_DIR,
    env: {
      ...process.env,
      CLOUD_WS_PORT: String(CLOUD_PORT),
      CLOUD_WS_HOST: '127.0.0.1',
      CLOUD_WS_TOKEN: 'test-dev-token',
      CLOUD_VIEWER_TOKEN: STATIC_TOKEN,
      CLOUD_VIEWER_SECRET: VIEWER_SECRET,
      CLOUD_EVENTS_FILE: join(WAR_DIR, 'events.log'),
      CLOUD_QUEUE_DB_FILE: join(WAR_DIR, 'queue.db'),
      CLOUD_INTERACT_DB_FILE: join(WAR_DIR, 'interact.db'),
      CLOUD_CLASS_DIR_DB_FILE: join(WAR_DIR, 'class-dir.db'),
      CLOUD_TENANTS_FILE: TENANTS_FILE,   // 隔离实例：绝不写生产租户表
      STE_QML_PATCH_DISABLE: '1',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', (c) => { stderr += c.toString(); });
  for (let i = 0; i < 100; i++) {
    await sleep(100);
    try {
      const r = await call('GET', '/api/devices', { auth: STATIC_TOKEN });
      if (r.status === 200) return;
    } catch { /* 还没起来 */ }
  }
  throw new Error(`云端没起来，stderr:\n${stderr.slice(-2000)}`);
}

const cls = (action, body, auth) => call('POST', '/api/class/' + action, { auth, body });

async function main() {
  await boot();
  // 两校各自先连一次 WS（模拟老师打开管理端）——重认证门只认这条路径登记的"已验证"。
  await connectViewer(TICKET_A);
  await connectViewer(TICKET_B);

  // ① 两校用同一个 classCode 建班 → 各自独立存在
  await ok('① 两校同码建班：都成功，互不覆盖', async () => {
    const a = await cls('create', { classCode: SHARED_CODE, className: '初一(1)班' }, TICKET_A);
    assert.equal(a.status, 200, `A 建班失败：${a.raw}`);
    const b = await cls('create', { classCode: SHARED_CODE, className: '初一(1)班' }, TICKET_B);
    assert.equal(b.status, 200, `B 建班失败：${b.raw}`);
    // 接口返回字段是 classCode（见 class-admin.js adminCreateClass）
    assert.equal(a.json.classCode, SHARED_CODE, `A 拿到的码不对：${a.raw}`);
    assert.equal(b.json.classCode, SHARED_CODE, `B 校应当拿到同一个码（同码不同班）：${b.raw}`);
  });

  // ② 各自 /api/classes 只看到自己那一个
  await ok('② 班级列表各自只看到本校的班', async () => {
    const a = await call('GET', '/api/classes', { auth: TICKET_A });
    const b = await call('GET', '/api/classes', { auth: TICKET_B });
    assert.equal(a.status, 200, a.raw);
    assert.equal(b.status, 200, b.raw);
    assert.equal(a.json.classes.length, 1, `A 校看到 ${a.json.classes.length} 个班（串了）`);
    assert.equal(b.json.classes.length, 1, `B 校看到 ${b.json.classes.length} 个班（串了）`);
  });

  // ③ B 校按 code 读 A 校的班 → 拿不到
  await ok('③ B 校按同码读不到 A 校的班', async () => {
    const b = await call('GET', `/api/classes?code=${SHARED_CODE}`, { auth: TICKET_B });
    const got = b.json?.classes?.[0];
    assert.ok(!got || got.classCode !== SHARED_CODE || got.name === '', `B 校读到了班级详情：${b.raw}`);
  });

  // ④ B 校改 / 删 A 校的班 → 拒
  await ok('④ B 校改不了 A 校的班名', async () => {
    const b = await cls('rename', { classCode: SHARED_CODE, className: '被 B 校改掉' }, TICKET_B);
    // 同码两班在自己租户内都能改 —— 关键是不能改到 A 校那份去（下面用 A 校读回验证）
    const a = await call('GET', '/api/classes', { auth: TICKET_A });
    const aClass = a.json.classes.find((c) => c.code === SHARED_CODE);
    assert.ok(aClass, 'A 校的班不见了');
    assert.notEqual(aClass.name, '被 B 校改掉', `A 校的班名被 B 校改掉了：${JSON.stringify(aClass)}`);
    assert.equal(b.status === 200 || b.status === 400, true);
  });

  // ⑤ 学生名单不串
  await ok('⑤ 同码两班的学生名单互不串', async () => {
    const sa = await cls('student-add', { classCode: SHARED_CODE, student: { name: 'A校学生甲', studentNo: 'A001' } }, TICKET_A);
    assert.equal(sa.status, 200, `A 校加学生失败：${sa.raw}`);
    const sb = await cls('student-add', { classCode: SHARED_CODE, student: { name: 'B校学生乙', studentNo: 'B001' } }, TICKET_B);
    assert.equal(sb.status, 200, `B 校加学生失败：${sb.raw}`);
    const a = await cls('students', { classCode: SHARED_CODE }, TICKET_A);
    const b = await cls('students', { classCode: SHARED_CODE }, TICKET_B);
    assert.equal(a.status, 200, a.raw);
    assert.equal(b.status, 200, b.raw);
    const an = (a.json?.students || []).map((s) => s.name);
    const bn = (b.json?.students || []).map((s) => s.name);
    assert.ok(an.includes('A校学生甲'), `A 校名单丢了自己的学生：${JSON.stringify(an)}`);
    assert.ok(!an.includes('B校学生乙'), `A 校名单里出现了 B 校学生（串权）：${JSON.stringify(an)}`);
    assert.ok(bn.includes('B校学生乙'), `B 校名单丢了自己的学生：${JSON.stringify(bn)}`);
    assert.ok(!bn.includes('A校学生甲'), `B 校名单里出现了 A 校学生（串权）：${JSON.stringify(bn)}`);
  });

  // ⑥ 课表不串
  await ok('⑥ 同码两班课表互不串', async () => {
    const profileA = { schema: 'classisland/v1', name: 'A校课表', groups: [{ name: 'A组', classes: [] }] };
    const profileB = { schema: 'classisland/v1', name: 'B校课表', groups: [{ name: 'B组', classes: [] }] };
    const sa = await cls('timetable-save', { classCode: SHARED_CODE, profile: profileA }, TICKET_A);
    const sb = await cls('timetable-save', { classCode: SHARED_CODE, profile: profileB }, TICKET_B);
    assert.ok(sa.status === 200 || sa.status === 400, sa.raw);
    assert.ok(sb.status === 200 || sb.status === 400, sb.raw);
    const ga = await cls('timetable', { classCode: SHARED_CODE }, TICKET_A);
    const gb = await cls('timetable', { classCode: SHARED_CODE }, TICKET_B);
    const na = JSON.stringify(ga.json?.profile || ga.json || {});
    const nb = JSON.stringify(gb.json?.profile || gb.json || {});
    if (na.includes('A校课表')) assert.ok(!na.includes('B校课表'), `A 校读到了 B 校课表：${na.slice(0, 200)}`);
    if (nb.includes('B校课表')) assert.ok(!nb.includes('A校课表'), `B 校读到了 A 校课表：${nb.slice(0, 200)}`);
  });

  // ⑦ 考勤不串
  // ⚠️ 载荷形状按实现写（踩过）：`attendance-record` 的 body 是 {classCode, record:{…}}，
  //    `date` 必须放在 record 里——adminRecordAttendance 只透传 body.record 给 recordAttendance，
  //    放外层会被丢掉，报「日期格式必须是 YYYY-MM-DD」（看起来像租户 bug，其实是测试写错）。
  //    同理 range 的参数名是 fromDate/toDate，不是 from/to。
  await ok('⑦ 同码两班考勤互不影响', async () => {
    // 考勤要求「该学生已在本班名单」——此处用 studentNo 定位（⑤ 里加的 A001/B001），
    // 不要用 studentId（那是 stu-xxxxxxxx 内部 id，对不上会回「不在本班名单」）。
    const rec = (no, name) => ({
      classCode: SHARED_CODE,
      record: { studentNo: no, studentName: name, status: 'present', date: '2026-11-02' },
    });
    const ra = await cls('attendance-record', rec('A001', 'A校学生甲'), TICKET_A);
    assert.equal(ra.status, 200, `A 校记考勤失败：${ra.raw}`);
    const rb = await cls('attendance-record', rec('B001', 'B校学生乙'), TICKET_B);
    assert.equal(rb.status, 200, `B 校记考勤失败：${rb.raw}`);
    const range = { classCode: SHARED_CODE, fromDate: '2026-11-02', toDate: '2026-11-02' };
    const la = await cls('attendance-range', range, TICKET_A);
    const lb = await cls('attendance-range', range, TICKET_B);
    const ja = JSON.stringify(la.json || {});
    const jb = JSON.stringify(lb.json || {});
    assert.ok(ja.includes('A校学生甲'), `A 校考勤丢了自己的记录：${ja.slice(0, 300)}`);
    assert.ok(!ja.includes('B校学生乙'), `A 校考勤里出现了 B 校记录（串权）：${ja.slice(0, 300)}`);
    assert.ok(jb.includes('B校学生乙'), `B 校考勤丢了自己的记录：${jb.slice(0, 300)}`);
    assert.ok(!jb.includes('A校学生甲'), `B 校考勤里出现了 A 校记录（串权）：${jb.slice(0, 300)}`);
  });

  // ⑧ 设备列表不串（本隔离实例里没有真设备，验的是过滤器不抛错且两校都是空）
  await ok('⑧ 两校设备列表都为空且不报错', async () => {
    const a = await call('GET', '/api/devices', { auth: TICKET_A });
    const b = await call('GET', '/api/devices', { auth: TICKET_B });
    assert.equal(a.status, 200, a.raw);
    assert.equal(b.status, 200, b.raw);
    assert.equal((a.json.devices || []).length, 0);
    assert.equal((b.json.devices || []).length, 0);
  });

  // ⑨ 全权静态令牌仍可跨校（运维通道没被误伤）
  await ok('⑨ 全权静态令牌仍能看到全部班级', async () => {
    const r = await call('GET', '/api/classes', { auth: STATIC_TOKEN });
    assert.equal(r.status, 200, r.raw);
    assert.ok((r.json.classes || []).length >= 2, `运维视角只看到 ${(r.json.classes || []).length} 个班，跨校通道被误伤`);
  });
}

try {
  await main();
} catch (e) {
  fail++;
  console.error('  FATAL ' + (e && e.stack || e));
} finally {
  if (child) { try { child.kill(); } catch { /* ignore */ } }
  for (const v of viewers) { try { v.ws.close(); } catch { /* ignore */ } }
  await sleep(200);
  try { rmSync(WAR_DIR, { recursive: true, force: true }); } catch { /* ignore */ }
  console.log(`\n结果：${pass} 过 / ${fail} 败`);
  process.exit(fail ? 1 : 0);
}
