// class-attendance 的租户隔离单测 —— 2026-11-02 · class_id 全链路多租户
//
// 守的是：**考勤记录不能跨校覆盖。**
//
// 为什么这条最要紧（背景）：class_attendance 的老自然键只有
// (class_code, student_id, date, session)。多校之后两所学校都可能用站点老外部码
// `class_0102`，而**学生 id / 学号都是各自从 01 起的**。不按租户隔离的话：
//   · A 校老师点「全员出勤」→ B 校当天的缺勤被 upsert 覆盖，且不报错；
//   · B 校学号 01 会解析到 A 校名单里的某个学生（考勤记到隔壁学校的人头上）。
// 考勤是要拿去对账、写家长通知的，静默错比对不上更糟。
//
// 跑法：node tests/unit-class-attendance-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const d = mkdtempSync(join(tmpdir(), 'catn-'));
process.env.CLOUD_CLASS_ROOM_DB_FILE = join(d, 'room.db');

const { initTenants, closeTenants, registerTenant, DEFAULT_TENANT } = await import('../src/tenant.js');
const { initClassRoom, closeClassRoom } = await import('../src/class-room.js');
const { initClassData, addStudent, listStudents } = await import('../src/class-data.js');
const {
  initClassAttendance, recordAttendance, batchRecord,
  listByClassDate, listByClassRange, getAttendance,
  updateAttendance, removeAttendance, attendanceStats,
  dropClassAttendance, attendanceCount,
} = await import('../src/class-attendance.js');

initTenants(join(d, 'tenants.json'));
initClassRoom();
initClassData();
initClassAttendance();

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const A = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;
const CODE = 'class_0102';   // 两校都用的站点外部码
const DAY = '2026-11-02';

// 两校各导一名「学号 01」的学生 —— 这是多校最常见的真实情况
const aStu = addStudent(CODE, { name: '张A', studentNo: '01' }, 'ta', A).student;
const bStu = addStudent(CODE, { name: '李B', studentNo: '01' }, 'tb', B).student;

console.log('class-attendance 租户隔离单测');

// ── 学生解析 ────────────────────────────────────────────────────────
ok('★ 学号 01 按租户解析到本校学生（B 校不会解析到 A 校的人）', () => {
  const ra = recordAttendance(CODE, { studentNo: '01', date: DAY, status: 'present' }, 'ta', A);
  assert.equal(ra.ok, true);
  assert.equal(ra.record.studentName, '张A', 'A 校应解析到张A');

  const rb = recordAttendance(CODE, { studentNo: '01', date: DAY, status: 'absent' }, 'tb', B);
  assert.equal(rb.ok, true);
  assert.equal(rb.record.studentName, '李B', 'B 校应解析到李B，不是张A');
});

ok('★ studentId 也按租户解析（拿错校的 id 必须拒）', () => {
  const r = recordAttendance(CODE, { studentId: bStu.id, date: DAY, status: 'present' }, 'ta', A);
  assert.equal(r.ok, false, 'A 校不该能用 B 校的学生 id 记考勤');
});

// ── 记录隔离 ────────────────────────────────────────────────────────
ok('★ 两校同码班同一天的状态互不覆盖', () => {
  const la = listByClassDate(CODE, DAY, '', A);
  const lb = listByClassDate(CODE, DAY, '', B);
  assert.equal(la.count, 1);
  assert.equal(lb.count, 1);
  assert.equal(la.records[0].status, 'present');
  assert.equal(lb.records[0].status, 'absent', 'B 校状态不该被 A 校覆盖');
});

ok('★ 重复记录只在本校 upsert（不新增行、不动别校）', () => {
  const r = recordAttendance(CODE, { studentNo: '01', date: DAY, status: 'late' }, 'ta', A);
  assert.equal(r.replaced, true, 'A 校同键应为 upsert');
  assert.equal(listByClassDate(CODE, DAY, '', A).count, 1);
  assert.equal(listByClassDate(CODE, DAY, '', B).count, 1);
  assert.equal(listByClassDate(CODE, DAY, '', A).records[0].status, 'late');
  assert.equal(listByClassDate(CODE, DAY, '', B).records[0].status, 'absent');
});

