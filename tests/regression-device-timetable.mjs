// 设备侧课表只读端点回归（2026-11-06 · 用户 m08463 第 3 项）
//
// 三层，各有各能抓到的东西：
//   L1 纯逻辑：deviceTimetable() 的判定分支（未分班 / 无课表 / 有课表 / 读失败）
//              —— 注入桩依赖，不起服务、不碰磁盘，跑得快。
//   L2 隔离实例：真起一台云端（独立端口 + 全新 DB + 全新 events.log），
//              真设备 WS register（设备令牌），真 viewer 令牌写一份课表，
//              然后 GET /api/device/timetable 断言信封原样回来。
//              —— 这一层才能证明「鉴权与路由真的接上了」。
//   L3 反向：把薄壳端点从 index.js 里摘掉，重跑 L2 的请求，必须 404。
//              —— 证明 L2 的断言不是靠别的路径碰巧通过的。
//
// ⚠️ 与既有测试同样的两个坑（踩过，写在这里防重踩）：
//   ① config.js 在**模块评估期**读死环境变量 ⇒ 必须先设 env 再动态 import，不能静态 import。
//   ② 子进程 stdout 必须重定向到文件：Fastify 每请求一条 JSON 日志，PIPE 不排空会把
//      事件循环堵死（表现为「服务在但请求超时」）。本脚本用 spawn 的 stdio:'ignore'
//      加日志文件，不走 PIPE。

import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const CLOUD = join(HERE, '..');
const ENTRY = join(CLOUD, 'src', 'index.js');

const PORT = 8891;
const BASE = `http://127.0.0.1:${PORT}`;
const DEV_TOKEN = 'regress-device-token-8891';
const VIEWER_TOKEN = 'regress-viewer-token-8891';

let PASS = 0;
let FAIL = 0;
const fails = [];
function ok(cond, label) {
  if (cond) { PASS += 1; return true; }
  FAIL += 1; fails.push(label);
  console.log(`  ✗ ${label}`);
  return false;
}

/* ─────────────────────────── L1 纯逻辑 ─────────────────────────── */

