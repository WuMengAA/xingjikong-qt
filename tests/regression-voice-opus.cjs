// 语音对讲 P1（Opus）云端路由回归（2026-10-21 固化）
//
// 作用：只验**云端不解码、按 mime 原样透传**这一条，编解码正确性由 L0 单测保证。
//   1) 管理端推 mime=audio/opus 的 v1 帧 → 被控端收到 header.mime 仍是 audio/opus、载荷逐字节一致
//   2) 管理端推 mime=audio/pcm（旧端/回落路径）→ 被控端收到 audio/pcm、载荷一致
//   3) 被控端上行 audio/opus → 管理端收到 audio/opus（duplex 单播，mime 不被改写）
//   4) 被控端上行 audio/pcm（滚动升级兜底）→ 管理端收到 audio/pcm
//   5) audio.stop 后帧被丢弃（与旧行为一致，无回归）
//
// 用法（先起隔离云端）：
//   CLOUD_WS_PORT=18788 CLOUD_VIEWER_TOKEN=test-viewer-token node src/index.js
//   node tests/regression-voice-opus.cjs 18788
// 返回码：0 全过；非 0 有失败。**只对隔离实例跑，别指向生产端口。**

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const PORT = Number(process.argv[2] || 18788);
const UID = String(process.argv[3] || 'OK' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'test-viewer-token';

let pass = 0, fail = 0;
const check = (name, ok, detail) => {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
};
const waitFor = (fn, timeoutMs = 6000) => new Promise((resolve) => {
  const t0 = Date.now();
  const iv = setInterval(() => {
    const v = fn();
    if (v) { clearInterval(iv); resolve(v); }
    else if (Date.now() - t0 > timeoutMs) { clearInterval(iv); resolve(null); }
  }, 50);
});

// 与云端 protocol.js makeFrame 同格式：[1B ver=1][2B 大端 headerLen][header JSON][payload]
function makeFrame(mime, seq, payload) {
  const header = Buffer.from(JSON.stringify({
    v: 1, type: 'frame', uid: '', seq, ts: Date.now(), mime, bytes: payload.length,
  }), 'utf8');
  const out = Buffer.alloc(3 + header.length + payload.length);
  out[0] = 1;
  out[1] = (header.length >> 8) & 0xFF;
  out[2] = header.length & 0xFF;
  header.copy(out, 3);
  payload.copy(out, 3 + header.length);
  return out;
}
// 解析下发的 v1 帧 → {mime, payload, bytes}
function parseFrame(buf) {
  if (!buf || buf.length < 4 || buf[0] !== 1) return null;
  const hLen = (buf[1] << 8) | buf[2];
  if (hLen <= 0 || buf.length < 3 + hLen) return null;
  let h;
  try { h = JSON.parse(buf.subarray(3, 3 + hLen).toString('utf8')); } catch { return null; }
  return { header: h, payload: buf.subarray(3 + hLen), bytes: buf.length - 3 - hLen };
}
// 假 Opus 包：内容不重要（云端不解码），只要长度与字节可辨认
const fakeOpus = (n, tag) => Buffer.alloc(n, tag);
const sameBytes = (a, b) => !!a && !!b && a.length === b.length && Buffer.compare(a, b) === 0;

(async () => {
  const agentMsgs = [];
  let agentBin = null;
  const agent = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
  agent.on('open', () => {
    agent.send(JSON.stringify({
      v: 1, type: 'register', id: 'a1',
      payload: { uid: UID, token: AGENT_TOKEN, fp: 'p1-test', version: '9.9.9', actions: ['notify'] },
    }));
  });
  agent.on('message', (raw, isBinary) => {
    if (isBinary) agentBin = Buffer.from(raw);
    else { try { agentMsgs.push(JSON.parse(raw.toString())); } catch {} }
  });

  const viewerMsgs = [];
  let viewerBin = null;
  const viewer = new WebSocket(`ws://${HOST}:${PORT}/ws/viewer`);
  viewer.on('open', () => {
    viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v1', payload: { token: VIEWER_TOKEN } }));
  });
  viewer.on('message', (raw, isBinary) => {
    if (isBinary) viewerBin = Buffer.from(raw);
    else { try { viewerMsgs.push(JSON.parse(raw.toString())); } catch {} }
  });

  const reg = await waitFor(() => agentMsgs.find((m) => m.type === 'registered' || m.type === 'registration-ok'));
  check('被控端注册成功', !!reg, UID);
  const authed = await waitFor(() => viewerMsgs.find((m) => m.type === 'viewer-ready' || m.type === 'authed'));
  check('管理端鉴权通过', !!authed);

  // ⚠️ 必须带 duplex:true —— 语音**通话**（双向）才记录发起者 ws，学生上行才有收件人；
  //    不带 duplex 只是广播（老师 → 学生单向）。生产对讲走的就是 duplex 这条。
  viewer.send(JSON.stringify({ v: 1, type: 'audio.start', id: 'v2', payload: { duplex: true } }));
  const started = await waitFor(() => viewerMsgs.find((m) => m.type === 'audio.started'));
  check('语音 start 被接受', !!started);

  // ── 1) 下行 Opus：mime 透传 + 载荷逐字节一致 ──
  const opusDown = fakeOpus(40, 0xA1);
  agentBin = null;
  viewer.send(makeFrame('audio/opus', 11, opusDown), { binary: true });
  const downFrame = await waitFor(() => (agentBin ? parseFrame(agentBin) : null));
  check('被控端收到下行帧', !!downFrame, `bytes=${agentBin?.length}`);
  check('下行 mime = audio/opus（云端不改写）', downFrame?.header?.mime === 'audio/opus',
    `mime=${downFrame?.header?.mime}`);
  check('下行载荷 40B 逐字节一致', sameBytes(downFrame?.payload, opusDown),
    `got=${downFrame?.payload?.length}B`);

  // ── 2) 下行 PCM 兜底（旧端/编码失败回落）──
  const pcmDown = fakeOpus(640, 0xB2);
  agentBin = null;
  viewer.send(makeFrame('audio/pcm', 12, pcmDown), { binary: true });
  const pcmFrame = await waitFor(() => (agentBin ? parseFrame(agentBin) : null));
  check('下行 mime = audio/pcm 仍通（双解兜底）', pcmFrame?.header?.mime === 'audio/pcm',
    `mime=${pcmFrame?.header?.mime}`);
  check('下行 PCM 640B 逐字节一致', sameBytes(pcmFrame?.payload, pcmDown));

  // ── 3) 上行 Opus（duplex 单播回老师，mime 不被改写）──
  const opusUp = fakeOpus(41, 0xC3);
  viewerBin = null;
  agent.send(makeFrame('audio/opus', 21, opusUp), { binary: true });
  const upFrame = await waitFor(() => (viewerBin ? parseFrame(viewerBin) : null));
  check('管理端收到上行 Opus 帧', !!upFrame, `bytes=${viewerBin?.length}`);
  check('上行 mime = audio/opus（透传）', upFrame?.header?.mime === 'audio/opus',
    `mime=${upFrame?.header?.mime}`);
  check('上行载荷 41B 逐字节一致', sameBytes(upFrame?.payload, opusUp));

  // ── 4) 上行 PCM 兜底 ──
  const pcmUp = fakeOpus(640, 0xD4);
  viewerBin = null;
  agent.send(makeFrame('audio/pcm', 22, pcmUp), { binary: true });
  const upPcm = await waitFor(() => (viewerBin ? parseFrame(viewerBin) : null));
  check('上行 mime = audio/pcm 仍通', upPcm?.header?.mime === 'audio/pcm',
    `mime=${upPcm?.header?.mime}`);

  // ── 5) stop 后帧被丢弃（无回归）──
  viewer.send(JSON.stringify({ v: 1, type: 'audio.stop', id: 'v3', payload: {} }));
  const stopped = await waitFor(() => viewerMsgs.find((m) => m.type === 'audio.stopped'));
  check('语音 stop 被接受', !!stopped);
  agentBin = null;
  viewer.send(makeFrame('audio/opus', 31, fakeOpus(40, 0xE5)), { binary: true });
  await new Promise((r) => setTimeout(r, 1200));
  check('stop 后 Opus 帧被丢弃', agentBin === null, agentBin ? `意外收到 ${agentBin.length}B` : '无');

  agent.close(); viewer.close();
  console.log(`\n\u2550\u2550 结果: ${pass} 通过 / ${fail} 失败 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();
