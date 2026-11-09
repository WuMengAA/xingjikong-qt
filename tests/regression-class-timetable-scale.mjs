// 班级课表「100 班量级」规模回归探针 —— 2026-11-09
//
// 目的：在**隔离的临时 DB** 上把课表读写链路压到 100/300 班，逐条验证
//   UPSERT 不串班 / 多租户隔离 / 512KB 上限 / 主键命中读性能。
//
// 跑法：node tests/regression-class-timetable-scale.mjs
//   （受管 node v24.21.0，node:sqlite 可用）
//
// 硬约束（本文件自我约束，别改）：
//   · 显式把临时路径传给 initClassRoom / initTenants —— 绝不碰生产 class-room.db；
//   · 不连 8788、不改 .env、不重启服务、不写任何 src/ 文件；
//   · 探针只 import 纯函数模块（已核实：这四个模块顶层没有 init 调用，
//     config.js 顶层只读 process.env，import 不会打开生产库）；
//   · 收尾 closeClassRoom() + closeTenants() + 删 db/-wal/-shm，不留垃圾。
//
// 【坑·多租户】normalizeTenantId() 只认「tn_ 前缀 + 已在租户表登记」的 id，
//   否则一律塌回 tn_default。所以字面量 'tenant_a'/'tenant_b' 直接传给
//   saveTimetable/getTimetable 会**双双落进默认租户**，隔离测试变成假的。
//   这里照 unit-class-data-tenant.mjs 的成熟范式：initTenants(临时文件) +
//   registerTenant 登记两个真租户，再用它们的 id 做 D 组断言。

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { initClassRoom, closeClassRoom } from '../src/class-room.js';
import {
  initClassData, saveTimetable, getTimetable, classDataStats,
} from '../src/class-data.js';
import {
  initTenants, closeTenants, registerTenant, DEFAULT_TENANT,
} from '../src/tenant.js';

/* ───────────────────────── 临时环境（全部隔离）──────────────────────── */
const workDir = mkdtempSync(join(tmpdir(), 'ctts-'));
const dbFile = join(workDir, 'room.db');
const tenantsFile = join(workDir, 'tenants.json');

// 双保险：确认我们用的是 tmpdir 下的文件，不是仓库里的生产库。
const hereDir = fileURLToPath(new URL('.', import.meta.url));
assert.ok(dbFile.startsWith(tmpdir()), '临时 DB 不在 os.tmpdir() 下');
assert.ok(!dbFile.startsWith(hereDir), '临时 DB 竟然落在源码目录里（会碰生产）');

/* ───────────────────────── 贴近真实的 classisland/v1 档案 ──────────────
 * 形态照 src/class-timetable.js:246-263 的归一化信封：
 *   schema/source/importedAt/group/days/layouts/subjects
 *   每节 lessons 带 subject/start/end（另带 subjectId/index/initial/teacher）
 * 规格：6 天（周一~周六）、3 套作息、13 个科目、每天 12 节正课。
 * 序列化体积落在 10~20KB，与线上 12 班 profile ~14KB 同量级。
 */
const DAY_NAMES = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];
const SUBJECT_NAMES = ['语文', '数学', '英语', '物理', '化学', '生物', '政治',
  '历史', '地理', '体育', '音乐', '美术', '信息技术'];
const TEACHERS = ['王老师', '李老师', '张老师', '刘老师', '陈老师', '杨老师',
  '赵老师', '黄老师', '周老师', '吴老师', '徐老师', '孙老师', '马老师'];

const hm = (mins) => `${String(Math.floor(mins / 60)).padStart(2, '0')}:${String(mins % 60).padStart(2, '0')}`;