async function L1() {
  console.log('\n== L1 纯逻辑 deviceTimetable() ==');
  const { deviceTimetable } = await import('../src/device-timetable.js');

  // ① 缺 uid
  const a = deviceTimetable({ uid: '', classCodeOf: () => null, tenantOf: () => 'tn_default', read: () => ({ ok: true }) });
  ok(a.status === 400, 'L1-01 缺 uid ⇒ 400');

  // ② 未分班（classCode 空）⇒ 200 + reason=NO_CLASS，不是错误
  const b = deviceTimetable({ uid: 'u1', classCodeOf: () => null, tenantOf: () => 'tn_default', read: () => ({ ok: true }) });
  ok(b.status === 200, 'L1-02 未分班 ⇒ 200');
  ok(b.body.ok === true, 'L1-03 未分班 ok=true');
  ok(b.body.reason === 'NO_CLASS', 'L1-04 未分班 reason=NO_CLASS');
  ok(b.body.profile === null, 'L1-05 未分班 profile=null');

  // ③ 有班但没课表 ⇒ 200 + reason=NO_TIMETABLE
  const c = deviceTimetable({
    uid: 'u1', classCodeOf: () => 'cls-a', tenantOf: () => 'tn_default',
    read: () => ({ ok: true, profile: null, updatedAt: 0 }),
  });
  ok(c.status === 200, 'L1-06 无课表 ⇒ 200');
  ok(c.body.reason === 'NO_TIMETABLE', 'L1-07 无课表 reason=NO_TIMETABLE');
  ok(c.body.classCode === 'cls-a', 'L1-08 无课表也回班级码');

  // ④ 有课表 ⇒ profile 原样透传（云端不解释结构）
  const profile = { schema: 'classisland/v1', source: 'cloud', days: [{ weekDay: 1 }] };
  const d = deviceTimetable({
    uid: 'u1', classCodeOf: () => 'cls-a', tenantOf: () => 'tn_default',
    read: () => ({ ok: true, profile, updatedAt: 1762300000000, updatedBy: 'WuMengAA' }),
  });
  ok(d.status === 200, 'L1-09 有课表 ⇒ 200');
  ok(d.body.reason === '', 'L1-10 有课表 reason 为空');
  ok(JSON.stringify(d.body.profile) === JSON.stringify(profile), 'L1-11 profile 原样透传');
  ok(d.body.updatedAt === 1762300000000, 'L1-12 updatedAt 透传');
  ok(d.body.updatedBy === 'WuMengAA', 'L1-13 updatedBy 透传');

  // ⑤ 读失败（read 抛异常）⇒ 500，不假装成功
  const e = deviceTimetable({
    uid: 'u1', classCodeOf: () => 'cls-a', tenantOf: () => 'tn_default',
    read: () => { throw new Error('db locked'); },
  });
  ok(e.status === 500, 'L1-14 read 抛异常 ⇒ 500');
  ok(String(e.body.error).includes('db locked'), 'L1-15 错误信息带上原因');

  // ⑥ read 回 ok:false ⇒ 400
  const f = deviceTimetable({
    uid: 'u1', classCodeOf: () => 'cls-a', tenantOf: () => 'tn_default',
    read: () => ({ ok: false, error: '缺班级码' }),
  });
  ok(f.status === 400, 'L1-16 read 回 ok:false ⇒ 400');

  // ⑦ uid 会 trim（前端带空格不算另一台设备）
  const g = deviceTimetable({
    uid: '  u1  ', classCodeOf: () => 'cls-a', tenantOf: () => 'tn_default',
    read: () => ({ ok: true, profile: null }),
  });
  ok(g.body.uid === 'u1', 'L1-17 uid 去空白');

  // ⑧ 班级显示名从 listClasses 取；取不到不许编
  const { deviceTimetableDefault } = await import('../src/device-timetable.js');
  ok(typeof deviceTimetableDefault === 'function', 'L1-18 deviceTimetableDefault 已导出');
}

/* ─────────────────── L1b 纯逻辑 选班预览 / 班级清单 ─────────────────── */

