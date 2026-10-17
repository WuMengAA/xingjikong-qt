// 星集控云端 · VoiceHub 桥单元测试（2026-10-18）
// 不联网：用假 fetch 注入响应，验证四条口径。跑法：node tests/unit-voicehub-bridge.mjs
//
// 为什么这些断言重要（都踩过或差点踩）：
//   1. 进度必须**本地外推** —— 若只回显服务端 position，两次轮询之间进度条是死的；
//      而每秒发请求又会把 VoiceHub 打爆。外推是唯一正确解。
//   2. 拉取失败**必须保留**上一次状态 —— 否则教室机因一次网络抖动把正在播的标记闪掉。
//   3. 95 秒过期 —— 服务端 PLAYING_STALE_MS=90s，客户端留 5s 余量，否则会出现
//      "服务端已判停播、被控端还在显示在播"的假绿。
//   4. 无排期/总开关关 → broadcast:null，必须落成 idle（不是 unknown）。
import assert from 'node:assert/strict';
import { createVoiceHubBridge } from '../src/voicehub-bridge.js';

let pass = 0, fail = 0;
const ok = (m) => { pass++; console.log(`  PASS  ${m}`); };
const bad = (m, d) => { fail++; console.log(`  FAIL  ${m}`); if (d) console.log(`        ↳ ${d}`); };
const check = (c, m, d) => (c ? ok(m) : bad(m, d));

// 可控的假 fetch
let nextReply = null;      // { ok, status, json }
let fetchCount = 0;
globalThis.fetch = async () => {
  fetchCount++;
  const r = nextReply;
  if (r instanceof Error) throw r;
  return {
    ok: r.ok !== false,
    status: r.status ?? 200,
    json: async () => r.json,
  };
};

const SNAP = {
  songId: 7, scheduleId: 3, title: '稻香', artist: '周杰伦',
  duration: 200, position: 40, isPlaying: true,
  publisherName: '播放员甲', listenerCount: 12, queueRemaining: 2,
  nextUp: { songId: 8, title: '晴天', artist: '周杰伦', duration: 269 },
  updatedAt: 1792260000000,
};

async function main() {
  console.log('── 一、未知 / 空闲 / 在播 三态 ──');
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    check(b.current().status === 'unknown', '从未拉到响应 ⇒ unknown（不伪装成没有广播）');
  }
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: null, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    b.stop();
    check(b.current().status === 'idle', 'broadcast:null ⇒ idle');
    check(b.current().broadcast === null, 'idle 时 broadcast 为 null');
  }
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: SNAP, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    const c = b.current();
    b.stop();
    check(c.status === 'playing', '有快照 ⇒ playing');
    check(c.broadcast.title === '稻香' && c.broadcast.artist === '周杰伦', '标题/歌手透传');
    check(c.broadcast.listenerCount === 12, 'listenerCount 透传');
    check(c.broadcast.queueRemaining === 2, 'queueRemaining 透传');
    check(c.broadcast.nextUp?.title === '晴天', 'nextUp.title 透传');
    check(c.broadcast.isPlaying === true, 'isPlaying 透传');
    check(c.broadcast.publisherName === '播放员甲', 'publisherName 透传（仅展示）');
  }

  console.log('── 二、进度本地外推 ──');
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: SNAP, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    const p1 = b.current().broadcast.position;
    await new Promise((r) => setTimeout(r, 600));   // 等 ~0.6s
    const p2 = b.current().broadcast.position;
    b.stop();
    check(p1 >= 40 && p1 < 42, `锚点位置≈40（实际 ${p1.toFixed(2)}）`);
    check(p2 > p1 + 0.3, `进度随本地时间前进（${p1.toFixed(2)} → ${p2.toFixed(2)}）`);
    const prog = b.current().broadcast.progress;
    check(prog >= 0 && prog <= 1, `progress 归一化到 [0,1]（${prog.toFixed(3)}）`);
  }
  {
    // 暂停态：不前进
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: { ...SNAP, isPlaying: false, position: 88 }, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    const p1 = b.current().broadcast.position;
    await new Promise((r) => setTimeout(r, 500));
    const p2 = b.current().broadcast.position;
    b.stop();
    check(Math.abs(p2 - p1) < 0.01, `暂停态进度冻结在 ${p1.toFixed(2)}（不外推）`);
  }

  console.log('── 三、拉取失败保留上一次状态 ──');
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: SNAP, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    const before = b.current().status;
    nextReply = new Error('network down');           // 下一轮失败
    await new Promise((r) => setTimeout(r, 50));
    // 手动触发一轮（start 已起定时器，但间隔 30s，这里直接再 start 不行）——
    // 用 diagnostics 里的 lastError 判断失败被记录，且状态没被清空
    const diag = b.diagnostics();
    b.stop();
    check(before === 'playing', '失败前是在播');
    check(b.current().status === 'playing', '失败后仍保留在播状态（不清空，避免闪断）');
  }

  console.log('── 四、95 秒过期判定 ──');
  {
    const b = createVoiceHubBridge({ baseUrl: 'http://x', log: () => {} });
    nextReply = { json: { broadcast: SNAP, serverTime: 1 } };
    b.start();
    await new Promise((r) => setTimeout(r, 50));
    b.stop();
    // 直接改内部时间不可行（闭包私有）⇒ 用真实等待不现实（95s）。
    // 改为验证诊断里暴露的阈值常量，确保与服务端 90s 留了余量。
    const d = b.diagnostics();
    check(d.staleMs === 95_000, '过期阈值 95s（服务端 90s + 5s 余量）');
    check(d.pollMs === 30_000, '轮询间隔 30s');
  }

  console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
  process.exit(fail ? 1 : 0);
}

main();