function buildProfile(classCode, tag) {
  const subjects = SUBJECT_NAMES.map((name, i) => ({
    id: `sub-${String(i + 1).padStart(2, '0')}`,
    name,
    initial: name.slice(0, 1),
    teacher: TEACHERS[i],
  }));

  // 3 套作息：每套 12 节正课 + 11 个课间 + 1 个大课间（行动）= 24 项。
  const layouts = [];
  const layoutNames = ['高二通用作息', '周日作息', '周五作息'];
  for (let li = 0; li < 3; li++) {
    const items = [];
    let t = 7 * 60 + 20;                      // 07:20 开始
    for (let n = 1; n <= 12; n++) {
      items.push({ kind: 'lesson', label: `第${n}节`, start: hm(t), end: hm(t + 45), name: '' });
      t += 45;
      if (n === 4) {
        items.push({ kind: 'action', label: '行动', start: hm(t), end: hm(t + 30), name: '大课间' });
        t += 30;
      } else {
        items.push({ kind: 'break', label: `课间${n}`, start: hm(t), end: hm(t + 10), name: '课间休息' });
        t += 10;
      }
    }
    layouts.push({ id: `tl-${li + 1}`, name: layoutNames[li], items });
  }

  // 6 天，每天 12 节（从当天作息里取正课时段），科目按 (序号+天*3) 轮转。
  const days = [];
  for (let d = 1; d <= 6; d++) {
    const layout = layouts[(d - 1) % 3];
    const slots = layout.items.filter((x) => x.kind === 'lesson');
    const lessons = slots.map((slot, i) => {
      const s = subjects[(i + d * 3) % subjects.length];
      return {
        index: i + 1,
        subjectId: s.id,
        subject: s.name,
        initial: s.initial,
        teacher: s.teacher,
        start: slot.start,
        end: slot.end,
      };
    });
    days.push({ dow: d, name: DAY_NAMES[d], timeLayoutId: layout.id, lessons });
  }

  return {
    schema: 'classisland/v1',
    source: 'classisland',
    importedAt: '2026-11-09T00:00:00.000Z',
    group: {
      id: `grp-${classCode}`,
      name: `${classCode}课表群`,
      label: classCode,
      index: 1,
      inferred: false,
      nameSource: 'declared',
    },
    days,
    layouts,
    subjects,
    // 探针标记：让每个班的档案互不相同，才能抓出「串班」；同时区分原值/覆盖值。
    probeTag: tag,
  };
}

const bytesOf = (p) => Buffer.byteLength(JSON.stringify(p), 'utf8');
const hashOf = (p) => createHash('sha256').update(JSON.stringify(p), 'utf8').digest('hex');
const codeOf = (i) => `class_${String(i).padStart(3, '0')}`;

/* ───────────────────────── 断言框架 ───────────────────────── */
let pass = 0;
let fail = 0;
function check(name, fn) {
  try { fn(); pass++; console.log(`  PASS ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e && e.message ? e.message : e}`); }
}

console.log('星集控云端 · 班级课表 100 班量级规模回归探针');
console.log(`临时 DB : ${dbFile}`);
console.log(`临时租户: ${tenantsFile}`);

// 实测数字（G 组打印）。
const perf = { write100Ms: 0, read100Ms: 0, write300Ms: 0, avgGetMs: 0, profileBytes: 0, distinctUpdatedAt: 0 };

let cleanErr = null;
function cleanup() {
  try { closeClassRoom(); } catch (e) { cleanErr = cleanErr || e; }
  try { closeTenants(); } catch (e) { cleanErr = cleanErr || e; }
  // 显式点名三个文件（即便 mkdtemp 目录递归删也覆盖），再兜底删整目录。
  for (const f of [dbFile, `${dbFile}-wal`, `${dbFile}-shm`, tenantsFile]) {
    try { rmSync(f, { force: true }); } catch (e) { cleanErr = cleanErr || e; }
  }
  try { rmSync(workDir, { recursive: true, force: true }); } catch (e) { cleanErr = cleanErr || e; }
}

