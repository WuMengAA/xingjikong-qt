// class-data 的租户隔离单测 —— 2026-11-02 · class_id 全链路多租户
//
// 守的是：**学生名单与班级课表不能跨校串台。**
//
// 为什么这条最要紧（背景）：class_students / class_timetable 老表的主键只有
// class_code。多校之后两所学校都可能用站点老外部码（class_0102）——A 校老师的
// 学生名单会直接出现在 B 校的「同名班」里，而且 B 校能删能改。这不是显示问题，
// 是同一个存储槽位被两校共用。
//
// 跑法：node tests/unit-class-data-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const d = mkdtempSync(join(tmpdir(), 'cdtn-'));
process.env.CLOUD_CLASS_ROOM_DB_FILE = join(d, 'room.db');

const { initTenants, closeTenants, registerTenant, DEFAULT_TENANT } = await import('../src/tenant.js');
const { initClassRoom, closeClassRoom } = await import('../src/class-room.js');
const {
  initClassData, listStudents, addStudent, updateStudent, removeStudent,
  importStudents, getTimetable, saveTimetable, dropClassData, classDataStats,
} = await import('../src/class-data.js');

initTenants(join(d, 'tenants.json'));
initClassRoom();
initClassData();

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const A = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;
const CODE = 'class_0102';   // 两校都用的站点外部码

console.log('class-data 租户隔离单测');

// ── 学生名单 ─────────────────────────────────────────────────────────
ok('★ A 校加学生，B 校同码班必须看不到', () => {
  const r = addStudent(CODE, { name: '张三', studentNo: '01' }, 'teacherA', A);
  assert.equal(r.ok, true);
  assert.equal(listStudents(CODE, A).count, 1);
  assert.equal(listStudents(CODE, B).count, 0, 'B 校不该看到 A 校的学生');
});

ok('★ B 校同码班自己加学生，两份互不干扰', () => {
  addStudent(CODE, { name: '李四', studentNo: '02' }, 'teacherB', B);
  assert.deepEqual(listStudents(CODE, A).students.map((s) => s.name), ['张三']);
  assert.deepEqual(listStudents(CODE, B).students.map((s) => s.name), ['李四']);
});

ok('★ 学号幂等键按租户隔离：两校都能用 01 号', () => {
  const r = addStudent(CODE, { name: '王五', studentNo: '01' }, 'teacherB', B);
  assert.equal(r.ok, true);
  assert.equal(r.replaced, false, 'B 校的 01 号不该顶掉 A 校的 01 号');
  assert.equal(listStudents(CODE, B).count, 2);
  assert.equal(listStudents(CODE, A).count, 1, 'A 校仍只有 1 人');
});

ok('★ 改学生只能改本校的（拿错租户的 id 无效）', () => {
  const aStu = listStudents(CODE, A).students[0];
  const r = updateStudent(CODE, aStu.id, { name: '被B改了' }, 'teacherB', B);
  assert.equal(r.ok, false, 'B 校用 A 校的学生 id 必须失败');
  assert.equal(listStudents(CODE, A).students[0].name, '张三', 'A 校数据不该被动');
});

ok('★ 删学生只能删本校的', () => {
  const aStu = listStudents(CODE, A).students[0];
  const r = removeStudent(CODE, aStu.id, B);
  assert.equal(r.removed, 0, 'B 校删不掉 A 校学生');
  assert.equal(listStudents(CODE, A).count, 1);
});

ok('★ 批量导入按租户隔离', () => {
  const r = importStudents(CODE, [
    { name: '赵六', studentNo: '10' },
    { name: '钱七', studentNo: '11' },
  ], 'teacherA', A);
  assert.equal(r.ok, true);
  assert.equal(r.added, 2);
  assert.equal(listStudents(CODE, A).count, 3, 'A 校 3 人');
  assert.equal(listStudents(CODE, B).count, 2, 'B 校仍是 2 人');
});

// ── 课表 ─────────────────────────────────────────────────────────────
ok('★ 课表按租户隔离（两校同码班各存一份）', () => {
  const pa = { subjects: ['语文', '数学'], tag: 'A校' };
  const pb = { subjects: ['英语'], tag: 'B校' };
  assert.equal(saveTimetable(CODE, pa, 'teacherA', A).ok, true);
  assert.equal(saveTimetable(CODE, pb, 'teacherB', B).ok, true);
  assert.deepEqual(getTimetable(CODE, A).profile, pa);
  assert.deepEqual(getTimetable(CODE, B).profile, pb);
});

ok('★ 没存过课表的租户 → profile:null，不串用别校的', () => {
  const r = getTimetable('class_9999', A);
  assert.equal(r.profile, null);
});

ok('★ 课表覆盖写只影响本校（ON CONFLICT 的键带租户）', () => {
  saveTimetable(CODE, { subjects: ['覆盖后'] }, 'teacherA', A);
  assert.deepEqual(getTimetable(CODE, A).profile, { subjects: ['覆盖后'] });
  assert.deepEqual(getTimetable(CODE, B).profile, { subjects: ['英语'], tag: 'B校' }, 'B 校课表不该被 A 覆盖');
});

// ── 统计与删除 ────────────────────────────────────────────────────────
ok('★ classDataStats 只统计本校', () => {
  const sa = classDataStats(CODE, A);
  const sb = classDataStats(CODE, B);
  assert.equal(sa.students, 3);
  assert.equal(sb.students, 2);
  assert.equal(sa.hasTimetable, true);
});

ok('★ 删班数据只清本校，别校原样', () => {
  const r = dropClassData(CODE, A);
  assert.equal(r.ok, true);
  assert.equal(r.students, 3);
  assert.equal(listStudents(CODE, A).count, 0);
  assert.equal(listStudents(CODE, B).count, 2, 'B 校名单必须还在');
  assert.deepEqual(getTimetable(CODE, B).profile, { subjects: ['英语'], tag: 'B校' }, 'B 校课表必须还在');
});

// ── 默认租户（向后兼容）────────────────────────────────────────────────
ok('不传租户 = 默认组织（既有调用方行为不变）', () => {
  assert.equal(addStudent('class_default_01', { name: '默认生' }).ok, true);
  assert.equal(listStudents('class_default_01').count, 1);
  assert.equal(listStudents('class_default_01', DEFAULT_TENANT).count, 1);
  assert.equal(listStudents('class_default_01', A).count, 0, '默认组织的数据不属于 A 校');
});

ok('默认组织的课表也独立', () => {
  saveTimetable('class_default_02', { subjects: ['默认课'] });
  assert.deepEqual(getTimetable('class_default_02').profile, { subjects: ['默认课'] });
  assert.equal(getTimetable('class_default_02', A).profile, null);
});

closeClassRoom();
closeTenants();
try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
