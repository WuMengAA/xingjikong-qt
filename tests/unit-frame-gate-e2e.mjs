// frame-governance ↔ 被控端 frame_push 端到端契约单测（四单点①）
// 跑法：node tests/unit-frame-gate-e2e.mjs
// 守的是「云端 watchers 结论 → 下发指令 → 端侧翻转运行期开关」这条链的两端形状：
// 云端只在 0↔非0 变化时发、端侧只认布尔 enabled、离线不记状态等上线重发。
// 端侧 C++ 分支的编译实证见 scripts/_build_agent.py（execute_media.cpp.obj 重新编译）。
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { pushFrameGate, resetGateState } from '../src/frame-governance.js';

let pass = 0, fail = 0;
function ok(name, fn) { try { fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + '\n       ' + e.message); } }

console.log('画面帧闸门端到端契约单测');

ok('云端：第一个管端接入 → watchers 0→1 → 下发 enabled=true', () => {
  resetGateState();
  const sent = [];
  const r = pushFrameGate('u1', 1, (uid, action, params) => {
    sent.push({ uid, action, params }); return { state: 'sent' }; });
  assert.equal(r.sent, true);
  assert.equal(sent[0].action, 'frame_push');
  assert.equal(sent[0].params.enabled, true);
});

ok('云端：最后一个管端离开 → watchers 1→0 → 下发 enabled=false（★ 省带宽那一下）', () => {
  resetGateState();
  const sent = [];
  const send = (uid, action, params) => { sent.push(params.enabled); return { state: 'sent' }; };
  pushFrameGate('u1', 1, send);
  pushFrameGate('u1', 0, send);
  assert.deepEqual(sent, [true, false]);
});

ok('云端：结论不变不重发（订阅抖动不刷指令队列）', () => {
  resetGateState();
  let n = 0;
  const send = () => { n++; return { state: 'sent' }; };
  pushFrameGate('u1', 2, send);
  pushFrameGate('u1', 3, send);
  pushFrameGate('u1', 1, send);
  assert.equal(n, 1);
});

ok('云端：设备离线不记假状态，上线补发（markConnected 尾部接线断言）', () => {
  resetGateState();
  const reg = readFileSync(new URL('../src/registry.js', import.meta.url), 'utf8');
  assert.match(reg, /resetGateState\(String\(uid \|\| ''\)\.trim\(\)\)/,
    'markConnected 必须在上线时先清状态再重算');
  assert.match(reg, /pushWatchToAgent\(String\(uid \|\| ''\)\.trim\(\)\)/,
    '清完状态必须由 pushWatchToAgent 内部按最新 watchers 触发 pushFrameGate');
  let online = false;
  const send = () => ({ state: online ? 'sent' : 'offline' });
  assert.equal(pushFrameGate('u2', 0, send).sent, false);
  online = true;
  // ★ 设备重启：端侧运行期开关回落默认「开」，云端必须先清单台状态再重发，
  //   否则 gateState 还记着「已停」就永不补发 = 永久空转（本轮抓到的真 bug）。
  resetGateState('u2');
  assert.equal(pushFrameGate('u2', 0, send).sent, true);
  const reg2 = readFileSync(new URL('../src/registry.js', import.meta.url), 'utf8');
  assert.match(reg2, /resetGateState\(String\(uid \|\| ''\)\.trim\(\)\)/,
    'markConnected 必须在重算前先清该台闸门状态');
});

ok('端侧：execute_media.cpp 真的有 frame_push 分支且写 STE_ENABLE_WS_FRAME_PUSH', () => {
  const cpp = readFileSync(new URL('../../Stelarith-control-qt/src/action/execute_media.cpp', import.meta.url), 'utf8');
  assert.match(cpp, /action == QStringLiteral\("frame_push"\)/);
  assert.match(cpp, /qputenv\("STE_ENABLE_WS_FRAME_PUSH", val\)/);
  assert.match(cpp, /qgetenv\("STE_ENABLE_WS_FRAME_PUSH"\) != val/, '必须回读验证');
  assert.match(cpp, /!v\.isBool\(\)/, '只认布尔，不猜');
});

ok('端侧：frame_push 已登记进 kActionNames（能力表单一真源）', () => {
  const caps = readFileSync(new URL('../../Stelarith-control-qt/src/action/action_caps.cpp', import.meta.url), 'utf8');
  assert.match(caps, /"frame_push"/);
});

ok('端侧：main.cpp:3062 的开关判定与本分支写的两态对得上', () => {
  const main = readFileSync(new URL('../../Stelarith-control-qt/src/main.cpp', import.meta.url), 'utf8');
  assert.match(main, /qgetenv\("STE_ENABLE_WS_FRAME_PUSH"\) == "0"/,
    'main 判 0 停；本分支写 0/1 两态，语义一致');
});

ok('★ 撞车边界：本轮没有改 control-qt/src/main.cpp / CMakeLists.txt', () => {
  const r = readFileSync(new URL('../../Stelarith-control-qt/src/main.cpp', import.meta.url), 'utf8');
  assert.ok(!r.includes('frame_push'), 'main.cpp 不该出现 frame_push（脏文件不许碰）');
});

console.log('\n════ ' + pass + ' 通过 / ' + fail + ' 失败 ════');
if (fail) process.exit(1);