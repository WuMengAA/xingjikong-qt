// class-room 的租户隔离单测 —— 2026-11-02 · class_id 全链路多租户
//
// 守的是：**成员表与事件历史不能跨校串台。**
//
// 为什么这条最要紧（背景）：class_members 是**权限表**。老结构主键只有
// (class_code, subject_type, subject_id)。多校之后两所学校都可能用同一个外部码
// `class_0102` —— 在 A 校被加成 teacher 的人，进 B 校同名班**自动拿到 teacher**。
// 这不是显示错位，是权限发放；能改设备、能下发指令。
//
// 事件历史（class_events）同理：两校共用一条时间线，B 校老师能读到 A 校的班级通知。
//
// 跑法：node tests/unit-class-room-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const d = mkdtempSync(join(tmpdir(), 'crtn-'));
process.env.CLOUD_CLASS_ROOM_DB_FILE = join(d, 'room.db');

const { initTenants, closeTenants, registerTenant, DEFAULT_TENANT } = await import('../src/tenant.js');
const {
  initClassRoom, closeClassRoom,
  appendEvent, recentEvents, countEvents, dropRoomEvents,
  addMember, setMemberRole, removeMember, listMembers, roleOf, isMemberOf, classesOf,
  dropRoomMembers, roomStats,
} = await import('../src/class-room.js');

initTenants(join(d, 'tenants.json'));
initClassRoom();

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const A = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;
const CODE = 'class_0102';   // 两校都用的站点外部码

console.log('class-room 租户隔离单测');

// ── 成员（权限表）───────────────────────────────────────────────────
ok('★ A 校加成员，B 校同码班查不到这个人', () => {
  const r = addMember(CODE, { id: 'u-teacher-a', role: 'teacher', by: 'sys' }, A);
  assert.equal(r.ok, true);
  assert.equal(roleOf(CODE, 'u-teacher-a', A), 'teacher');
  assert.equal(roleOf(CODE, 'u-teacher-a', B), null, 'B 校不该看到 A 校成员');
  assert.equal(isMemberOf(CODE, 'u-teacher-a', B), false);
});

ok('★ 同一个人在两校可以是不同角色，互不覆盖', () => {
  addMember(CODE, { id: 'u-shared', role: 'viewer' }, A);
  addMember(CODE, { id: 'u-shared', role: 'teacher' }, B);
  assert.equal(roleOf(CODE, 'u-shared', A), 'viewer');
  assert.equal(roleOf(CODE, 'u-shared', B), 'teacher');
});

ok('★ 不降权逻辑按租户独立（B 校降不下来 A 校的）', () => {
  addMember(CODE, { id: 'u-shared', role: 'owner' }, B);   // B 校升 owner
  assert.equal(roleOf(CODE, 'u-shared', B), 'owner');
  assert.equal(roleOf(CODE, 'u-shared', A), 'viewer', 'A 校不该跟着升');
});

ok('★ 改角色只作用本校', () => {
  const r = setMemberRole(CODE, 'u-shared', 'owner', 'user', A);
  assert.equal(r.ok, true);
  assert.equal(roleOf(CODE, 'u-shared', A), 'owner');
  assert.equal(roleOf(CODE, 'u-shared', B), 'owner', 'B 校本来就是 owner，未被波及');
});

ok('★ 移出成员只作用本校', () => {
  const r = removeMember(CODE, 'u-shared', 'user', A);
  assert.equal(r.removed, true);
  assert.equal(roleOf(CODE, 'u-shared', A), null);
  assert.equal(roleOf(CODE, 'u-shared', B), 'owner', 'B 校成员必须还在');
});

ok('★ 成员清单按租户隔离', () => {
  const la = listMembers(CODE, 'user', A).map((m) => m.id).sort();
  const lb = listMembers(CODE, 'user', B).map((m) => m.id).sort();
  assert.deepEqual(la, ['u-teacher-a']);
  assert.ok(lb.includes('u-shared'));
  assert.ok(!lb.includes('u-teacher-a'), 'B 校清单不该混进 A 校成员');
});

ok('★ classesOf 不传租户 = 全查（我的班级要能列出全部）', () => {
  const all = classesOf('u-teacher-a');
  assert.equal(all.length, 1);
  assert.equal(all[0].tenantId, A);
  const onlyB = classesOf('u-teacher-a', B);
  assert.equal(onlyB.length, 0, '限定 B 校时查不到');
});

ok('★ 删班成员只清本校', () => {
  const r = dropRoomMembers(CODE, A);
  assert.equal(r.deleted, 1);
  assert.equal(listMembers(CODE, 'user', A).length, 0);
  assert.ok(listMembers(CODE, 'user', B).length > 0, 'B 校成员必须还在');
});

// ── 事件历史 ─────────────────────────────────────────────────────────
ok('★ 事件历史按租户隔离', () => {
  appendEvent(CODE, { kind: 'notice-a', data: { text: 'A 校通知' } }, A);
  appendEvent(CODE, { kind: 'notice-b', data: { text: 'B 校通知' } }, B);
  const ea = recentEvents(CODE, 20, A);
  const eb = recentEvents(CODE, 20, B);
  assert.equal(ea.length, 1);
  assert.equal(eb.length, 1);
  assert.equal(ea[0].kind, 'notice-a');
  assert.equal(eb[0].kind, 'notice-b', 'B 校不该读到 A 校事件');
});

ok('★ 热缓存键带租户：先读 A 再读 B 不会串', () => {
  // 上面已读了两校；再连发两条确认缓存没被顶掉
  appendEvent(CODE, { kind: 'notice-a2' }, A);
  appendEvent(CODE, { kind: 'notice-b2' }, B);
  assert.deepEqual(recentEvents(CODE, 20, A).map((e) => e.kind), ['notice-a', 'notice-a2']);
  assert.deepEqual(recentEvents(CODE, 20, B).map((e) => e.kind), ['notice-b', 'notice-b2']);
});

ok('★ countEvents 按租户计数', () => {
  assert.equal(countEvents(CODE, A), 2);
  assert.equal(countEvents(CODE, B), 2);
});

ok('★ dropRoomEvents 只清本校', () => {
  const r = dropRoomEvents(CODE, A);
  assert.equal(r.deleted, 2);
  assert.equal(countEvents(CODE, A), 0);
  assert.equal(countEvents(CODE, B), 2, 'B 校历史必须还在');
  assert.deepEqual(recentEvents(CODE, 20, B).map((e) => e.kind), ['notice-b', 'notice-b2']);
});

ok('★ roomStats 按租户统计', () => {
  const sa = roomStats(CODE, A);
  const sb = roomStats(CODE, B);
  assert.equal(sa.events, 0);
  assert.equal(sb.events, 2);
});

// ── 默认租户（向后兼容）──────────────────────────────────────────────
ok('不传租户 = 默认组织，与两校都互不干扰', () => {
  addMember('class_default_01', { id: 'u-default', role: 'owner' });
  assert.equal(roleOf('class_default_01', 'u-default'), 'owner');
  assert.equal(roleOf('class_default_01', 'u-default', A), null);
  assert.equal(roleOf('class_default_01', 'u-default', DEFAULT_TENANT), 'owner');
  appendEvent('class_default_01', { kind: 'default-ev' });
  assert.equal(countEvents('class_default_01'), 1);
  assert.equal(countEvents('class_default_01', A), 0);
});

closeClassRoom();
closeTenants();
try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
