// 画面帧治理 · 单测（纯函数，零 IO；跑法 node tests/unit-frame-governance.mjs）
// 守的是四单点①的根因：一个 auto 管端不该攒出 40 条幽灵订阅。
import assert from 'node:assert/strict';
import {
  shouldAutoSubscribe, watchersOf, relayBudget, idleVerdict, idleSnapshot,
  AUTO_SUBSCRIBE_MAX, RELAY_FANOUT_MAX, pushFrameGate, resetGateState,
} from '../src/frame-governance.js';

let pass = 0, fail = 0;
function ok(name, fn) { try { fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + '\n       ' + e.message); } }
const ws = (ready = 1) => ({ readyState: ready, send() {} });
const viewer = (subs = [], auto = true) => ({ subs: new Set(subs), live: new Set(), auto });

console.log('画面帧治理单测');

ok('默认上限：auto 管端只补 1 台，帧扇出封顶 8', () => {
  assert.equal(AUTO_SUBSCRIBE_MAX, 1);
  assert.equal(RELAY_FANOUT_MAX, 8);
});

ok('shouldAutoSubscribe：0 条订阅才补，第 2 台一律不补', () => {
  const v = viewer();
  assert.equal(shouldAutoSubscribe(v, 'u1', 1), true);
  v.subs.add('u1');
  assert.equal(shouldAutoSubscribe(v, 'u2', 1), false);   // ★ 根因就在这条
  assert.equal(shouldAutoSubscribe(v, 'u1', 1), false);   // 同一台也不再加
});

ok('shouldAutoSubscribe：被 pin 过的管端（auto=false）永不自动补', () => {
  assert.equal(shouldAutoSubscribe(viewer([], false), 'u1', 1), false);
  assert.equal(shouldAutoSubscribe(null, 'u1', 1), false);
  assert.equal(shouldAutoSubscribe({ auto: true }, 'u1', 1), false); // subs 不是 Set 也按 0 处理但不崩
});

ok('shouldAutoSubscribe：上限可配（2 台就允许第 2 台，第 3 台拦住）', () => {
  const v = viewer(['u1']);
  assert.equal(shouldAutoSubscribe(v, 'u2', 2), true);
  v.subs.add('u2');
  assert.equal(shouldAutoSubscribe(v, 'u3', 2), false);
});

ok('watchersOf：只数活连接、只数订阅了这台', () => {
  const m = new Map();
  m.set(ws(1), viewer(['u1']));
  m.set(ws(1), viewer(['u1','u2']));
  m.set(ws(3), viewer(['u1']));        // 断的不算
  m.set(ws(1), viewer(['u2']));        // 没订 u1 不算
  assert.equal(watchersOf(m, 'u1'), 2);
  assert.equal(watchersOf(m, 'u2'), 2);
  assert.equal(watchersOf(m, 'u3'), 0);
});

ok('relayBudget：夹到上限，负数归零', () => {
  assert.equal(relayBudget(3, 8), 3);
  assert.equal(relayBudget(99, 8), 8);
  assert.equal(relayBudget(-1, 8), 0);
});

ok('idleVerdict：没有设备说「还没有」，不写 0 台空转', () => {
  assert.equal(idleVerdict(0, 0), '还没有设备');
  assert.equal(idleVerdict(-1, 0), '还没有设备');
  assert.equal(idleVerdict(4, 0), '4 台设备都有观看者');
  assert.match(idleVerdict(40, 39), /39 \/ 40 台设备没人看/);
  assert.equal(idleVerdict(3, 99), '3 / 3 台设备没人看，仍在占链路'); // 夹紧
});

ok('★ 回归：40 台设备 + 1 个 auto 管端，只攒 1 条订阅，39 台判空转', () => {
  const m = new Map();
  const v = viewer();                    // 管端从没挑过设备，auto=true
  m.set(ws(1), v);
  const uids = Array.from({ length: 40 }, (_, i) => 'u' + i);
  for (const uid of uids) {
    if (shouldAutoSubscribe(v, uid, AUTO_SUBSCRIBE_MAX)) {
      v.subs.add(uid);
    }
  }
  assert.equal(v.subs.size, 1, 'auto 管端不该攒出 40 条幽灵订阅');
  const snap = idleSnapshot(uids, m);
  assert.equal(snap.deviceCount, 40);
  assert.equal(snap.idle.length, 39, '39 台应判为没人看');
  assert.match(snap.verdict, /39 \/ 40/);
});

ok('对照：没有 auto 上限时确实会攒出 40 条（证明断言抓的是真问题）', () => {
  const v = viewer();
  const uids = Array.from({ length: 40 }, (_, i) => 'u' + i);
  for (const uid of uids) if (shouldAutoSubscribe(v, uid, Infinity)) v.subs.add(uid);
  assert.equal(v.subs.size, 40);
});

ok('对照：0 个管端时 40 台全空转（watchers=0 的真实场景）', () => {
  const snap = idleSnapshot(Array.from({ length: 40 }, (_, i) => 'u' + i), new Map());
  assert.equal(snap.idle.length, 40);
  assert.match(snap.verdict, /40 \/ 40/);
});

ok('pushFrameGate：只在 0↔非0 变化时下发，不刷指令队列', () => {
  resetGateState();
  const sent = [];
  const send = (uid, action, params) => { sent.push({ uid, action, params }); return { state: 'sent' }; };
  assert.deepEqual(pushFrameGate('u1', 0, send), { sent: true, enabled: false, changed: true });
  assert.equal(sent.length, 1);
  assert.equal(sent[0].action, 'frame_push');
  assert.equal(sent[0].params.enabled, false);
  assert.equal(pushFrameGate('u1', 0, send).changed, false);   // 结论没变，不再发
  assert.equal(sent.length, 1);
  assert.deepEqual(pushFrameGate('u1', 2, send), { sent: true, enabled: true, changed: true });
  assert.equal(sent[1].params.enabled, true);
  assert.equal(sent.length, 2);
  assert.equal(pushFrameGate('u1', 3, send).changed, false);   // 2→3 同为「有人看」
});

ok('★ pushFrameGate：设备离线（未 sent）不记状态，上线后会重发', () => {
  resetGateState();
  let online = false;
  const send = () => ({ state: online ? 'sent' : 'offline' });
  assert.equal(pushFrameGate('u2', 0, send).sent, false);
  online = true;
  const r = pushFrameGate('u2', 0, send);   // markConnected 补发
  assert.equal(r.sent, true, '离线那次没记状态，上线必须真发出去');
  assert.equal(pushFrameGate('u2', 0, send).changed, false);
});

ok('pushFrameGate：多设备状态互不串台', () => {
  resetGateState();
  const send = () => ({ state: 'sent' });
  assert.equal(pushFrameGate('a', 0, send).sent, true);
  assert.equal(pushFrameGate('b', 0, send).sent, true);
  assert.equal(pushFrameGate('a', 0, send).changed, false);
});

ok('pushFrameGate：没给下发函数时不崩、不记状态', () => {
  resetGateState();
  assert.equal(pushFrameGate('u3', 1, null).sent, false);
  assert.equal(pushFrameGate('u3', 1, null).changed, true);
});
console.log('\n════ ' + pass + ' 通过 / ' + fail + ' 失败 ════');
if (fail) process.exit(1);