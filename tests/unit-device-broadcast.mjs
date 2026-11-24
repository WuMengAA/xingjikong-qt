// 设备侧广播排期端点单测（方案 C T2 · 2026-11-24）
//
// 守的是他线 06-可开工清单 §3 的五条验收：
//   ① 无班级 → 200 NO_CLASS       ② 有班级无排期 → 200 NO_SCHEDULE
//   ③ 正常 → items 按 seq 升序、rev 与库一致
//   ④ 错令牌 → 401（由 deviceGet 负责，本测覆盖分派层）  ⑤ 查别班不影响本班
//
// 用依赖注入（不碰真实 sqlite），跑法：node tests/unit-device-broadcast.mjs

import assert from 'node:assert/strict';
import { deviceBroadcastSchedule, STALE_AFTER_MS } from '../src/device-broadcast.js';

let pass = 0, fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + '\n       ' + e.message); }
}

const DAY = '2026-11-24';
const base = {
  uid: 'agent-class-8',
  classCodeOf: () => 'class_xlzx_2028_08',
  tenantOf: () => 'default',
  classNameOf: () => '2028届8班',
  date: DAY,
};

console.log('device-broadcast 单测');

ok('① 无班级 → 200 NO_CLASS', () => {
  const r = deviceBroadcastSchedule({ ...base, classCodeOf: () => '' });
  assert.equal(r.status, 200);
  assert.equal(r.body.reason, 'NO_CLASS');
  assert.deepEqual(r.body.items, []);
});

ok('② 有班级无排期 → 200 NO_SCHEDULE', () => {
  const r = deviceBroadcastSchedule({ ...base, read: () => null });
  assert.equal(r.status, 200);
  assert.equal(r.body.reason, 'NO_SCHEDULE');
  assert.equal(r.body.classCode, 'class_xlzx_2028_08');
  assert.equal(r.body.className, '2028届8班');
});

ok('③ 正常 → items 按 seq 升序、rev 与库一致', () => {
  const items = [
    { seq: 3, startMs: 300, endMs: 400, title: '第三首' },
    { seq: 1, startMs: 100, endMs: 200, title: '第一首' },
    { seq: 2, startMs: 200, endMs: 300, title: '第二首' },
  ];
  const r = deviceBroadcastSchedule({
    ...base,
    read: () => ({ play_date: DAY, rev: 17, updated_at: Date.now(), items_json: JSON.stringify(items) }),
  });
  assert.equal(r.status, 200);
  assert.equal(r.body.rev, 17);
  assert.deepEqual(r.body.items.map((x) => x.seq), [1, 2, 3]);
  assert.equal(r.body.reason, '');
  assert.equal(r.body.stale, false);
  assert.equal(r.headers.etag, '17');
  assert.ok(r.headers['cache-control'].includes('max-age=300'));
});

ok('④ 坏日期 → 200 NO_DATE', () => {
  const r = deviceBroadcastSchedule({ ...base, date: 'not-a-date' });
  assert.equal(r.status, 200);
  assert.equal(r.body.reason, 'NO_DATE');
});

ok('⑤ 超过 24h → stale=true', () => {
  const old = Date.now() - STALE_AFTER_MS - 1000;
  const r = deviceBroadcastSchedule({
    ...base,
    read: () => ({ play_date: DAY, rev: 3, updated_at: old, items_json: '[]' }),
  });
  assert.equal(r.body.stale, true);
});

ok('⑥ 查别班不影响本班（read 收到的是自己班号）', () => {
  let seen = null;
  deviceBroadcastSchedule({
    ...base,
    read: (cc, tenant, day) => { seen = { cc, tenant, day }; return null; },
  });
  assert.deepEqual(seen, { cc: 'class_xlzx_2028_08', tenant: 'tn_default', day: DAY });
});

ok('⑦ 缺 uid → 400', () => {
  const r = deviceBroadcastSchedule({ ...base, uid: '' });
  assert.equal(r.status, 400);
});

ok('⑧ 坏 items_json 不崩，回落 NO_SCHEDULE', () => {
  const r = deviceBroadcastSchedule({
    ...base,
    read: () => ({ play_date: DAY, rev: 1, updated_at: Date.now(), items_json: '{bad' }),
  });
  assert.equal(r.status, 200);
  assert.equal(r.body.reason, 'NO_SCHEDULE');
});

console.log('');
console.log('== SUMMARY: PASS ' + pass + ' / FAIL ' + fail + ' ==');
process.exit(fail === 0 ? 0 : 1);