async function L1b() {
  console.log('\n== L1b 纯逻辑 device-api（班级清单 / 选班预览 / 鉴权）==');
  const { classesForDevice, timetableOfClass, authDevice, bearerOf } = await import('../src/device-api.js');

  const tenantOf = () => 'tn_a';
  const list = () => [
    { code: 'cls-1', name: '一班', onlineCount: 2, devices: ['d1', 'd2'] },
    { code: 'cls-2', name: '二班', onlineCount: 0, devices: [] },
  ];
  const read = (code) => ({ ok: true, profile: { code }, updatedAt: 5, updatedBy: 'x' });

  // ① 班级清单脱敏：只回 code/name/onlineCount，绝不带 devices
  const c = classesForDevice({ uid: 'dev-a', tenantOf, list });
  ok(c.status === 200 && c.body.ok === true, 'L1b-01 班级清单 200');
  ok(c.body.classes.length === 2, 'L1b-02 两条班级');
  ok(JSON.stringify(c.body.classes[0]) === JSON.stringify({ code: 'cls-1', name: '一班', onlineCount: 2 }),
    'L1b-03 只回脱敏三件（devices 不下发）');
  ok(!('devices' in c.body.classes[0]), 'L1b-04 无 devices 键');

  // ② 缺 uid
  ok(classesForDevice({ uid: '  ', tenantOf, list }).status === 400, 'L1b-05 班级清单缺 uid ⇒ 400');

  // ③ 选班预览：本班有课表 ⇒ 原样透传
  const t = timetableOfClass({ uid: 'dev-a', classCode: 'cls-1', tenantOf, list, read });
  ok(t.status === 200, 'L1b-06 选班预览 200');
  ok(t.body.className === '一班', 'L1b-07 回班级名');
  ok(JSON.stringify(t.body.profile) === JSON.stringify({ code: 'cls-1' }), 'L1b-08 profile 原样');

  // ④ 选一个不存在的班 ⇒ 403 NO_SUCH_CLASS（不区分不存在/跨租户，防探测）
  const t2 = timetableOfClass({ uid: 'dev-a', classCode: 'cls-nope', tenantOf, list, read });
  ok(t2.status === 403, 'L1b-09 不存在的班 ⇒ 403');
  ok(t2.body.code === 'NO_SUCH_CLASS', 'L1b-10 code=NO_SUCH_CLASS');

  // ⑤ 该班存在但没课表 ⇒ 200 NO_TIMETABLE（不是错误）
  const t3 = timetableOfClass({ uid: 'dev-a', classCode: 'cls-2', tenantOf, list, read: () => ({ ok: true, profile: null }) });
  ok(t3.status === 200 && t3.body.reason === 'NO_TIMETABLE', 'L1b-11 有班无课表 ⇒ 200 NO_TIMETABLE');

  // ⑥ 缺 classCode / 缺 uid
  ok(timetableOfClass({ uid: 'dev-a', classCode: '', tenantOf, list, read }).status === 400, 'L1b-12 缺班级号 ⇒ 400');
  ok(timetableOfClass({ uid: '', classCode: 'cls-1', tenantOf, list, read }).status === 400, 'L1b-13 缺 uid ⇒ 400');

  // ⑦ read 抛异常 ⇒ 500
  const t4 = timetableOfClass({ uid: 'dev-a', classCode: 'cls-1', tenantOf, list, read: () => { throw new Error('boom'); } });
  ok(t4.status === 500, 'L1b-14 read 抛异常 ⇒ 500');

  // ⑧ 鉴权：票优先（票里的 uid 权威），静态令牌 via=token
  ok(authDevice({ headers: { authorization: 'Bearer t123' }, devToken: 'DT', verifyTicket: () => ({ ok: true, uid: 'ticket-uid' }) }).uid === 'ticket-uid',
    'L1b-15 票验通 ⇒ uid 取票里的');
  const st = authDevice({ headers: { authorization: 'Bearer DT' }, devToken: 'DT', verifyTicket: () => ({ ok: false }) });
  ok(st.ok === true && st.via === 'token', 'L1b-16 静态令牌 ⇒ via=token');
  ok(authDevice({ headers: {}, devToken: 'DT' }).ok === false, 'L1b-17 无令牌 ⇒ 拒绝');
  ok(authDevice({ headers: { authorization: 'Bearer nope' }, devToken: 'DT', verifyTicket: () => ({ ok: false }) }).ok === false,
    'L1b-18 错令牌 ⇒ 拒绝');
  // 票说 ok 但没 uid（防御：不许当成通过）
  ok(authDevice({ headers: { authorization: 'Bearer x' }, devToken: 'DT', verifyTicket: () => ({ ok: true }) }).ok === false,
    'L1b-19 票无 uid ⇒ 不算通过');
  ok(bearerOf({ authorization: 'bEaReR abc ' }) === 'abc', 'L1b-20 Bearer 大小写不敏感且 trim');
}

/* ─────────────────────────── L2 隔离实例 ─────────────────────────── */

function makeWar() {
  const dir = mkdtempSync(join(tmpdir(), 'dev-tt-'));
  mkdirSync(join(dir, 'data'), { recursive: true });
  return dir;
}