ok('★ 批量记录按租户隔离', () => {
  const r = batchRecord(CODE, [
    { studentNo: '01', date: '2026-11-03', status: 'present' },
  ], 'ta', A);
  assert.equal(r.ok, true);
  assert.equal(r.succeeded, 1);
  assert.equal(listByClassDate(CODE, '2026-11-03', '', A).count, 1);
  assert.equal(listByClassDate(CODE, '2026-11-03', '', B).count, 0, 'B 校不该有这条');
});

ok('★ 区间查询按租户隔离', () => {
  const ra = listByClassRange(CODE, '2026-11-01', '2026-11-30', A);
  const rb = listByClassRange(CODE, '2026-11-01', '2026-11-30', B);
  assert.equal(ra.count, 2);
  assert.equal(rb.count, 1);
  assert.ok(rb.records.every((x) => x.studentName === '李B'));
  assert.ok(ra.records.every((x) => x.studentName === '张A'));
});

ok('★ getAttendance 按租户取不到别校的', () => {
  assert.equal(getAttendance(CODE, aStu.id, DAY, '', A).ok, true);
  assert.equal(getAttendance(CODE, aStu.id, DAY, '', B).ok, false, 'B 校取不到 A 校记录');
});

// ── 改 / 删 ─────────────────────────────────────────────────────────
ok('★ 改判只作用本校', () => {
  const r = updateAttendance(CODE, aStu.id, DAY, '', { status: 'leave' }, 'ta', A);
  assert.equal(r.ok, true);
  assert.equal(getAttendance(CODE, aStu.id, DAY, '', A).record.status, 'leave');
  assert.equal(getAttendance(CODE, bStu.id, DAY, '', B).record.status, 'absent', 'B 校不该被波及');
});

ok('★ 删一条只作用本校', () => {
  const r = removeAttendance(CODE, aStu.id, DAY, '', A);
  assert.equal(r.removed, 1);
  assert.equal(getAttendance(CODE, aStu.id, DAY, '', A).ok, false);
  assert.equal(getAttendance(CODE, bStu.id, DAY, '', B).ok, true, 'B 校记录必须还在');
});

// ── 统计 ────────────────────────────────────────────────────────────
ok('★ attendanceStats 只统计本校（名册人数也按租户算）', () => {
  // A 校已删掉 DAY 那条 ⇒ 0 条；B 校 1 条
  const sa = attendanceStats(CODE, DAY, A);
  const sb = attendanceStats(CODE, DAY, B);
  assert.equal(sa.recorded, 0);
  assert.equal(sa.rosterSize, 1, 'A 校名册 1 人');
  assert.equal(sa.unrecorded, 1);
  assert.equal(sb.recorded, 1);
  assert.equal(sb.rosterSize, 1, 'B 校名册也 1 人 —— 不能算成 2');
  assert.deepEqual(sb.counts, { present: 0, late: 0, leave: 0, absent: 1 });
});

// ── 删除与计数 ──────────────────────────────────────────────────────
ok('★ attendanceCount 按租户计数', () => {
  assert.equal(attendanceCount(CODE, A).count, 1);   // 11-03 那条还在
  assert.equal(attendanceCount(CODE, B).count, 1);
});

ok('★ dropClassAttendance 只清本校', () => {
  const r = dropClassAttendance(CODE, A);
  assert.equal(r.removed, 1);
  assert.equal(attendanceCount(CODE, A).count, 0);
  assert.equal(attendanceCount(CODE, B).count, 1, 'B 校考勤必须还在');
});

// ── 默认租户（向后兼容）──────────────────────────────────────────────
ok('不传租户 = 默认组织，与两校互不干扰', () => {
  addStudent('class_default_03', { name: '默认生', studentNo: '01' });
  const r = recordAttendance('class_default_03', { studentNo: '01', date: DAY, status: 'present' });
  assert.equal(r.ok, true);
  assert.equal(r.record.studentName, '默认生');
  assert.equal(attendanceCount('class_default_03').count, 1);
  assert.equal(attendanceCount('class_default_03', A).count, 0);
  assert.equal(attendanceCount('class_default_03', DEFAULT_TENANT).count, 1);
});

ok('默认组织的统计不与两校名册混淆', () => {
  const s = attendanceStats('class_default_03', DAY);
  assert.equal(s.rosterSize, 1);
});

closeClassRoom();
closeTenants();
try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
