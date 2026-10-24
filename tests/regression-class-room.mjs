// 班级房间（持久房间）· 单元回归 —— 2026-10-24
//
// 验证四件事（都是用户诉求的直接对应项，不是凑数）：
//   ① 「数据保存在云端」= 重启不丢 → 关库重开，历史与成员原样还在；
//   ② 「人可以被添加」= 成员表能加能移，且角色不因重复加而意外变化；
//   ③ 「永不停息」但内存有界 → 3000 班不可能全进内存，LRU 淘汰后仍能从库读回；
//   ④ 环形裁剪 = 库不会无限长大（默认每班 200 条）。
//
// 跑法：node tests/regression-class-room.mjs
// 用临时目录里的独立库，绝不碰生产 class-room.db。

import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';
import {
  initClassRoom, closeClassRoom, appendEvent, recentEvents, countEvents,
  addMember, setMemberRole, removeMember, listMembers, roleOf, isMemberOf, classesOf,
  dropRoomEvents, dropRoomMembers, roomStats, maybePrune, CLASS_ROLES,
} from '../src/class-room.js';

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ste-room-'));
const DB = path.join(dir, 'room-test.db');

let pass = 0;
let fail = 0;
function ok(name, cond, detail = '') {
  if (cond) { pass++; console.log(`  ok   ${name}`); }
  else { fail++; console.log(`  FAIL ${name}${detail ? ` — ${detail}` : ''}`); }
}

console.log('班级房间（持久房间）· 单元回归');

/* ① 初始化与自增 id */
{
  const r = initClassRoom(DB);
  ok('初始化：库文件建好、成员表可读', r.ok && fs.existsSync(DB));
  const a = appendEvent('cls-aaa111', { kind: 'test.alpha', uid: 'dev1', data: { n: 1 } });
  const b = appendEvent('cls-aaa111', { kind: 'test.beta', uid: 'dev1', data: { n: 2 } });
  ok('写入：两条事件各自拿到单调自增 id', a.ok && b.ok && b.id > a.id, `a=${a.id} b=${b.id}`);
  const evs = recentEvents('cls-aaa111', 10);
  ok('读取：正序返回、载荷完整', evs.length === 2 && evs[0].kind === 'test.alpha'
    && evs[1].data.n === 2);
}

/* ② 重启不丢（关库重开 = 模拟云端重启） */
{
  closeClassRoom();
  initClassRoom(DB);
  const evs = recentEvents('cls-aaa111', 10);
  ok('重启后历史仍在（这是"数据保存在云端"的硬指标）', evs.length === 2 && evs[1].data.n === 2);
  ok('重启后计数正确', countEvents('cls-aaa111') === 2);
}

/* ③ 成员：加／查／幂等／不降权 */
{
  const r1 = addMember('cls-aaa111', { id: 'teacher-1', role: 'teacher', by: 'admin' });
  ok('加成员：新成员 added=true', r1.ok && r1.added === true && r1.role === 'teacher');
  ok('查角色：roleOf 命中', roleOf('cls-aaa111', 'teacher-1') === 'teacher');
  ok('isMemberOf 一致', isMemberOf('cls-aaa111', 'teacher-1') === true);

  const r2 = addMember('cls-aaa111', { id: 'teacher-1', role: 'viewer' });
  ok('幂等且不降权：重复加不会把 teacher 降成 viewer',
    r2.ok && r2.added === false && r2.role === 'teacher', `实际 ${r2.role}`);

  const r3 = addMember('cls-aaa111', { id: 'teacher-1', role: 'owner' });
  ok('重复加以更高角色 → 升级到 owner', r3.ok && r3.role === 'owner');

  const r4 = setMemberRole('cls-aaa111', 'teacher-1', 'viewer');
  ok('显式 setMemberRole 可以降权（降权只走这条路）',
    r4.ok && roleOf('cls-aaa111', 'teacher-1') === 'viewer');

  addMember('cls-aaa111', { id: 'teacher-2', role: 'viewer' });
  addMember('cls-aaa111', { id: 'teacher-3', role: 'teacher' });
  const list = listMembers('cls-aaa111');
  ok('清单：按角色强弱排序（teacher 在 viewer 前）',
    list.length === 3 && list[0].role === 'teacher', JSON.stringify(list.map((m) => m.role)));

  const my = classesOf('teacher-1');
  ok('反查：classesOf 能列出该账号所在的班', my.length === 1 && my[0].classCode === 'cls-aaa111');
}