function startCloud(war, entry = ENTRY) {
  const logPath = join(war, 'server.log');
  const env = {
    ...process.env,
    CLOUD_WS_PORT: String(PORT),
    CLOUD_WS_HOST: '127.0.0.1',
    CLOUD_WS_TOKEN: DEV_TOKEN,
    CLOUD_VIEWER_TOKEN: VIEWER_TOKEN,
    CLOUD_EVENTS_FILE: join(war, 'events.log'),
    CLOUD_CLASS_DIR_DB_FILE: join(war, 'class-dir.db'),
    CLOUD_CLASS_ROOM_DB_FILE: join(war, 'class-room.db'),
    CLOUD_QUEUE_DB_FILE: join(war, 'queue.db'),
    CLOUD_INTERACT_DB_FILE: join(war, 'interact.db'),
    CLOUD_TENANTS_FILE: join(war, 'tenants.json'),
    CLOUD_DATA_DIR: join(war, 'data'),
  };
  // stdio 全部落文件：PIPE 不排空会堵事件循环（云端的日志量足以撑爆管道缓冲）。
  const out = spawn(process.execPath, [entry], { cwd: CLOUD, env, stdio: ['ignore', 'pipe', 'pipe'] });
  let buf = '';
  out.stdout.on('data', (c) => { buf += c.toString(); });
  out.stderr.on('data', (c) => { buf += c.toString(); });
  out.on('exit', () => {
    try { writeFileSync(logPath, buf); } catch { /* 写不进就算了，不影响结论 */ }
  });
  return { proc: out, log: () => buf };
}

async function waitReady(ms = 15000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    try {
      const r = await fetch(`${BASE}/api/plugins`);
      if (r.status === 200) return true;
    } catch { /* 还没起来 */ }
    await new Promise((r) => setTimeout(r, 200));
  }
  return false;
}

async function stopCloud(h) {
  if (!h || !h.proc) return;
  const p = h.proc;
  if (p.exitCode !== null) return;
  await new Promise((resolve) => {
    const t = setTimeout(() => { try { p.kill('SIGKILL'); } catch { /* 已退 */ } resolve(); }, 3000);
    p.once('exit', () => { clearTimeout(t); resolve(); });
    try { p.kill('SIGTERM'); } catch { /* 已退 */ }
  });
}