try {
  /* 初始化：全部显式传临时路径，绝不触发生产 class-room.db / tenants.json。 */
  initTenants(tenantsFile);
  initClassRoom(dbFile);
  const initInfo = initClassData();
  assert.equal(initInfo.ok, true, 'initClassData 未 ok');
  assert.equal(initInfo.timetables, 0, '临时库起始不该有课表行');

  const tenantA = registerTenant({ name: '规模回归甲校', id: 'tn_scale_a' }).tenant.id;
  const tenantB = registerTenant({ name: '规模回归乙校', id: 'tn_scale_b' }).tenant.id;
  assert.notEqual(tenantA, tenantB, '两个租户 id 不该相同');
  assert.notEqual(tenantA, DEFAULT_TENANT, '甲校不该塌回默认租户');
  assert.notEqual(tenantB, DEFAULT_TENANT, '乙校不该塌回默认租户');

  // 100 个班各自的原值档案（互不相同）。
  const originals = new Map();
  for (let i = 1; i <= 100; i++) originals.set(codeOf(i), buildProfile(codeOf(i), 'orig'));

  /* ── P 档案体积（先于写入，独立于 DB）──────────────────────────── */
  check('P 生成的 classisland/v1 档案 6天/3作息/13科目，序列化 10~20KB', () => {
    const p = originals.get('class_001');
    assert.equal(p.schema, 'classisland/v1');
    assert.equal(p.days.length, 6, 'days 应为 6');
    assert.equal(p.layouts.length, 3, 'layouts 应为 3');
    assert.equal(p.subjects.length, 13, 'subjects 应为 13');
    assert.equal(p.days[0].lessons.length, 12, '每天应 12 节');
    const l = p.days[0].lessons[0];
    assert.ok(l.subject && l.start && l.end, '每节需带 subject/start/end');
    const b = bytesOf(p);
    perf.profileBytes = b;
    assert.ok(b >= 10 * 1024 && b <= 20 * 1024, `档案 ${b} 字节，不在 10~20KB`);
  });

  /* ── A 写入 100 班，全部 ok + updatedAt 递增有序 ─────────────────── */
  const writeResults = [];
  const tA0 = performance.now();
  for (let i = 1; i <= 100; i++) writeResults.push(saveTimetable(codeOf(i), originals.get(codeOf(i)), 'scale-probe'));
  const tA1 = performance.now();
  perf.write100Ms = tA1 - tA0;

  check('A 写入 100 班全部 ok，updatedAt 递增有序（Date.now 单调递增）', () => {
    assert.equal(writeResults.length, 100);
    const stamps = [];
    for (let i = 0; i < 100; i++) {
      const r = writeResults[i];
      assert.equal(r && r.ok, true, `第 ${i + 1} 班写入不 ok：${r && r.error}`);
      assert.equal(r.bytes, bytesOf(originals.get(codeOf(i + 1))), '写入字节数应等于档案序列化长度');
      stamps.push(r.updatedAt);
    }
    // 紧密循环里 Date.now() 可能同一毫秒 ⇒ 断言「非递减」（递增有序），
    // 并额外确认时间确实前进过（至少 2 个不同值），而非恒 0。
    for (let i = 1; i < stamps.length; i++) {
      assert.ok(stamps[i] >= stamps[i - 1], `updatedAt 在 ${i} 处回退`);
    }
    assert.ok(stamps[0] > 0, 'updatedAt 应 > 0');
    assert.ok(stamps[99] >= stamps[0], '起止时间应递增');
    perf.distinctUpdatedAt = new Set(stamps).size;
    assert.ok(perf.distinctUpdatedAt >= 2, '时间戳应至少跨越 2 个毫秒值');
  });

  /* ── B 100 班回读字节一致（深比对 + sha256，非只看 ok）──────────── */
  const tB0 = performance.now();
  let bOk = true;
  const bErr = [];
  for (let i = 1; i <= 100; i++) {
    const code = codeOf(i);
    const g = getTimetable(code);
    if (!g || g.ok !== true) { bOk = false; bErr.push(`${code} 读回 ok 非 true`); continue; }
    if (g.profile === null) { bOk = false; bErr.push(`${code} 读回 profile 为 null`); continue; }
    // 深比对：结构逐字段相等。
    try { assert.deepStrictEqual(g.profile, originals.get(code)); }
    catch { bOk = false; bErr.push(`${code} 深比对不一致`); }
    // 字节级：序列化后 sha256 一致。
    if (hashOf(g.profile) !== hashOf(originals.get(code))) {
      bOk = false; bErr.push(`${code} 序列化字节 hash 不一致`);
    }
  }
  const tB1 = performance.now();
  perf.read100Ms = tB1 - tB0;
  perf.avgGetMs = perf.read100Ms / 100;

  check('B 100 班回读全部字节一致（deepStrictEqual + sha256，非只看 ok）', () => {
    assert.equal(bOk, true, bErr.slice(0, 5).join(' | '));
  });

  /* ── C 覆盖 20 班：新值生效、其余 80 班原样、总行数仍 100 ───────── */
  const overwriteCodes = [];
  for (let i = 1; i <= 100; i += 5) overwriteCodes.push(codeOf(i)); // 1,6,11,...96 = 20 个
  const overwritten = new Map();
  for (const code of overwriteCodes) {
    const p = buildProfile(code, 'overwrite');
    overwritten.set(code, p);
    const r = saveTimetable(code, p, 'scale-probe-overwrite');
    assert.equal(r && r.ok, true, `覆盖写 ${code} 失败`);
  }
  check('C UPSERT 覆盖 20 班：读回新值、其余 80 班原样、恰好 100 行', () => {
    assert.equal(overwriteCodes.length, 20, '应恰好覆盖 20 个班');
    for (let i = 1; i <= 100; i++) {
      const code = codeOf(i);
      const g = getTimetable(code);
      assert.equal(g && g.ok, true, `${code} 读回失败`);
      const expect = overwritten.has(code) ? overwritten.get(code) : originals.get(code);
      assert.deepStrictEqual(g.profile, expect, `${code} 内容串了（UPSERT 串班？）`);
      assert.equal(hashOf(g.profile), hashOf(expect), `${code} 字节串了`);
    }
    // initClassData() 幂等且回传全表行数：UPSERT 不该长出新行。
    const info = initClassData();
    assert.equal(info.timetables, 100, `覆盖后应有 100 行，实为 ${info.timetables}`);
  });

  /* ── D 多租户：同 class_code 在两租户各存一份，互不可见 ─────────── */
  const SHARED = 'class_shared_tenant';
  const pa = buildProfile(SHARED, 'tenant-A');
  const pb = buildProfile(SHARED, 'tenant-B');
  const rA = saveTimetable(SHARED, pa, 'probe', tenantA);
  const rB = saveTimetable(SHARED, pb, 'probe', tenantB);
  check('D 多租户：同 class_code 两租户各一份互不可见（主键含 tenant_id）', () => {
    assert.equal(rA.ok, true); assert.equal(rB.ok, true);
    assert.equal(rA.tenantId, tenantA); assert.equal(rB.tenantId, tenantB);
    const ga = getTimetable(SHARED, tenantA);
    const gb = getTimetable(SHARED, tenantB);
    assert.deepStrictEqual(ga.profile, pa, '甲校读回应是甲校档案');
    assert.deepStrictEqual(gb.profile, pb, '乙校读回应是乙校档案');
    assert.notDeepStrictEqual(ga.profile, gb.profile, '两租户不该看到同一份');
    assert.equal(ga.profile.probeTag, 'tenant-A');
    assert.equal(gb.profile.probeTag, 'tenant-B');
    // 默认租户既没写过、也不该串到任一租户的数据。
    assert.equal(getTimetable(SHARED, DEFAULT_TENANT).profile, null, '默认租户不该串到别校课表');
    // classDataStats 也必须按租户分账。
    assert.equal(classDataStats(SHARED, tenantA).hasTimetable, true);
    assert.equal(classDataStats(SHARED, tenantB).hasTimetable, true);
    assert.equal(classDataStats(SHARED, DEFAULT_TENANT).hasTimetable, false);
  });

  /* ── E 规模到 300 班仍全部 ok ─────────────────────────────────── */
  const tE0 = performance.now();
  let eOk = true;
  const eErr = [];
  for (let i = 1; i <= 300; i++) {
    const r = saveTimetable(codeOf(i), buildProfile(codeOf(i), 'e300'), 'scale-probe-300');
    if (!r || r.ok !== true) { eOk = false; if (eErr.length < 5) eErr.push(`${codeOf(i)}: ${r && r.error}`); }
  }
  const tE1 = performance.now();
  perf.write300Ms = tE1 - tE0;
  check('E 规模到 300 班仍全部 ok', () => {
    assert.equal(eOk, true, eErr.join(' | '));
    // 抽查 3 个班读回正常（尾部尤其重要：确认不是只写了前几个）。
    for (const i of [1, 150, 300]) {
      const g = getTimetable(codeOf(i));
      assert.equal(g && g.ok, true, `${codeOf(i)} 读回失败`);
      assert.equal(g.profile.probeTag, 'e300', `${codeOf(i)} 内容不对`);
    }
    const info = initClassData();
    // 默认租户 300 行 + D 组两个租户各 1 行 = 302（全表跨租户计数）。
    assert.equal(info.timetables, 302, `全表应有 302 行，实为 ${info.timetables}`);
  });

  /* ── F 512KB 上限生效：超限必须 ok:false 且 error 含 KB ────────── */
  const huge = buildProfile('class_huge', 'huge');
  huge.bloat = 'x'.repeat(600 * 1024);   // >512KB
  const rf = saveTimetable('class_huge', huge, 'scale-probe-huge');
  check('F 512KB 上限：塞 >512KB 档案必须 ok:false 且 error 含 KB', () => {
    assert.ok(bytesOf(huge) > 512 * 1024, '构造的档案应确实超过 512KB');
    assert.equal(rf.ok, false, '超限不该写入成功');
    assert.match(String(rf.error || ''), /KB/, `error 应含 KB 字样，实为：${rf.error}`);
    // 确认真没落库（不是只回错却偷偷写了）。
    assert.equal(getTimetable('class_huge').profile, null, '超限档案不该落库');
  });

  /* ── G 性能数字 + 单次 get 平均 < 5ms ─────────────────────────── */
  check('G 性能：单次 getTimetable 平均 < 5ms（主键命中，非全表扫）', () => {
    console.log('       ---- 实测耗时 ----');
    console.log(`       档案体积            : ${perf.profileBytes} B (${(perf.profileBytes / 1024).toFixed(1)} KiB)`);
    console.log(`       100 班批量写        : ${perf.write100Ms.toFixed(2)} ms`);
    console.log(`       100 班批量读(含校验): ${perf.read100Ms.toFixed(2)} ms`);
    console.log(`       300 班批量写        : ${perf.write300Ms.toFixed(2)} ms`);
    console.log(`       单次 get 平均       : ${perf.avgGetMs.toFixed(4)} ms  (read100 / 100)`);
    console.log(`       updatedAt 不同毫秒值: ${perf.distinctUpdatedAt} / 100`);
    assert.ok(perf.avgGetMs < 5, `单次 get 平均 ${perf.avgGetMs.toFixed(4)}ms，应 < 5ms`);
  });

} catch (fatal) {
  fail++;
  console.error(`  FAIL FATAL 未捕获异常\n       ${fatal && fatal.stack ? fatal.stack : fatal}`);
} finally {
  /* ── H 收尾：关连接 + 删临时 DB(含 -wal/-shm)，不留垃圾 ─────────── */
  cleanup();
  check('H 收尾 closeClassRoom + 删临时 DB/-wal/-shm，不留垃圾', () => {
    if (cleanErr) throw cleanErr;
    assert.equal(existsSync(dbFile), false, 'db 文件残留');
    assert.equal(existsSync(`${dbFile}-wal`), false, '-wal 残留');
    assert.equal(existsSync(`${dbFile}-shm`), false, '-shm 残留');
    assert.equal(existsSync(workDir), false, '临时目录残留');
  });
}

console.log(`\n汇总：${pass} PASS / ${fail} FAIL`);
console.log(`实测：写100=${perf.write100Ms.toFixed(2)}ms 读100=${perf.read100Ms.toFixed(2)}ms 写300=${perf.write300Ms.toFixed(2)}ms avgGet=${perf.avgGetMs.toFixed(4)}ms 档案=${perf.profileBytes}B`);
process.exit(fail > 0 ? 1 : 0);