/* ④ 成员跨班级隔离 */
{
  ok('隔离：不是别的班的成员', isMemberOf('cls-bbb222', 'teacher-1') === false);
  addMember('cls-bbb222', { id: 'teacher-1', role: 'teacher' });
  ok('加进第二个班后，两个班各自独立',
    classesOf('teacher-1').length === 2 && roleOf('cls-bbb222', 'teacher-1') === 'teacher');
}

/* ⑤ 成员持久化（重启不丢 = 加的人一直在） */
{
  closeClassRoom();
  initClassRoom(DB);
  ok('重启后成员仍在（不能被重启"踢出班"）',
    roleOf('cls-aaa111', 'teacher-1') === 'viewer' && listMembers('cls-aaa111').length === 3);
}

/* ⑥ 移出成员 */
{
  const r = removeMember('cls-aaa111', 'teacher-3');
  ok('移出：removed=true 且角色查询回 null', r.ok && r.removed === true
    && roleOf('cls-aaa111', 'teacher-3') === null);
  const again = removeMember('cls-aaa111', 'teacher-3');
  ok('移出幂等：再移一次不报错、removed=false', again.ok && again.removed === false);
}

/* ⑦ 环形裁剪：库不会无限长大 */
{
  const code = 'cls-prune7';
  const KEEP = 200;   // 与 CLASS_EVENT_LOG_KEEP 默认值一致
  for (let i = 0; i < KEEP + 60; i++) {
    appendEvent(code, { kind: 'spam', uid: 'dev9', data: { i } });
  }
  maybePrune(code, 1);   // 强制裁一次（生产是每 50 条裁一次）
  const n = countEvents(code);
  ok(`环形裁剪：写 ${KEEP + 60} 条后库内只剩 ${KEEP} 条`, n === KEEP, `实际 ${n}`);
  const tail = recentEvents(code, 1);
  ok('裁剪留的是最新那条（不是把新的删了）', tail.length === 1 && tail[0].data.i === KEEP + 59);
}

/* ⑧ LRU：缓存有上限，被淘汰的班仍能从库读回 */
{
  // 缓存上限由 config 决定（默认 64）；这里灌 80 个班把最早的挤出去
  for (let i = 0; i < 80; i++) {
    appendEvent(`cls-lru${i}`, { kind: 'ping', uid: 'x', data: { i } });
    recentEvents(`cls-lru${i}`, 5);   // 触发加载进热缓存
  }
  ok('LRU：热缓存不会无限增长（roomStats.cached 反映真实占用）',
    roomStats('cls-lru0').cached === false, '最早的班应已被淘汰');
  const back = recentEvents('cls-lru0', 5);
  ok('LRU 淘汰后从库懒加载仍正确（数据没丢）',
    back.length === 1 && back[0].data.i === 0);
}

/* ⑨ 删班清房间 */
{
  const code = 'cls-del9';
  appendEvent(code, { kind: 'a', uid: 'd', data: {} });
  addMember(code, { id: 'u1', role: 'teacher' });
  const ev = dropRoomEvents(code);
  const mb = dropRoomMembers(code);
  ok('删班：历史与成员一并清空', ev.deleted === 1 && mb.deleted === 1
    && countEvents(code) === 0 && listMembers(code).length === 0);
}

/* ⑩ 脏输入不崩、不静默写坏数据 */
{
  ok('空班级码 → 拒写而不是写进空 key', appendEvent('', { kind: 'x' }).ok === false);
  ok('缺 kind → 拒写', appendEvent('cls-aaa111', {}).ok === false);
  // 注意：函数属性**不算**不可序列化 —— JSON.stringify({f:()=>1}) 静默丢键得 '{}'。
  // 真正会抛的是循环引用，用它验证"序列化失败要拒写"这条路。
  const cyc = { name: 'loop' };
  cyc.self = cyc;
  ok('循环引用载荷 → 拒写（不写半截数据）', appendEvent('cls-aaa111', { kind: 'x', data: cyc }).ok === false);
  ok('空 uid 加成员 → 拒', addMember('cls-aaa111', { id: '' }).ok === false);
  ok('非法角色回落 viewer（不抛错）',
    addMember('cls-aaa111', { id: 'u-bad', role: 'god' }).role === 'viewer');
  ok('角色表口径稳定', CLASS_ROLES.join(',') === 'owner,teacher,viewer');
}

/* 收尾 */
closeClassRoom();
try { fs.rmSync(dir, { recursive: true, force: true }); } catch { /* 临时目录，删不掉不影响结论 */ }

console.log(`\n结果：${pass} 过 / ${fail} 败`);
process.exit(fail === 0 ? 0 : 1);