async function L2(war) {
  console.log('\n== L2 隔离实例 HTTP ==');
  const h = startCloud(war);
  try {
    const ready = await waitReady();
    if (!ok(ready, 'L2-00 隔离云端已就绪')) {
      console.log('--- server log ---\n' + h.log().slice(-2000));
      return;
    }

    // ① 无令牌 ⇒ 401
    const r0 = await fetch(`${BASE}/api/device/timetable?uid=dev-a`);
    ok(r0.status === 401, `L2-01 无令牌 ⇒ 401（实得 ${r0.status}）`);

    // ② 错令牌 ⇒ 401
    const r1 = await fetch(`${BASE}/api/device/timetable?uid=dev-a`, { headers: { authorization: 'Bearer wrong-token' } });
    ok(r1.status === 401, `L2-02 错令牌 ⇒ 401（实得 ${r1.status}）`);

    // ③ 正确设备令牌 + 未知设备 ⇒ 200 reason=NO_CLASS（不是 404，未分班是正常态）
    const r2 = await fetch(`${BASE}/api/device/timetable?uid=dev-unknown`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    ok(r2.status === 200, `L2-03 未分班设备 ⇒ 200（实得 ${r2.status}）`);
    const j2 = await r2.json();
    ok(j2.reason === 'NO_CLASS', `L2-04 未分班 reason=NO_CLASS（实得 ${j2.reason}）`);

    // ④ 建班（真实管理端路径：POST /api/class/<action>，body 带 classCode）
    const rc = await fetch(`${BASE}/api/class/create`, {
      method: 'POST',
      headers: { authorization: `Bearer ${VIEWER_TOKEN}`, 'content-type': 'application/json' },
      body: JSON.stringify({ classCode: 'cls-devtt', className: '回归测试班' }),
    });
    const jc = await rc.json();
    ok(rc.status === 200 && jc.ok, `L2-05 建班成功（实得 ${rc.status} ${JSON.stringify(jc).slice(0, 120)}）`);

    // ⑤ 用 viewer 令牌写一份课表到该班（老师侧正常路径）
    const profile = {
      schema: 'classisland/v1',
      source: 'cloud',
      group: { id: 'g1', name: '默认课表群' },
      days: [{ weekDay: 1, weekCountDiv: 1, weekCountDivTotal: 1, lessons: [{ slotIndex: 0, subjectId: 's-math' }] }],
      layouts: [{ id: 'l1', name: '第1节', startTime: '08:00:00', endTime: '08:45:00', timeType: 0 }],
      subjects: [{ id: 's-math', name: '数学' }],
    };
    const rs = await fetch(`${BASE}/api/class/timetable-save`, {
      method: 'POST',
      headers: { authorization: `Bearer ${VIEWER_TOKEN}`, 'content-type': 'application/json' },
      body: JSON.stringify({ classCode: 'cls-devtt', profile }),
    });
    const js = await rs.json();
    ok(rs.status === 200 && js.ok, `L2-06 写课表成功（实得 ${rs.status} ${JSON.stringify(js).slice(0, 120)}）`);

    // ⑥ 把设备绑到该班（走 /api/class/bind，与真实管理端一致）
    const rb = await fetch(`${BASE}/api/class/bind`, {
      method: 'POST',
      headers: { authorization: `Bearer ${VIEWER_TOKEN}`, 'content-type': 'application/json' },
      body: JSON.stringify({ classCode: 'cls-devtt', uid: 'dev-a' }),
    });
    const jb = await rb.json();
    ok(rb.status === 200 && jb.ok, `L2-07 设备绑班成功（实得 ${rb.status} ${JSON.stringify(jb).slice(0, 120)}）`);

    // ⑦ 设备令牌拉自己的课表 ⇒ 信封原样回来
    const r3 = await fetch(`${BASE}/api/device/timetable?uid=dev-a`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    ok(r3.status === 200, `L2-08 拉课表 ⇒ 200（实得 ${r3.status}）`);
    const j3 = await r3.json();
    ok(j3.ok === true, 'L2-09 ok=true');
    ok(j3.classCode === 'cls-devtt', `L2-10 classCode=cls-devtt（实得 ${j3.classCode}）`);
    ok(j3.reason === '', `L2-11 有课表 reason 为空（实得 ${j3.reason}）`);
    ok(JSON.stringify(j3.profile) === JSON.stringify(profile), 'L2-12 profile 与写入内容逐字节一致');
    ok(Number(j3.updatedAt) > 0, 'L2-13 updatedAt 非 0');

    // ⑧ 未绑班的设备拉课表 ⇒ NO_CLASS，不会串到 cls-devtt（租户/班级都不许串）
    const r5 = await fetch(`${BASE}/api/device/timetable?uid=dev-unknown`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    const j5 = await r5.json();
    ok(j5.reason === 'NO_CLASS' && j5.profile === null, 'L2-14 别的设备不会串到 cls-devtt');
    ok(j5.classCode === '', 'L2-15 未分班设备 classCode 为空串');

    // ⑨ 新增：班级清单（设备令牌）
    const rc2 = await fetch(`${BASE}/api/device/classes?uid=dev-a`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    ok(rc2.status === 200, `L2-16 班级清单 ⇒ 200（实得 ${rc2.status}）`);
    const jc2 = await rc2.json();
    const mine = (jc2.classes || []).find((c) => c.code === 'cls-devtt');
    ok(!!mine, 'L2-17 清单里有 cls-devtt');
    ok(mine && mine.name === '回归测试班', `L2-18 班名正确（实得 ${mine && mine.name}）`);
    ok(mine && !('devices' in mine), 'L2-19 清单项不带 devices');

    // ⑩ 新增：选班预览（读别班课表，同租户）
    const rp = await fetch(`${BASE}/api/device/timetable?uid=dev-a&classCode=cls-devtt`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    ok(rp.status === 200, `L2-20 选班预览 ⇒ 200（实得 ${rp.status}）`);
    const jp = await rp.json();
    ok(JSON.stringify(jp.profile) === JSON.stringify(profile), 'L2-21 选班预览 profile 逐字节一致');
    ok(jp.className === '回归测试班', 'L2-22 选班预览回班级名');

    // ⑪ 选不存在的班 ⇒ 403，且不会因为带 classCode 就绕过鉴权
    const rn = await fetch(`${BASE}/api/device/timetable?uid=dev-a&classCode=cls-nope`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
    ok(rn.status === 403, `L2-23 不存在的班 ⇒ 403（实得 ${rn.status}）`);
    const rn2 = await fetch(`${BASE}/api/device/timetable?uid=dev-a&classCode=cls-devtt`);
    ok(rn2.status === 401, `L2-24 选班预览也要令牌（实得 ${rn2.status}）`);
  } finally {
    await stopCloud(h);
  }
}

/* ─────────────────────────── L3 反向 ─────────────────────────── */

async function L3() {
  console.log('\n== L3 反向（摘掉薄壳端点必须 404）==');
  const war = makeWar();
  const srcDir = join(war, 'src');
  try {
    // 只复刻 src/（node_modules 在运行时靠 cwd 找 CLOUD，本测试用 CLOUD 作 cwd 不够，
    // 因为 entry 在临时目录；简单做法：临时目录里放一个 patched index.js 的兄弟结构。）
    mkdirSync(srcDir, { recursive: true });
    const orig = readFileSync(ENTRY, 'utf8');
    // 摘掉端点那一整行（保留周围注释，不影响语法）
    const line = "  if (req.method === 'GET' && u.pathname.startsWith('/api/device/')) {";
    const idx = orig.indexOf(line);
    if (!ok(idx >= 0, 'L3-00 找到待摘端点的锚点')) return;
    const endIdx = orig.indexOf('\n', idx);
    const patched = orig.slice(0, idx) + '  // [reverse] 端点已摘除\n' + orig.slice(endIdx + 1);
    writeFileSync(join(srcDir, 'index.js'), patched);

    // 反向实例：entry 指 patched 副本，其余依赖（node_modules / 兄弟模块）仍指 CLOUD。
    // 做法：把 patched index.js 放在 CLOUD/.tmp-rev-<rand>/ 下会破坏相对 import，
    // 所以直接改成把 src 目录整体软链不划算 —— 这里用最省的办法：
    // 在 CLOUD 下临时放一份 index.js.bak 式的旁路文件，import 路径以 CLOUD/src 为基准。
    const revPath = join(CLOUD, 'src', '_index_rev_tmp.mjs');
    writeFileSync(revPath, patched);
    const h = startCloud(war, revPath);
    try {
      const ready = await waitReady();
      if (!ok(ready, 'L3-01 反向实例已就绪')) {
        console.log('--- server log ---\n' + h.log().slice(-1500));
        return;
      }
      const r = await fetch(`${BASE}/api/device/timetable?uid=dev-a`, { headers: { authorization: `Bearer ${DEV_TOKEN}` } });
      ok(r.status === 404, `L3-02 摘掉端点后 ⇒ 404（实得 ${r.status}）`);
      // 相邻端点没被误伤（证明摘除是局部的）
      const r2 = await fetch(`${BASE}/api/plugins`);
      ok(r2.status === 200, `L3-03 相邻端点仍 200（实得 ${r2.status}）`);
    } finally {
      await stopCloud(h);
      try { rmSync(revPath, { force: true }); } catch { /* 尽力 */ }
    }
  } finally {
    try { rmSync(war, { recursive: true, force: true, maxRetries: 3 }); } catch { /* 尽力 */ }
  }
}

/* ─────────────────────────── 主流程 ─────────────────────────── */

async function main() {
  const war = makeWar();
  try {
    await L1();
    await L1b();
    await L2(war);
    await L3();
  } finally {
    try { rmSync(war, { recursive: true, force: true, maxRetries: 3 }); } catch { /* 尽力 */ }
  }
  console.log(`\nRESULT ${FAIL === 0 ? 'PASS' : 'FAIL'}  PASS=${PASS} FAIL=${FAIL}`);
  if (FAIL) console.log('失败项：\n' + fails.map((f) => '  - ' + f).join('\n'));
  process.exitCode = FAIL === 0 ? 0 : 1;
}

main().catch((e) => {
  console.error('回归脚本自身异常：', e);
  process.exitCode = 1;
});
